# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

cmake_minimum_required(VERSION 4.4)

include(cmake/kit/std_modules.cmake)

# The version lives only here; releases are tagged v<version> (docs/kit/build.md#versioning).
project(md_capture
    VERSION 0.1.0
    DESCRIPTION "Production market-data capture and symbol discovery applications"
    LANGUAGES CXX)

include(cmake/kit/nom_kit.cmake)
nom_project_options()

if(nom_install)
    include(GNUInstallDirs)
    install(FILES ${PROJECT_SOURCE_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME})
endif()

# asio_comm supplies communication and the nom::core facade through nom_core.
nom_require_submodule(external/asio_comm)
FetchContent_Declare(asio_comm SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/asio_comm)
FetchContent_MakeAvailable(asio_comm)

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
