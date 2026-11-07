# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_c2test(md_logger_ai IMPORT_STD SOURCES md_logger_ai_test.cxx
    DEPS nom::md_logger_ai nlohmann_json::nlohmann_json)
if(TARGET md_capture_md_logger_ai)
    target_link_options(md_capture_md_logger_ai PRIVATE -Wl,--wrap=write)
endif()
