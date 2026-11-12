# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0
#
# One subdirectory per library (docs/kit/howto.md#a-library).

add_subdirectory(md_logger_ai)
add_subdirectory(services)

nom_install_package(TARGETS md_logger_ai services
    DEPENDENCIES asio_comm "nom_core 0.3" Threads nlohmann_json)
