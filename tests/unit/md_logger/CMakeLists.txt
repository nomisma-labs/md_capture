# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

nom_c2test(md_logger IMPORT_STD SOURCES md_logger_test.cxx DEPS nom::md_logger nom::spd_logger)
if(TARGET md_capture_md_logger)
    target_link_options(md_capture_md_logger PRIVATE -Wl,--wrap=write -Wl,--wrap=pthread_mutex_trylock -Wl,--wrap=pthread_mutex_lock)
    set_tests_properties(md_capture_md_logger PROPERTIES TIMEOUT 30)
endif()
