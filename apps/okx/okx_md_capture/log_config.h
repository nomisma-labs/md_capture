// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

#pragma once

#include <json_config/config.h>

import std;
import nom.json_config;
import nom.spd_logger;

// JSON mapping belongs to the application; spd_logger remains format-independent.
namespace nom
{
    NOM_JCONFIG_ENUM(log_level,
                     {{log_level::debug, "debug"},
                      {log_level::info, "info"},
                      {log_level::warn, "warn"},
                      {log_level::error, "error"},
                      {log_level::always, "always"},
                      {log_level::debug, "DEBUG"},
                      {log_level::info, "INFO"},
                      {log_level::warn, "WARN"},
                      {log_level::error, "ERROR"},
                      {log_level::always, "ALWAYS"}})

    NOM_JCONFIG(logfile_config, name, rotation_size)
    NOM_JCONFIG(log_config, level, file)
} // namespace nom
