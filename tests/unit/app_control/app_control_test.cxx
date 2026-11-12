// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include <json_config/config.h>
#include <spd_logger/logging.h>

#include <csignal>

#include <catch2/catch_test_macros.hpp>

import std;
import nom.core;
import nom.services;

using namespace std::chrono_literals;

namespace
{
    struct test_config
    {
        nom::cfg_defaulted<nom::app_control_config> app_control;

        std::expected<void, std::string> validate() const
        {
            return app_control->validate();
        }
    };

    // This upstream probe must never receive responses accepted by app_control.
    template <typename Traits> struct response_probe
    {
        struct context_mixin
        {
            unsigned finished_responses{};
            unsigned failed_responses{};
        };

        void on_response(nom::event::app_finished)
        {
            ++nom::ctx(this).finished_responses;
        }

        void on_response(nom::event::app_failed)
        {
            ++nom::ctx(this).failed_responses;
        }
    };

    template <typename Traits> struct first_probe
    {
        struct context_mixin
        {
            std::vector<unsigned> visits;
            std::vector<std::string_view> lifecycle;
            std::vector<nom::app_status> lifecycle_statuses;
            unsigned starts{};
            unsigned stops{};
            unsigned checks{};
            bool finish_during_start{};
            bool fail_during_start{};
            bool fail_check{};
            bool fail_stop{};
            std::function<void()> start_action;
            std::function<void()> stop_action;
        };

        void on_request(nom::event::app_start event)
        {
            auto& context = nom::ctx(this);
            ++context.starts;
            context.checks = 0;
            context.lifecycle.push_back("first_start");
            context.lifecycle_statuses.push_back(context.status());
            if (context.start_action)
                context.start_action();
            if (context.finish_during_start)
                nom::relay<Traits>::pass_response(this, nom::event::app_finished{});
            if (context.fail_during_start)
                nom::relay<Traits>::pass_response(this, nom::event::app_failed{});
            nom::relay<Traits>::pass_request(this, event);
        }

        void on_request(nom::event::app_stop event)
        {
            auto& context = nom::ctx(this);
            ++context.stops;
            context.lifecycle.push_back("first_stop");
            context.lifecycle_statuses.push_back(context.status());
            if (context.stop_action)
                context.stop_action();
            if (context.fail_stop)
                nom::relay<Traits>::pass_response(this, nom::event::app_failed{});
            nom::relay<Traits>::pass_request(this, event);
        }

        void on_request(nom::event::app_health_check event)
        {
            auto& context = nom::ctx(this);
            context.visits.push_back(1);
            if (context.fail_check)
                nom::relay<Traits>::pass_response(this, nom::event::app_failed{});
            nom::relay<Traits>::pass_request(this, event);
        }
    };

    template <typename Traits> struct last_probe
    {
        void on_request(nom::event::app_start event)
        {
            nom::ctx(this).lifecycle.push_back("last_start");
            nom::ctx(this).lifecycle_statuses.push_back(nom::ctx(this).status());
            nom::relay<Traits>::pass_request(this, event);
        }

        void on_request(nom::event::app_stop event)
        {
            nom::ctx(this).lifecycle.push_back("last_stop");
            nom::ctx(this).lifecycle_statuses.push_back(nom::ctx(this).status());
            nom::relay<Traits>::pass_request(this, event);
        }

        void on_request(nom::event::app_health_check event)
        {
            auto& context = nom::ctx(this);
            context.visits.push_back(2);
            if (++context.checks == 3)
                nom::relay<Traits>::pass_response(this, nom::event::app_finished{});
            nom::relay<Traits>::pass_request(this, event);
        }
    };

    struct test_assembly : nom::assembly<test_assembly,
                                         nom::config_owner,
                                         nom::asio_service,
                                         response_probe,
                                         nom::app_control,
                                         first_probe,
                                         last_probe>
    {
        using config = test_config;
        struct custom_traits : nom::event_suppressors
        {
            using suppressed_requests = nom::type_list<nom::event::app_start,
                                                       nom::event::app_stop,
                                                       nom::event::app_health_check,
                                                       nom::event::app_finished,
                                                       nom::event::app_failed>;
        };
    };

    struct temporary_log
    {
        std::filesystem::path directory =
            std::filesystem::temp_directory_path() /
            std::format("nom_app_control_{}", std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::path file = directory / "app.log";

        temporary_log()
        {
            std::filesystem::create_directory(directory);
            nom::log_config config;
            config.file = nom::logfile_config{file.string(), {}};
            nom::init_logger(nom::main_logger, config);
        }

        ~temporary_log()
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }

