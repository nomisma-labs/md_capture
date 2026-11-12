// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

module;

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/system/error_code.hpp>

export module nom.services:asio_service;

// Share Asio types through this partition. Including its headers in another partition
// makes GCC 16 emit duplicate query/require/prefer objects in unoptimized builds.
namespace nom::detail
{
    using asio_signal_set = boost::asio::signal_set;
    using asio_error_code = boost::system::error_code;
} // namespace nom::detail

export namespace nom
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
