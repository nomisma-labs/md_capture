// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

module;

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 16
// Keep fortified stdio declarations local before importing the partitions.
// GCC 16 otherwise ICEs in nonnull_arg_p while compiling the imported snprintf wrapper.
#include <cstdio>
#endif

export module nom.services;

export import :app_events;
export import :config_owner;
export import :asio_service;
export import :app_control;
export import :md_logger;
export import :md_sink;
