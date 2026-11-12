// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include <catch2/catch_test_macros.hpp>
#include <json_config/config.h>

#include "../../../apps/okx/okx_md_capture/md_subscriber.h"

import std;
import nom.core;
import nom.services;

using namespace std::chrono_literals;

namespace
{
    struct test_config
    {
        nom::cfg_defaulted<nom::app_control_config> app_control;
        nom::md_sink_config md_sink;
        okx::md_subscriber_config md_subscriber;

        std::expected<void, std::string> validate() const
        {
            if (const auto result = app_control->validate(); !result)
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
                                         nom::app_control,
                                         nom::md_sink,
                                         okx::md_subscriber,
                                         trailing_probe>
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

    test_config make_config()
    {
        test_config config;
        config.app_control->health_check_period_msec = {0};
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
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().status() == nom::app_status::success);
    CHECK(app.ctx().io.stopped());
}

TEST_CASE("A failed sink forwards health checks without generating repeated stops")
{
    nom::init_logger(nom::main_logger);
    const auto config = make_config();
    nom::relay<test_assembly> app(config);
    app.request(nom::event::app_start{});
    app.respond(nom::event::market_data{std::chrono::system_clock::now(), R"({"price":"1"})"});
    // Draining the writer exposes the storage failure deterministically.
    app.request(nom::event::app_stop{});
    REQUIRE(app.ctx().status() == nom::app_status::failure);
    REQUIRE(app.ctx().stops == 1);
    app.request(nom::event::app_health_check{});
    CHECK(app.ctx().checks == 1);
    CHECK(app.ctx().stops == 1);
    CHECK(app.ctx().status() == nom::app_status::failure);
}

TEST_CASE("Capture nodes restart and append data from each accepted run")
{
    nom::init_logger(nom::main_logger);
    const auto directory =
        std::filesystem::temp_directory_path() /
        std::format("nom_capture_restart_{}", std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = directory / "capture.txt";
    auto config = make_config();
    config.md_sink.md_logger.file = path.string();
    {
        nom::relay<test_assembly> app(config);
        for (unsigned run = 1; run <= 2; ++run)
        {
            app.ctx().io.restart();
            app.request(nom::event::app_start{});
            app.request(nom::event::app_start{});
            const auto payload = std::format("{{\"run\":{}}}", run);
            app.respond(nom::event::market_data{std::chrono::system_clock::time_point{std::chrono::microseconds{run}},
                                                payload});
            app.request(nom::event::app_health_check{});
            // Cancel connection work before running the context: no server is required.
            app.request(nom::event::app_stop{});
            app.request(nom::event::app_stop{});
            app.ctx().io.run_for(500ms);
            REQUIRE(app.ctx().status() == nom::app_status::success);
            CHECK(app.ctx().starts == run);
            CHECK(app.ctx().stops == run);
            CHECK(app.ctx().checks == run);
            CHECK(app.ctx().io.stopped());
        }
    }
    std::ifstream input(path);
    const std::string contents{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    CHECK(contents == "1 {\"run\":1}\n2 {\"run\":2}\n");
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}
