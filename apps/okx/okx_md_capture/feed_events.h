// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

namespace nom::event
{
    enum class feed_event
    {
        connected,
        completed,
        failed,
        stop_failed
    };
} // namespace nom::event
