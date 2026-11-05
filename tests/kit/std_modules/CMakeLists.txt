# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

# Exercise std imports across interface, implementation and Catch2 consumer boundaries.
nom_lib(${PROJECT_NAME}_std_module_probe IMPORT_STD STATIC
    MODULES sequence.cppm
    SOURCES sequence.cpp
    BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

nom_c2test(std_modules IMPORT_STD
    SOURCES std_modules_test.cxx
    DEPS ${PROJECT_NAME}_std_module_probe
    LABELS kit)

add_test(NAME ${PROJECT_NAME}_std_module_options
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_test.py
        --cmake ${CMAKE_COMMAND}
        --compiler ${CMAKE_CXX_COMPILER}
        --kit ${PROJECT_SOURCE_DIR}/cmake/kit)
set_tests_properties(${PROJECT_NAME}_std_module_options PROPERTIES LABELS kit TIMEOUT 120)
