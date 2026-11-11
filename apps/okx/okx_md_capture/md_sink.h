// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

#include "app_events.h"
#include "compiler_workarounds.h"

#include <json_config/config.h>
#include <spd_logger/logging.h>

import std;
import nom.core;
import nom.md_logger;

namespace nom
{
    struct md_sink_config
    {
        nom::md_logger_config md_logger{};

        std::expected<void, std::string> validate() const;
    };

    NOM_JCONFIG(md_sink_config, md_logger)

    template <typename Traits> class md_sink
    {
        using relay = nom::relay<Traits>;

    public:
        void on_request(nom::event::app_start event)
        {
            // Each run gets a fresh worker; the configured file remains append-only.
            md_logger_.emplace(cfg().md_logger);
            LINFO("Capture file: {}", md_logger_->file());
            relay::pass_request(this, event);
        }

        void on_request(nom::event::app_stop event)
        {
            // Stop the producer before draining its accepted messages.
            relay::pass_request(this, event);
            if (!md_logger_)
                return;
            if (!md_logger_->stop())
            {
                LERROR("Capture finalization failed: {}", md_logger_->error().value_or("Unknown storage error"));
                relay::pass_response(this, nom::event::app_failed{});
            }
            const auto counts = md_logger_->stats();
            LINFO("Capture stopped: queued={}, flushed={}, bytes={}",
                  counts.queued_messages,
                  counts.flushed_messages,
                  counts.flushed_bytes);
        }

        void on_request(nom::event::app_health_check event)
        {
            if (md_logger_)
            {
                if (const auto failure = md_logger_->error())
                {
                    LERROR("MD logger failed: {}", *failure);
                    relay::pass_response(this, nom::event::app_failed{});
                }
            }
            relay::pass_request(this, event);
        }

        void on_response(const nom::event::market_data& event)
        {
            if (md_logger_ && md_logger_->log(event))
                return;
            LERROR("MD logger is stopped or failed; stopping capture");
            relay::pass_response(this, nom::event::app_failed{});
        }

    private:
        const md_sink_config& cfg() const noexcept
        {
            return nom::ctx(this).cfg().md_sink;
        }
        std::optional<nom::md_logger> md_logger_;
    };

    inline std::expected<void, std::string> md_sink_config::validate() const
    {
        if (const auto result = md_logger.validate(); !result)
            return std::unexpected("/md_logger: " + result.error());
        return {};
    }
} // namespace nom
