# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_lib(md_logger IMPORT_STD MODULES md_logger.cppm SOURCES md_logger.cpp
    PUBLIC_DEPS nom::config_types nom::json_config PRIVATE_DEPS Threads::Threads nom::spd_logger)
