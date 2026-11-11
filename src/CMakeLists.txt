# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0
#
# One subdirectory per library (docs/kit/howto.md#a-library).

add_subdirectory(md_logger)
add_subdirectory(md_logger_ai)

nom_install_package(TARGETS md_logger md_logger_ai DEPENDENCIES "nom_core 0.3" Threads nlohmann_json)
