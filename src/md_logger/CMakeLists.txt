# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

nom_lib(md_logger IMPORT_STD MODULES md_logger.cppm SOURCES md_logger.cpp
    PUBLIC_DEPS nom::core PRIVATE_DEPS Threads::Threads)
