# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "md_logger_ai segment publication requires Linux renameat2")
endif()

nom_lib(md_logger_ai IMPORT_STD MODULES md_logger_ai.cppm SOURCES md_logger_ai.cpp
    PRIVATE_DEPS nlohmann_json::nlohmann_json Threads::Threads)
