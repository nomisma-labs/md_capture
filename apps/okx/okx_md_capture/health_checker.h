// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

// Load JSON headers before module imports.
#include <boost/asio/any_io_executor.hpp>
#include <json_config/config.h>

#include "app_events.h"

import std;
import nom.asio_comm;
import nom.assembly;
import nom.json_config;

namespace nom
{
    struct health_checker_config
    {
        cfg_defaulted<cfg_uint> period_msec{2000};

        std::expected<void, std::string> validate() const;
    };

    NOM_JCONFIG(health_checker_config, period_msec)

    template <typename Traits> class health_checker
    {
        using relay = nom::relay<Traits>;

    public:
        health_checker()
        {
            if (*cfg().period_msec)
                timer_.emplace(nom::ctx(this).io.get_executor());
        }

        void on_request(nom::event::app_start event)
        {
            if (timer_)
                schedule_check();
            relay::pass_request(this, event);
        }

        void on_request(nom::event::app_stop event)
        {
            if (timer_)
                timer_->cancel();
            relay::pass_request(this, event);
        }

    private:
        const health_checker_config& cfg() const noexcept
        {
            return *nom::ctx(this).cfg().health_checker;
        }

        void schedule_check()
        {
            const std::chrono::milliseconds period{static_cast<std::int64_t>(*cfg().period_msec)};
            timer_->arm_after(period,
                              [this]
                              {
                                  // A check can synchronously trigger stop, which must cancel the next tick.
                                  schedule_check();
                                  relay::pass_request(this, nom::event::app_health_check{});
                              });
        }

        std::optional<nom::timer> timer_;
    };

    inline std::expected<void, std::string> health_checker_config::validate() const
    {
        constexpr cfg_uint max_period{60 * 1000};
        if (*period_msec > max_period)
            return std::unexpected("/period_msec must be 0..60000 (0 disables health checks)");
        return {};
    }
} // namespace nom
