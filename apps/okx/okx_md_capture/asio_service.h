// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

#pragma once

#include <boost/asio/io_context.hpp>

namespace nom
{
    template <typename Traits> struct asio_service
    {
        struct context_mixin
        {
            // The assembly constructs context services before nodes and destroys them after nodes.
            boost::asio::io_context io;
        };
    };
} // namespace nom