        std::string read() const
        {
            std::ifstream input(file);
            return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        }
    };
} // namespace

TEST_CASE("App control broadcasts recurring health checks and stops after completion")
{
    test_config config;
    config.app_control->health_check_period_msec = {5};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().visits == std::vector<unsigned>{1, 2, 1, 2, 1, 2});
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().status() == nom::app_status::success);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("A failed health check cancels the next tick while forwarding the current check")
{
    test_config config;
    config.app_control->health_check_period_msec = {5};
    nom::relay<test_assembly> app(config);
    app.ctx().fail_check = true;
    app.request(nom::event::app_start{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().visits == std::vector<unsigned>{1, 2});
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().status() == nom::app_status::failure);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Disabled health checks leave signal handling active and explicit stops propagate")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    app.ctx().io.run_for(20ms);
    CHECK(app.ctx().visits.empty());
    CHECK(app.ctx().status() == nom::app_status::running);
    CHECK_FALSE(app.ctx().io.stopped());
    app.request(nom::event::app_stop{});
    app.request(nom::event::app_stop{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("The exit timer starts with the application without any connection notification")
{
    nom::init_logger(nom::main_logger);
    test_config config;
    config.app_control->health_check_period_msec = {0};
    config.app_control->exit_after_sec = {0};
    SECTION("Zero limit still forwards start before stopping")
    {
    }
    SECTION("Positive limit measures application running time")
    {
        config.app_control->exit_after_sec = {1};
    }
    nom::relay<test_assembly> app(config);
    CHECK(app.ctx().status() == nom::app_status::init);
    app.request(nom::event::app_start{});
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 0);
    CHECK(app.ctx().status() == nom::app_status::running);
    if (*config.app_control->exit_after_sec == 1)
    {
        app.ctx().io.run_for(500ms);
        REQUIRE(app.ctx().status() == nom::app_status::running);
        app.request(nom::event::app_start{});
        CHECK(app.ctx().starts == 1);
        // The original deadline expires in this window; a reset deadline would not.
        app.ctx().io.run_for(750ms);
    }
    else
        app.ctx().io.run_for(500ms);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().status() == nom::app_status::success);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Normal stops succeed and downstream failures fail")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    auto expected = nom::app_status::success;
    SECTION("Completed feed succeeds")
    {
        app.respond(nom::event::app_finished{});
    }
    SECTION("Failed feed fails")
    {
        app.respond(nom::event::app_failed{});
        expected = nom::app_status::failure;
    }
    SECTION("An explicit stop succeeds without a completion report")
    {
        app.request(nom::event::app_stop{});
    }
    SECTION("Shutdown failure overrides successful completion without another stop")
    {
        app.ctx().fail_stop = true;
        app.respond(nom::event::app_finished{});
        expected = nom::app_status::failure;
    }
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().status() == expected);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Late failure reports override success and later completion cannot clear failure")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    app.respond(nom::event::app_finished{});
    REQUIRE(app.ctx().status() == nom::app_status::success);
    REQUIRE(app.ctx().stops == 1);
    app.respond(nom::event::app_failed{});
    REQUIRE(app.ctx().status() == nom::app_status::failure);
    app.respond(nom::event::app_finished{});
    app.respond(nom::event::app_failed{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().status() == nom::app_status::failure);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Repeated SIGQUIT flushes new log records without stopping or losing shutdown signals")
{
    temporary_log log;
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    for (unsigned index = 1; index <= 2; ++index)
    {
        const auto message = std::format("flush marker {}", index);
        LINFO("{}", message);
        REQUIRE(std::raise(SIGQUIT) == 0);
        app.ctx().io.run_for(20ms);
        CHECK(log.read().contains(message));
        CHECK(app.ctx().status() == nom::app_status::running);
        CHECK(app.ctx().stops == 0);
        CHECK_FALSE(app.ctx().io.stopped());
    }
    int signal = SIGINT;
    auto expected = nom::app_status::success;
    SECTION("Ctrl-C stops successfully")
    {
    }
    SECTION("SIGTERM stops with failure")
    {
        signal = SIGTERM;
        expected = nom::app_status::failure;
    }
    REQUIRE(std::raise(signal) == 0);
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().status() == expected);
    CHECK(app.ctx().finished_responses == 0);
    CHECK(app.ctx().failed_responses == 0);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Duplicate stops are ignored and pending health checks are cancelled")
{
    test_config config;
    config.app_control->health_check_period_msec = {5};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    app.request(nom::event::app_stop{});
    app.request(nom::event::app_stop{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().visits.empty());
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Assembly owns its configuration independently of the source lifetime")
{
    std::optional<nom::relay<test_assembly>> app;
    {
        test_config config;
        config.app_control->health_check_period_msec = {5};
        app.emplace(config);
        config.app_control->health_check_period_msec = {0};
        CHECK(*app->ctx().cfg().app_control->health_check_period_msec == 5);
    }
    // Starting and rearming the timer read the owned config after the source is gone.
    app->request(nom::event::app_start{});
    app->ctx().io.run_for(500ms);
    CHECK(app->ctx().visits == std::vector<unsigned>{1, 2, 1, 2, 1, 2});
    CHECK(app->ctx().stops == 1);
    CHECK(app->ctx().status() == nom::app_status::success);
    CHECK(app->ctx().io.stopped());
}

TEST_CASE("Only accepted lifecycle transitions reach downstream nodes")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    REQUIRE(app.ctx().status() == nom::app_status::init);
    app.request(nom::event::app_stop{});
    CHECK(app.ctx().stops == 0);
    CHECK(app.ctx().status() == nom::app_status::init);
    for (unsigned run = 1; run <= 2; ++run)
    {
        app.ctx().io.restart();
        app.request(nom::event::app_start{});
        app.request(nom::event::app_start{});
        CHECK(app.ctx().starts == run);
        CHECK(app.ctx().status() == nom::app_status::running);
        app.request(nom::event::app_stop{});
        app.request(nom::event::app_stop{});
        app.ctx().io.run_for(500ms);
        CHECK(app.ctx().stops == run);
        CHECK(app.ctx().status() == nom::app_status::success);
        CHECK(app.ctx().io.stopped());
    }
}

TEST_CASE("App control consumes finished and failed responses before upstream observers")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    // The configured request suppressor also accepts these types in the request direction.
    app.request(nom::event::app_finished{});
    app.request(nom::event::app_failed{});
    CHECK(app.ctx().status() == nom::app_status::running);
    app.respond(nom::event::app_finished{});
    CHECK(app.ctx().finished_responses == 0);
    CHECK(app.ctx().status() == nom::app_status::success);
    app.respond(nom::event::app_failed{});
    CHECK(app.ctx().failed_responses == 0);
    CHECK(app.ctx().status() == nom::app_status::failure);
    app.respond(nom::event::app_finished{});
    app.respond(nom::event::app_failed{});
    CHECK(app.ctx().status() == nom::app_status::failure);
    CHECK(app.ctx().finished_responses == 0);
    CHECK(app.ctx().failed_responses == 0);
    CHECK(app.ctx().stops == 1);
    app.ctx().io.run_for(500ms);
}

TEST_CASE("A new run resets failure and rearms health checks and signal waiting")
{
    nom::init_logger(nom::main_logger);
    test_config config;
    config.app_control->health_check_period_msec = {5};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    app.respond(nom::event::app_failed{});
    app.ctx().io.run_for(500ms);
    REQUIRE(app.ctx().status() == nom::app_status::failure);
    app.ctx().io.restart();
    app.request(nom::event::app_start{});
    REQUIRE(app.ctx().status() == nom::app_status::running);
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().visits == std::vector<unsigned>{1, 2, 1, 2, 1, 2});
    CHECK(app.ctx().status() == nom::app_status::success);
    app.ctx().io.restart();
    app.request(nom::event::app_start{});
    REQUIRE(std::raise(SIGTERM) == 0);
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().starts == 3);
    CHECK(app.ctx().stops == 3);
    CHECK(app.ctx().status() == nom::app_status::failure);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Restart before cancelled callbacks drain keeps the new timer and signal wait active")
{
    nom::init_logger(nom::main_logger);
    test_config config;
    config.app_control->health_check_period_msec = {0};
    SECTION("Runtime timer is rearmed")
    {
        config.app_control->exit_after_sec = {0};
    }
    SECTION("Signal wait is rearmed")
    {
    }
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    app.request(nom::event::app_stop{});
    app.request(nom::event::app_start{});
    REQUIRE(app.ctx().status() == nom::app_status::running);
    if (!config.app_control->exit_after_sec)
        REQUIRE(std::raise(SIGINT) == 0);
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().starts == 2);
    CHECK(app.ctx().stops == 2);
    CHECK(app.ctx().status() == nom::app_status::success);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("A reentrant start cannot interrupt stop propagation")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    app.ctx().stop_action = [&app] { app.request(nom::event::app_start{}); };
    app.request(nom::event::app_start{});
    app.request(nom::event::app_stop{});
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().status() == nom::app_status::success);
    app.ctx().stop_action = {};
    app.request(nom::event::app_start{});
    CHECK(app.ctx().starts == 2);
    CHECK(app.ctx().status() == nom::app_status::running);
    app.request(nom::event::app_stop{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().stops == 2);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Finalizing start preserves a synchronous completion or failure")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    auto expected = nom::app_status::success;
    SECTION("A component finishes during start")
    {
        app.ctx().finish_during_start = true;
    }
    SECTION("A component fails during start")
    {
        app.ctx().fail_during_start = true;
        expected = nom::app_status::failure;
    }
    app.request(nom::event::app_start{});
    CHECK(app.ctx().lifecycle == std::vector<std::string_view>{"first_start", "first_stop", "last_stop", "last_start"});
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().status() == expected);
    CHECK(app.ctx().finished_responses == 0);
    CHECK(app.ctx().failed_responses == 0);
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Status is read-only through the context and reflects lifecycle delivery")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    const auto& context = std::as_const(app).ctx();
    static_assert(std::same_as<decltype(context.status()), nom::app_status>);
    static_assert(noexcept(context.status()));
    CHECK(context.status() == nom::app_status::init);
    app.ctx().start_action = [&app]
    {
        CHECK(app.ctx().status() == nom::app_status::starting);
        app.request(nom::event::app_start{});
    };
    app.ctx().stop_action = [&app]
    {
        CHECK(app.ctx().status() == nom::app_status::stopping);
        app.request(nom::event::app_start{});
        app.request(nom::event::app_stop{});
    };
    app.request(nom::event::app_start{});
    CHECK(context.status() == nom::app_status::running);
    app.request(nom::event::app_stop{});
    CHECK(context.status() == nom::app_status::success);
    CHECK(context.starts == 1);
    CHECK(context.stops == 1);
    CHECK(context.lifecycle_statuses == std::vector<nom::app_status>{nom::app_status::starting,
                                                                     nom::app_status::starting,
                                                                     nom::app_status::stopping,
                                                                     nom::app_status::stopping});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("A stop during startup is immediate and finalization preserves its result")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    auto expected = nom::app_status::success;
    bool fail_before_stop{};
    bool fail_after_stop{};
    SECTION("Normal stop during startup")
    {
    }
    SECTION("Failure before stop and completion")
    {
        fail_before_stop = true;
        expected = nom::app_status::failure;
    }
    SECTION("Failure after stop and completion")
    {
        fail_after_stop = true;
        expected = nom::app_status::failure;
    }
    app.ctx().start_action = [&]
    {
        if (fail_before_stop)
            app.respond(nom::event::app_failed{});
        app.request(nom::event::app_stop{});
        app.respond(nom::event::app_finished{});
        if (fail_after_stop)
            app.respond(nom::event::app_failed{});
        app.request(nom::event::app_stop{});
        CHECK(app.ctx().status() == expected);
        CHECK(app.ctx().stops == 1);
    };
    app.request(nom::event::app_start{});
    CHECK(app.ctx().status() == expected);
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().lifecycle == std::vector<std::string_view>{"first_start", "first_stop", "last_stop", "last_start"});
    CHECK(app.ctx().lifecycle_statuses ==
          std::vector<nom::app_status>{
              nom::app_status::starting, nom::app_status::stopping, nom::app_status::stopping, expected});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Finalizing stop preserves failure selected by a nested response")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    auto expected = nom::app_status::success;
    auto last_stop_status = nom::app_status::stopping;
    SECTION("Successful completion")
    {
        app.respond(nom::event::app_finished{});
    }
    SECTION("Failed completion")
    {
        expected = nom::app_status::failure;
        app.respond(nom::event::app_failed{});
    }
    SECTION("Failure during stop followed by successful notifications")
    {
        expected = nom::app_status::failure;
        last_stop_status = nom::app_status::failure;
        app.ctx().stop_action = [&app]
        {
            app.respond(nom::event::app_failed{});
            app.respond(nom::event::app_finished{});
            CHECK(app.ctx().status() == nom::app_status::failure);
        };
        app.respond(nom::event::app_finished{});
    }
    CHECK(app.ctx().finished_responses == 0);
    CHECK(app.ctx().failed_responses == 0);
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().status() == expected);
    CHECK(app.ctx().lifecycle_statuses.back() == last_stop_status);
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Responses before the first start do not broadcast a stop")
{
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    app.respond(nom::event::app_finished{});
    CHECK(app.ctx().status() == nom::app_status::init);
    app.respond(nom::event::app_failed{});
    CHECK(app.ctx().status() == nom::app_status::failure);
    CHECK(app.ctx().starts == 0);
    CHECK(app.ctx().stops == 0);
    CHECK(app.ctx().finished_responses == 0);
    CHECK(app.ctx().failed_responses == 0);
    app.request(nom::event::app_start{});
    CHECK(app.ctx().status() == nom::app_status::running);
    app.request(nom::event::app_stop{});
    CHECK(app.ctx().status() == nom::app_status::success);
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Health checks are armed only when start finalization enters running")
{
    test_config config;
    config.app_control->health_check_period_msec = {5};
    nom::relay<test_assembly> app(config);
    app.ctx().start_action = [&app]
    {
        // Signal waiting is active, but health checks are not armed during start delivery.
        CHECK(app.ctx().io.run_for(15ms) == 0);
        CHECK(app.ctx().checks == 0);
        CHECK(app.ctx().status() == nom::app_status::starting);
    };
    app.request(nom::event::app_start{});
    REQUIRE(app.ctx().status() == nom::app_status::running);
    app.ctx().io.restart();
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().checks == 3);
    CHECK(app.ctx().status() == nom::app_status::success);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("The runtime timer includes start delivery and finalization preserves its stop")
{
    nom::init_logger(nom::main_logger);
    test_config config;
    config.app_control->exit_after_sec = {0};
    config.app_control->health_check_period_msec = {5};
    nom::relay<test_assembly> app(config);
    app.ctx().start_action = [&app]
    {
        app.ctx().io.run_for(500ms);
        CHECK(app.ctx().status() == nom::app_status::success);
        CHECK(app.ctx().stops == 1);
    };
    app.request(nom::event::app_start{});
    CHECK(app.ctx().status() == nom::app_status::success);
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 1);
    app.ctx().io.restart();
    CHECK(app.ctx().io.run_for(20ms) == 0);
    CHECK(app.ctx().checks == 0);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("Shutdown signals during starting preserve the result at start finalization")
{
    nom::init_logger(nom::main_logger);
    test_config config;
    config.app_control->health_check_period_msec = {5};
    nom::relay<test_assembly> app(config);
    int signal = SIGINT;
    auto expected = nom::app_status::success;
    SECTION("SIGINT stops startup successfully")
    {
    }
    SECTION("SIGTERM stops startup with failure")
    {
        signal = SIGTERM;
        expected = nom::app_status::failure;
    }
    app.ctx().start_action = [&]
    {
        REQUIRE(app.ctx().status() == nom::app_status::starting);
        REQUIRE(std::raise(signal) == 0);
        app.ctx().io.run_for(500ms);
        CHECK(app.ctx().status() == expected);
        CHECK(app.ctx().stops == 1);
    };
    app.request(nom::event::app_start{});
    CHECK(app.ctx().status() == expected);
    CHECK(app.ctx().starts == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().checks == 0);
    CHECK(app.ctx().finished_responses == 0);
    CHECK(app.ctx().failed_responses == 0);
    app.ctx().io.restart();
    CHECK(app.ctx().io.run_for(20ms) == 0);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("SIGQUIT flushes logs during starting and remains active after finalization")
{
    temporary_log log;
    test_config config;
    config.app_control->health_check_period_msec = {0};
    nom::relay<test_assembly> app(config);
    app.ctx().start_action = [&]
    {
        for (unsigned index = 1; index <= 2; ++index)
        {
            const auto message = std::format("startup flush marker {}", index);
            LINFO("{}", message);
            REQUIRE(std::raise(SIGQUIT) == 0);
            app.ctx().io.run_for(20ms);
            CHECK(log.read().contains(message));
            CHECK(app.ctx().status() == nom::app_status::starting);
            CHECK(app.ctx().stops == 0);
        }
    };
    app.request(nom::event::app_start{});
    REQUIRE(app.ctx().status() == nom::app_status::running);
    LINFO("flush after startup");
    REQUIRE(std::raise(SIGQUIT) == 0);
    app.ctx().io.run_for(20ms);
    CHECK(log.read().contains("flush after startup"));
    CHECK(app.ctx().stops == 0);
    app.request(nom::event::app_stop{});
    app.ctx().io.run_for(500ms);
    CHECK(app.ctx().status() == nom::app_status::success);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().io.stopped());
}
