// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

#include "app_events.h"
#include "feed_events.h"

// GCC 16's Ubuntu snapshot needs stdio declarations before module imports at -O2.
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
#include <cstdio>
#endif

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/system/error_code.hpp>
#include <csignal>
#include <spd_logger/logging.h>

import std;
import nom.asio_comm;
import nom.assembly;
import nom.spd_logger;

namespace nom
{
    // Place after nom::asio_service and before every other stop handler: pass_request visits later nodes.
    // Config supplies exit_after_sec and require_completion; the timer starts on feed_event::connected.
    // Providers require a completed event for success and keep stop diagnostics on stderr.
    template <typename Traits> class app_terminator
    {
        using relay = nom::relay<Traits>;

    public:
        struct context_mixin
        {
            // Unset while running; shutdown failures can turn a successful stop into failure.
            std::optional<int> exit_code;
        };

        app_terminator() : signals_(nom::ctx(this).io.get_executor(), SIGINT, SIGTERM)
        {
        }

        void on_request(nom::event::app_start event)
        {
            signals_.async_wait(
                [this](const boost::system::error_code& error, int signal_number)
                {
                    if (!error && !nom::ctx(this).exit_code)
                    {
                        report_stop(std::format("Stopping on signal {}", signal_number));
                        stop(cfg().require_completion ? 1 : 0);
                    }
                });
            relay::pass_request(this, event);
        }

        void on_request(nom::event::app_stop event)
        {
            if (!nom::ctx(this).exit_code)
                nom::ctx(this).exit_code = cfg().require_completion ? 1 : 0;
            if (exit_timer_)
                exit_timer_->cancel();
            signals_.cancel();
            relay::pass_request(this, event);
        }

        void on_response(nom::event::feed_event event)
        {
            switch (event)
            {
            case nom::event::feed_event::connected:
                if (!cfg().exit_after_sec)
                    break;
                if (*cfg().exit_after_sec == 0)
                {
                    report_stop("Stopping after connection (--exit-after-sec 0)");
                    stop(cfg().require_completion ? 1 : 0);
                    break;
                }
                exit_timer_.emplace(nom::ctx(this).io.get_executor());
                exit_timer_->arm_after(std::chrono::seconds{static_cast<std::int64_t>(*cfg().exit_after_sec)},
                                       [this]
                                       {
                                           report_stop(std::format("Stopping after {} seconds connected",
                                                                   static_cast<std::uint64_t>(*cfg().exit_after_sec)));
                                           stop(cfg().require_completion ? 1 : 0);
                                       });
                break;
            case nom::event::feed_event::completed:
                stop(0);
                break;
            case nom::event::feed_event::failed:
                stop(1);
                break;
            case nom::event::feed_event::stop_failed:
                // A stop handler may discover failure after shutdown has already begun.
                stop(1);
                nom::ctx(this).exit_code = 1;
                break;
            }
        }

    private:
        const auto& cfg() const noexcept
        {
            return nom::ctx(this).cfg();
        }

        void report_stop(std::string_view message)
        {
            if (cfg().require_completion)
                std::cerr << message << '\n';
            else
                LINFO("{}", message);
        }

        void stop(int exit_code)
        {
            // Generate a stop only once; shutdown responses must not trigger another stop.
            if (nom::ctx(this).exit_code)
                return;
            nom::ctx(this).exit_code = exit_code;
            on_request(nom::event::app_stop{});
        }

        boost::asio::signal_set signals_;
        std::optional<nom::timer> exit_timer_;
    };
} // namespace nom
