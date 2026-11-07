// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <catch2/catch_test_macros.hpp>
#include <json_config/config.h>

#include "../../../apps/okx/okx_md_capture/asio_service.h"

#include "../../../apps/okx/okx_md_capture/app_terminator.h"
#include "../../../apps/okx/okx_md_capture/config_owner.h"
#include "../../../apps/okx/okx_md_capture/health_checker.h"
#include "../../../apps/okx/okx_md_capture/md_sink.h"
#include "../../../apps/okx/okx_md_capture/md_subscriber.h"

import std;
import nom.assembly;
import nom.spd_logger;

using namespace std::chrono_literals;

namespace
{
    struct test_config
    {
        static constexpr bool require_completion = false;
        std::optional<unsigned> exit_after_sec;
        nom::cfg_defaulted<nom::health_checker_config> health_checker;
        nom::md_sink_config md_sink;
        okx::md_subscriber_config md_subscriber;

        std::expected<void, std::string> validate() const
        {
            if (const auto result = health_checker->validate(); !result)
                return result;
            if (const auto result = md_sink.validate(); !result)
                return result;
            return md_subscriber.validate();
        }
    };

    template <typename Traits> struct trailing_probe
    {
        struct context_mixin
        {
            unsigned starts{};
            unsigned stops{};
            unsigned checks{};
        };

        void on_request(nom::event::app_start event)
        {
            ++nom::ctx(this).starts;
            nom::relay<Traits>::pass_request(this, event);
        }

        void on_request(nom::event::app_stop event)
        {
            ++nom::ctx(this).stops;
            nom::relay<Traits>::pass_request(this, event);
        }

        void on_request(nom::event::app_health_check event)
        {
            ++nom::ctx(this).checks;
            nom::relay<Traits>::pass_request(this, event);
        }
    };

    struct test_assembly : nom::assembly<test_assembly,
                                         nom::config_owner,
                                         nom::asio_service,
                                         nom::app_terminator,
                                         nom::health_checker,
                                         nom::md_sink,
                                         okx::md_subscriber,
                                         trailing_probe>
    {
        using config = test_config;
        struct custom_traits : nom::event_suppressors
        {
            using suppressed_requests =
                nom::type_list<nom::event::app_start, nom::event::app_stop, nom::event::app_health_check>;
        };
    };

    test_config make_config()
    {
        test_config config;
        config.health_checker->period_msec = {0};
        // Opening an existing directory as a data file fails without modifying it.
        config.md_sink.md_logger.file = std::filesystem::temp_directory_path().string();
        config.md_subscriber.connection->endpoint = {"wss://127.0.0.1:1"};
        config.md_subscriber.subscription.symbols = {"BTC-USDT"};
        return config;
    }
} // namespace

TEST_CASE("Application events reach nodes after the capture sink and subscriber")
{
    nom::init_logger(nom::main_logger);
    const auto config = make_config();
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    app.request(nom::event::app_health_check{});
    // Cancel before running the I/O context: no live exchange is needed.
    app.request(nom::event::app_stop{});
    app.request(nom::event::app_stop{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().checks == 1);
    CHECK(app.ctx().stops == 2);
    CHECK(app.ctx().exit_code == 0);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("A failed sink forwards health checks without generating repeated stops")
{
    nom::init_logger(nom::main_logger);
    const auto config = make_config();
    nom::relay<test_assembly> app(config);
    app.respond(nom::capture_event{std::chrono::system_clock::now(), R"({"price":"1"})"});
    // Draining the writer exposes the storage failure deterministically.
    app.request(nom::event::app_stop{});
    REQUIRE(app.ctx().exit_code == 1);
    REQUIRE(app.ctx().stops == 1);
    app.request(nom::event::app_health_check{});
    CHECK(app.ctx().checks == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().exit_code == 1);
}
