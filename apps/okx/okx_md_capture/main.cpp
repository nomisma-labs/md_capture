// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#include "app_control.h"
#include "app_events.h"
#include "asio_service.h"
#include "config_owner.h"
#include "md_sink.h"
#include "md_subscriber.h"

#include <json_config/config.h>

import std;
import nom.core;

using namespace nom::cfg_types;

struct app_config
{
    cfg_defaulted<nom::log_config> log;
    cfg_defaulted<nom::app_control_config> app_control;
    okx::md_subscriber_config md_subscriber;
    nom::md_sink_config md_sink;

    std::expected<void, std::string> validate() const
    {
        if (const auto result = log->validate(); !result)
            return std::unexpected("/log" + result.error());
        if (const auto result = app_control->validate(); !result)
            return std::unexpected("/app_control" + result.error());
        if (const auto result = md_subscriber.validate(); !result)
            return std::unexpected("/md_subscriber" + result.error());
        if (const auto result = md_sink.validate(); !result)
            return std::unexpected("/md_sink" + result.error());
        return {};
    }
};

NOM_JCONFIG(app_config, log, app_control, md_subscriber, md_sink)

struct md_capture_assembly : nom::assembly<md_capture_assembly,
                                           nom::config_owner,
                                           nom::asio_service,
                                           nom::app_control,
                                           nom::md_sink,
                                           okx::md_subscriber>
{
    using config = ::app_config;

    struct custom_traits : nom::event_suppressors
    {
        using suppressed_requests =
            nom::type_list<nom::event::app_start, nom::event::app_stop, nom::event::app_health_check>;
    };
};

struct okx_md_capture : nom::relay<md_capture_assembly>
{
    explicit okx_md_capture(const app_config& config) : nom::relay<md_capture_assembly>(config)
    {
    }

    nom::app_status run()
    {
        if (ctx().io.stopped())
            ctx().io.restart();
        request(nom::event::app_start{});
        // Stop handlers close transport and drain the writer; keep nodes alive
        // until the remaining I/O callbacks have drained.
        ctx().io.run();
        return ctx().status();
    }
};

int main(int argc, char** argv)
{
    try
    {
        const auto arguments = nom::jcfg::parse_config_arguments(argc, argv, NOM_CONFIG_KEY_VALUE);
        if (arguments.help)
        {
            std::cout << "Usage: okx_md_capture [options]\n"
                         "Capture OKX Spot messages using an asynchronous MD "
                         "logger.\n";
            nom::jcfg::print_config_help(std::cout);
            return 0;
        }
        if (!arguments.remaining_arguments.empty())
            throw std::invalid_argument("Unknown arguments; use --help");
        if (arguments.options.check_config)
        {
            nom::jcfg::print_config_check(
                std::cout, nom::jcfg::load_config<app_config>(arguments.options), arguments.options.check_mode);
            return 0;
        }
        const auto load_config = [&]
        {
            const auto config = nom::jcfg::load_config<app_config>(arguments.options);
            nom::init_logger(nom::main_logger, *config.value.log);
            return config.value;
        };
        okx_md_capture app(load_config());
        const auto result = app.run();
        nom::flush_logger(nom::main_logger);
        return result == nom::app_status::success ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
