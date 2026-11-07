# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_c2test(health_checker IMPORT_STD SOURCES health_checker_test.cxx app_events_test.cxx config_owner_test.cxx
    DEPS nom::base_force_inline nom::md_logger nom::asio_comm nom::assembly nom::spd_logger nom::json_config Boost::headers)
