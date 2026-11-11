// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

// Application-wide requests: every handler must pass_request the event onward.
// The assembly suppresses these requests only after the last participating node.
namespace nom::event
{
    struct app_start
    {
    };

    struct app_stop
    {
    };

    struct app_health_check
    {
    };

    // Application responses travel back to app_control, which consumes them.
    struct app_finished
    {
    };

    struct app_failed
    {
    };
} // namespace nom::event
