# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_app(okx_md_capture IMPORT_STD
    SOURCES main.cpp compiler_workarounds.h md_subscriber.h
    DEPS nom::services nom::asio_comm nom::core Boost::headers
    COMPILE_OPTIONS -DNOM_CONFIG_KEY=okx_md_capture)

nom_jcfg_generate(okx_md_capture SOURCE okx_md_capture_jcfg.json BUILD_WITH_APP)
nom_jcfg_test(okx_md_capture dev)
