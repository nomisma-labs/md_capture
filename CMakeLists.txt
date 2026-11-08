# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

cmake_minimum_required(VERSION 4.4)

include(cmake/kit/std_modules.cmake)

# The version lives only here; releases are tagged v<version> (docs/kit/build.md#versioning).
project(md_capture
    VERSION 0.1.0
    DESCRIPTION "Production market-data capture and symbol discovery applications"
    LANGUAGES CXX)

include(cmake/kit/nom_kit.cmake)
nom_project_options()

# Dependencies come from external/ submodules (docs/kit/howto.md#a-dependency). In a superbuild the
# outermost FetchContent_Declare wins, so every repo shares one copy.

# Declare the application's pins before loading dependencies with shared transitive components.
nom_require_submodule(external/base)
FetchContent_Declare(base SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/base)

nom_require_submodule(external/assembly)
FetchContent_Declare(assembly SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/assembly)

nom_require_submodule(external/spd_logger)
FetchContent_Declare(spd_logger SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/spd_logger)

nom_require_submodule(external/asio_comm)
FetchContent_Declare(asio_comm SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/asio_comm)

nom_require_submodule(external/json_config)
FetchContent_Declare(json_config SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/json_config)

FetchContent_MakeAvailable(base assembly spd_logger asio_comm json_config)

# Boost comes from system packages (docs/kit/build.md#dependencies).
nom_find_boost()
find_package(Threads REQUIRED)

# Discover Catch2 before any code directory can register an inline test suite.
if(nom_build_tests)
    enable_testing()
    nom_find_catch2()
endif()

add_subdirectory(src)

if(nom_build_apps)
    add_subdirectory(apps)
endif()

if(nom_build_tests)
    add_subdirectory(tests)
endif()
