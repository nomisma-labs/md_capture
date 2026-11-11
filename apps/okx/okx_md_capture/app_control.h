// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

#include "app_events.h"

#include <base/util/enum.h>
#include <json_config/config.h>
#include <spd_logger/logging.h>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/system/error_code.hpp>
#include <csignal>
#include <cstdio>
#include <sys/types.h>

import std;
import nom.asio_comm;
import nom.core;

namespace nom
{
    NOM_ENUM(app_status, uint8_t, init, starting, running, stopping, success, failure)

    struct app_control_config
    {
        cfg_optional<cfg_uint> exit_after_sec;
        cfg_defaulted<cfg_uint> health_check_period_msec{2000};

        std::expected<void, std::string> validate() const;
    };

    NOM_JCONFIG(app_control_config, exit_after_sec, health_check_period_msec)

    // Place after nom::asio_service and before every other lifecycle handler.
    template <typename Traits> class app_control
    {
        using relay = nom::relay<Traits>;

    public:
        struct context_mixin
        {
            app_status status() const noexcept
            {
                const auto& body = static_cast<const typename Traits::body&>(*this);
                return static_cast<const app_control&>(body).status_;
            }
        };

        app_control() : signals_(nom::ctx(this).io.get_executor(), SIGINT, SIGTERM, SIGQUIT)
        {
            if (cfg().exit_after_sec)
                exit_timer_.emplace(nom::ctx(this).io.get_executor());
            if (*cfg().health_check_period_msec)
                health_timer_.emplace(nom::ctx(this).io.get_executor());
        }

        void on_request(nom::event::app_start event)
        {
            switch (status_)
            {
            case app_status::init:
            case app_status::success:
            case app_status::failure:
                status_ = app_status::starting;
                ++run_generation_;
                wait_for_signal();
                if (exit_timer_)
                {
                    // The runtime limit includes synchronous start delivery.
                    exit_timer_->arm_after(std::chrono::seconds{static_cast<std::int64_t>(*cfg().exit_after_sec)},
                                           [this] { on_exit_timer(); });
                }
                relay::pass_request(this, event);
                finalize_app_start();
                return;
            case app_status::starting:
            case app_status::running:
            case app_status::stopping:
                return;
            }
        }

        void on_request(nom::event::app_stop)
        {
            switch (status_)
            {
            case app_status::starting:
            case app_status::running:
                status_ = app_status::stopping;
                begin_stop();
                if (app_status::stopping == status_) //...finalize app stop
                    status_ = app_status::success;
                return;
            case app_status::init:
            case app_status::stopping:
            case app_status::success:
            case app_status::failure:
                return;
            }
        }

        void on_response(nom::event::app_finished)
        {
            switch (status_)
            {
            case app_status::starting:
            case app_status::running:
                status_ = app_status::stopping;
                begin_stop();
                if (app_status::stopping == status_) //...finalize app stop
                    status_ = app_status::success;
                return;
            case app_status::init:
            case app_status::stopping:
            case app_status::success:
            case app_status::failure:
                return;
            }
        }

        void on_response(nom::event::app_failed)
        {
            switch (status_)
            {
            case app_status::starting:
            case app_status::running:
                status_ = app_status::stopping;
                begin_stop();
                if (app_status::stopping == status_) //...finalize app stop
                    status_ = app_status::failure;
                return;
            case app_status::init:
            case app_status::stopping:
            case app_status::success:
                status_ = app_status::failure;
                return;
            case app_status::failure:
                return;
            }
        }

    private:
        const app_control_config& cfg() const noexcept
        {
            return *nom::ctx(this).cfg().app_control;
        }

        // Pseudo-event after downstream start delivery returns.
        void finalize_app_start()
        {
            switch (status_)
            {
            case app_status::starting:
                status_ = app_status::running;
                if (health_timer_)
                    schedule_health_check();
                return;
            case app_status::success:
            case app_status::failure:
            case app_status::init:
            case app_status::running:
            case app_status::stopping:
                return;
            }
        }

        void wait_for_signal()
        {
            signals_.async_wait(
                [this, generation = run_generation_](const boost::system::error_code& error, int signal_number)
                { on_signal(error, signal_number, generation); });
        }

        void on_signal(const boost::system::error_code& error, int signal_number, std::uint64_t generation)
        {
            switch (status_)
            {
            case app_status::starting:
            case app_status::running:
                if (error || generation != run_generation_)
                    return;
                switch (signal_number)
                {
                case SIGQUIT:
                    // Ctrl-\ flushes operational logs without stopping capture.
                    nom::flush_logger(nom::main_logger);
                    wait_for_signal();
                    return;
                case SIGINT:
                    LINFO("Stopping on signal {}", signal_number);
                    on_request(nom::event::app_stop{});
                    return;
                case SIGTERM:
                    LINFO("Stopping on signal {}", signal_number);
                    // Application convention: SIGTERM reports failure through normal shutdown.
                    on_response(nom::event::app_failed{});
                    return;
                }
                return;
            case app_status::init:
            case app_status::stopping:
            case app_status::success:
            case app_status::failure:
                return;
            }
        }

        void on_exit_timer()
        {
            switch (status_)
            {
            case app_status::starting:
            case app_status::running:
                LINFO("Stopping after {} seconds running", static_cast<std::uint64_t>(*cfg().exit_after_sec));
                on_request(nom::event::app_stop{});
                return;
            case app_status::init:
            case app_status::stopping:
            case app_status::success:
            case app_status::failure:
                return;
            }
        }

        void schedule_health_check()
        {
            const std::chrono::milliseconds period{static_cast<std::int64_t>(*cfg().health_check_period_msec)};
            health_timer_->arm_after(period, [this] { on_health_timer(); });
        }

        void on_health_timer()
        {
            switch (status_)
            {
            case app_status::running:
                // A check can synchronously stop the app and must cancel the next tick.
                schedule_health_check();
                relay::pass_request(this, nom::event::app_health_check{});
                return;
            case app_status::init:
            case app_status::starting:
            case app_status::stopping:
            case app_status::success:
            case app_status::failure:
                return;
            }
        }

        void begin_stop()
        {
            if (exit_timer_)
                exit_timer_->cancel();
            if (health_timer_)
                health_timer_->cancel();
            signals_.cancel();
            relay::pass_request(this, nom::event::app_stop{});
        }

        boost::asio::signal_set signals_;
        std::optional<nom::timer> exit_timer_;
        std::optional<nom::timer> health_timer_;
        std::uint16_t run_generation_{};
        app_status status_{app_status::init};
    };

    inline std::expected<void, std::string> app_control_config::validate() const
    {
        if (exit_after_sec && *exit_after_sec > 86400)
            return std::unexpected("/exit_after_sec must be 0..86400 or null");
        if (*health_check_period_msec > 60000)
            return std::unexpected("/health_check_period_msec must be 0..60000 (0 disables health checks)");
        return {};
    }
} // namespace nom
