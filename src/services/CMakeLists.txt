# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_lib(services IMPORT_STD
    MODULES services.cppm app_events.cppm config_owner.cppm asio_service.cppm app_control.cppm md_logger.cppm md_sink.cppm
    SOURCES md_logger.cpp
    PUBLIC_DEPS nom::asio_comm nom::core Boost::headers
    PRIVATE_DEPS Threads::Threads)
