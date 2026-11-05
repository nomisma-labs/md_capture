#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Check IMPORT_STD diagnostics and experimental-warning policy through real presets."""

import argparse
import json
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory


def run(source: Path, *command: str, success: bool = True) -> str:
    result = subprocess.run(command, cwd=source, capture_output=True, text=True)
    output = result.stdout + result.stderr
    if (result.returncode == 0) != success:
        raise RuntimeError(f"{' '.join(command)}\n{output}")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cmake', 'compiler', 'kit'):
        parser.add_argument(f'--{name}', required=True)
    args = parser.parse_args()
    kit = Path(args.kit).resolve().as_posix()
    with TemporaryDirectory(prefix='nomisma-import-std-') as temporary:
        source = Path(temporary)
        (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 4.4)
include("${NOM_KIT_PATH}/std_modules.cmake")
project(import_std_options VERSION 1.0.0 LANGUAGES CXX)
include("${NOM_KIT_PATH}/nom_kit.cmake")
nom_project_options()
if(TEST_INTERFACE)
    nom_lib(invalid IMPORT_STD)
endif()
message(AUTHOR_WARNING "author warning remains enabled")
nom_app(import_std_app IMPORT_STD SOURCES main.cpp)
''')
        (source / 'main.cpp').write_text('''import std;
int main() { return std::vector<int>{1, 2, 3}.size() == 3 ? 0 : 1; }
''')
        (source / 'CMakePresets.json').write_text(json.dumps({
            'version': 12,
            'include': [f'{kit}/presets.json'],
            'configurePresets': [
                {'name': 'quiet', 'inherits': 'base', 'cacheVariables': {
                    'CMAKE_CXX_COMPILER': args.compiler, 'NOM_KIT_PATH': kit}},
                {'name': 'verbose', 'inherits': 'quiet',
                 'warnings': {'experimental': True}},
            ],
        }))
        quiet = run(source, args.cmake, '--preset', 'quiet')
        assert 'author warning remains enabled' in quiet, quiet
        assert 'CMake Warning (experimental)' not in quiet, quiet
        build = source / 'build/quiet'
        run(source, args.cmake, '--build', str(build), '--parallel', '2')
        run(source, str(build / 'bin/import_std_app'))
        verbose = run(source, args.cmake, '--preset', 'verbose')
        assert 'CMake Warning (experimental)' in verbose, verbose
        assert 'author warning remains enabled' in verbose, verbose
        print('PASS: IMPORT_STD app builds; presets suppress only experimental warnings')

        invalid = run(source, args.cmake, '--preset', 'quiet',
                      '-B', str(source / 'build/invalid'), '-DTEST_INTERFACE=ON', success=False)
        assert 'IMPORT_STD requires SOURCES or MODULES' in invalid, invalid
        print('PASS: interface libraries reject IMPORT_STD with an actionable diagnostic')


if __name__ == '__main__':
    main()
