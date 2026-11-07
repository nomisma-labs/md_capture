# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_app(okx_md_capture IMPORT_STD
    SOURCES main.cpp app_events.h feed_events.h asio_service.h compiler_workarounds.h config_owner.h log_config.h app_terminator.h health_checker.h md_sink.h md_subscriber.h
    DEPS nom::base_force_inline nom::md_logger nom::asio_comm nom::assembly nom::spd_logger nom::json_config Boost::headers
    COMPILE_OPTIONS -DNOM_CONFIG_KEY=okx_md_capture)

nom_jcfg_generate(okx_md_capture SOURCE okx_md_capture_jcfg.json BUILD_WITH_APP)
nom_jcfg_test(okx_md_capture dev)
