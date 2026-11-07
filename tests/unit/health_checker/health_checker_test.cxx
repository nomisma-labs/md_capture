// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#include <catch2/catch_test_macros.hpp>
#include <json_config/config.h>

#include "../../../apps/okx/okx_md_capture/asio_service.h"

#include "../../../apps/okx/okx_md_capture/app_events.h"
#include "../../../apps/okx/okx_md_capture/app_terminator.h"
#include "../../../apps/okx/okx_md_capture/config_owner.h"
#include "../../../apps/okx/okx_md_capture/feed_events.h"
#include "../../../apps/okx/okx_md_capture/health_checker.h"

import std;
import nom.assembly;

using namespace std::chrono_literals;

namespace
{
    struct test_config
    {
        static constexpr bool require_completion = false;
        std::optional<unsigned> exit_after_sec;
        nom::cfg_defaulted<nom::health_checker_config> health_checker;

        std::expected<void, std::string> validate() const
        {
            return health_checker->validate();
        }
    };

    template <typename Traits> struct first_probe
    {
        struct context_mixin
        {
            std::vector<unsigned> visits;
            unsigned starts{};
            unsigned stops{};
            unsigned completed_checks{};
            bool fail_check{};
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
            auto& context = nom::ctx(this);
            context.visits.push_back(1);
            if (context.fail_check)
            {
                nom::relay<Traits>::pass_response(this, nom::event::feed_event::failed);
            }
            nom::relay<Traits>::pass_request(this, event);
        }
    };

    template <typename Traits> struct last_probe
    {
        void on_request(nom::event::app_health_check event)
        {
            auto& context = nom::ctx(this);
            context.visits.push_back(2);
            if (++context.completed_checks == 3)
                nom::relay<Traits>::pass_response(this, nom::event::feed_event::completed);
            nom::relay<Traits>::pass_request(this, event);
        }
    };

    struct test_assembly : nom::assembly<test_assembly,
                                         nom::config_owner,
                                         nom::asio_service,
                                         nom::app_terminator,
                                         nom::health_checker,
                                         first_probe,
                                         last_probe>
    {
        using config = test_config;
        struct custom_traits : nom::event_suppressors
        {
            using suppressed_requests =
                nom::type_list<nom::event::app_health_check, nom::event::app_start, nom::event::app_stop>;
        };
    };
} // namespace

TEST_CASE("Health checks recur, reach each handler and stop when a check completes the service")
{
    test_config config;
    config.health_checker->period_msec = {5};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    CHECK(app.ctx().visits.empty());
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().visits == std::vector<unsigned>{1, 2, 1, 2, 1, 2});
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().exit_code == 0);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Every explicit stop reaches handlers and cancels pending health checks")
{
    test_config config;
    config.health_checker->period_msec = {5};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    app.request(nom::event::app_stop{});
    app.request(nom::event::app_stop{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().visits.empty());
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 2);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("A failed health check reaches later handlers and cancels future checks")
{
    test_config config;
    config.health_checker->period_msec = {5};
    nom::relay<test_assembly> app(config);
    app.ctx().fail_check = true;
    app.request(nom::event::app_start{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().visits == std::vector<unsigned>{1, 2});
    CHECK(app.ctx().completed_checks == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().exit_code == 1);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("A zero health-check period disables ticks and preserves application lifecycle events")
{
    test_config config;
    config.health_checker->period_msec = {0};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    app.ctx().io.run_for(20ms);
    CHECK(app.ctx().visits.empty());
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 0);
    CHECK_FALSE(app.ctx().exit_code.has_value());
    app.request(nom::event::app_stop{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().visits.empty());
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Assembly owns its configuration independently of the source lifetime")
{
    std::optional<nom::relay<test_assembly>> app;
    {
        test_config config;
        config.health_checker->period_msec = {5};
        app.emplace(config);
        config.health_checker->period_msec = {0};
        CHECK(*app->ctx().cfg().health_checker->period_msec == 5);
    }
    // Starting and rearming the timer read the owned config after the source is gone.
    app->request(nom::event::app_start{});
    app->ctx().io.run_for(500ms);
    CHECK(app->ctx().visits == std::vector<unsigned>{1, 2, 1, 2, 1, 2});
    CHECK(app->ctx().stops == 1);
    CHECK(app->ctx().exit_code == 0);
    CHECK(app->ctx().io.stopped());
}
