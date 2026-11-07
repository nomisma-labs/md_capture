// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

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
} // namespace nom::event
