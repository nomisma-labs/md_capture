# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
#
# One subdirectory per library (docs/kit/howto.md#a-library).

add_subdirectory(md_logger)
add_subdirectory(md_logger_ai)

nom_install_package(TARGETS md_logger md_logger_ai DEPENDENCIES "base 0.4" "json_config 0.6" "spd_logger 0.8" Threads nlohmann_json)
