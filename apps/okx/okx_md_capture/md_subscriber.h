// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

#include "app_events.h"
#include "compiler_workarounds.h"
#include "feed_events.h"

#include <json_config/config.h>
#include <spd_logger/logging.h>

import std;
import nom.asio_comm;
import nom.assembly;
import nom.json_config;
import nom.md_logger;
import nom.spd_logger;
import nom.url;

namespace okx
{
    using namespace nom::cfg_types;

    inline std::string normalized_symbol(std::string symbol)
    {
        const auto& facet = std::use_facet<std::ctype<char>>(std::locale::classic());

        facet.toupper(symbol.data(), symbol.data() + symbol.size());
        return symbol;
    }

    struct connection_config
    {
        cfg_defaulted<cfg_string> endpoint{"wss://ws.okx.com/ws/v5/public"};
        cfg_defaulted<cfg_uint> heartbeat_sec{20};

        std::expected<void, std::string> validate() const;
    };

    NOM_JCONFIG(connection_config, endpoint, heartbeat_sec)

    struct subscription_config
    {
        cfg_vector<cfg_string> symbols;
        cfg_defaulted<cfg_vector<cfg_string>> channels{{"tickers", "trades", "books"}};

        std::expected<void, std::string> validate() const;
    };

    NOM_JCONFIG(subscription_config, symbols, channels)

    struct md_subscriber_config
    {
        cfg_defaulted<connection_config> connection;
        subscription_config subscription;

        std::expected<void, std::string> validate() const;
    };

    NOM_JCONFIG(md_subscriber_config, connection, subscription)

    template <typename Traits> class md_subscriber
    {
        using relay = nom::relay<Traits>;

    public:
        // config_owner constructs and validates the context before nodes; GCC 16 loses that ordering here.
        NOM_GCC16_IGNORE_BEGIN("-Wmaybe-uninitialized")
        md_subscriber()
            : request_(make_subscription(nom::ctx(this).cfg().md_subscriber.subscription)),
              client_(nom::ctx(this).io.get_executor(),
                      nom::url(*nom::ctx(this).cfg().md_subscriber.connection->endpoint)),
              heartbeat_(nom::ctx(this).io.get_executor())
        {
        }
        NOM_GCC16_IGNORE_END

        void on_request(nom::event::app_start event)
        {
            client_.connect(nom::web_socket::ws_delegate{std::ref(*this)});
            relay::pass_request(this, event);
        }

        void on_request(nom::event::app_stop event)
        {
            heartbeat_.cancel();
            client_.close();
            relay::pass_request(this, event);
        }

        void operator()(nom::web_socket& connection, const nom::event::on_connected&)
        {
            LINFO("Connected: {}", connection.session_id());
            relay::pass_response(this, nom::event::feed_event::connected);
            if (!connection.is_connected())
                return;
            if (!connection.send(request_))
            {
                fail("Subscription send failed");
                return;
            }
            arm_heartbeat();
        }

        void operator()(nom::web_socket&, const nom::event::ws_data& event)
        {
            const auto received_at = std::chrono::system_clock::now();
            // OKX pong is application heartbeat traffic; data records contain received JSON objects.
            if (event.sv_data() != "pong")
                relay::pass_response(this, nom::capture_event{received_at, event.sv_data()});
            if (!client_.is_connected())
                return;
            if (!awaiting_pong_ || event.sv_data() == "pong")
            {
                awaiting_pong_ = false;
                arm_heartbeat();
            }
        }

        void operator()(nom::web_socket&, const nom::event::on_disconnected& event)
        {
            fail(event.reason);
        }

        void operator()(nom::web_socket&, const nom::event::network_error& event)
        {
            fail(event.message);
        }

    private:
        const md_subscriber_config& cfg() const noexcept
        {
            return nom::ctx(this).cfg().md_subscriber;
        }

        static std::string make_subscription(const subscription_config& config)
        {
            nlohmann::json args = nlohmann::json::array();
            for (const auto& symbol : config.symbols)
                for (const auto& channel : *config.channels)
                    args.push_back({{"channel", channel}, {"instId", normalized_symbol(symbol)}});
            auto request = nlohmann::json{{"op", "subscribe"}, {"args", args}}.dump();
            if (request.size() > 64 * 1024)
                throw std::invalid_argument("Subscription exceeds 64 KiB");
            return request;
        }

        void fail(std::string_view reason)
        {
            LERROR("Feed failed: {}", reason);
            relay::pass_response(this, nom::event::feed_event::failed);
        }

        void arm_heartbeat()
        {
            const std::chrono::seconds period{static_cast<std::int64_t>(*cfg().connection->heartbeat_sec)};
            heartbeat_.arm_after(period,
                                 [this]
                                 {
                                     if (awaiting_pong_)
                                     {
                                         fail("Application pong deadline expired");
                                         return;
                                     }
                                     if (!client_.send("ping"))
                                     {
                                         fail("Application ping send failed");
                                         return;
                                     }
                                     awaiting_pong_ = true;
                                     arm_heartbeat();
                                 });
        }

        std::string request_;
        nom::web_socket client_;
        nom::timer heartbeat_;
        bool awaiting_pong_{};
    };

    inline std::expected<void, std::string> connection_config::validate() const
    {
        try
        {
            if (nom::url(*endpoint).scheme() != "wss")
                return std::unexpected("/endpoint must use wss://");
        }
        catch (const std::invalid_argument& error)
        {
            return std::unexpected(std::string("/endpoint: ") + error.what());
        }
        if (*heartbeat_sec == 0 || *heartbeat_sec >= 30)
            return std::unexpected("/heartbeat_sec must be 1..29");
        return {};
    }

    inline std::expected<void, std::string> subscription_config::validate() const
    {
        if (symbols.empty())
            return std::unexpected("/symbols must be nonempty");
        if (channels->empty())
            return std::unexpected("/channels must be nonempty");
        std::set<std::string> unique_symbols;
        for (const auto& symbol : symbols)
        {
            const auto normalized = normalized_symbol(symbol);
            if (normalized.empty() || normalized.front() == '-' || normalized.back() == '-' ||
                std::ranges::count(normalized, '-') != 1 ||
                normalized.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") != std::string::npos)
                return std::unexpected("/symbols requires Spot identifiers such as BTC-USDT");
            if (!unique_symbols.insert(normalized).second)
                return std::unexpected("/symbols contains a duplicate after uppercase normalization");
        }
        std::set<std::string> unique_channels;
        for (const auto& channel : *channels)
        {
            if (channel != "tickers" && channel != "trades" && channel != "books")
                return std::unexpected("/channels accepts tickers, trades and books");
            if (!unique_channels.insert(channel).second)
                return std::unexpected("/channels contains a duplicate");
        }
        return {};
    }

    inline std::expected<void, std::string> md_subscriber_config::validate() const
    {
        if (const auto result = connection->validate(); !result)
            return std::unexpected("/connection" + result.error());
        if (const auto result = subscription.validate(); !result)
            return std::unexpected("/subscription" + result.error());
        return {};
    }
} // namespace okx
