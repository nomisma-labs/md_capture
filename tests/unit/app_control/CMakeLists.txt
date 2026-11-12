# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_c2test(app_control IMPORT_STD SOURCES app_control_test.cxx app_events_test.cxx config_owner_test.cxx
    DEPS nom::services nom::asio_comm nom::core Boost::headers)
