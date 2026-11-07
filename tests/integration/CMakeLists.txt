# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

if(nom_build_tests AND TARGET okx_md_capture)
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
    find_program(openssl_executable openssl REQUIRED)
    add_test(NAME ${PROJECT_NAME}_okx_capture
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/okx_capture_test.py
            --client $<TARGET_FILE:okx_md_capture> --openssl ${openssl_executable}
            --reader ${PROJECT_SOURCE_DIR}/tools/md_capture/read_capture.py)
    set_tests_properties(${PROJECT_NAME}_okx_capture PROPERTIES LABELS integration TIMEOUT 60)
endif()
