#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

"""Inspect timestamped text capture; --recover reads complete records before a malformed tail."""

import argparse
import json
from pathlib import Path
import re
import sys


max_record_bytes = 256 * 1024 * 1024


def read_metadata(path):
    path = Path(path)
    metadata_path = path.with_name(path.name.split('.', 1)[0] + '.meta.json')
    metadata = json.loads(metadata_path.read_text(encoding='utf-8'))
    if not isinstance(metadata, dict) or not isinstance(metadata.get('format'), dict):
        raise ValueError(f'{metadata_path}: metadata must contain a format object')
    format_info = metadata['format']
    if format_info.get('name') != 'nom.md_capture.text' or format_info.get('version') != 1:
        raise ValueError(f'{metadata_path}: unsupported capture format/version')
    return metadata


def reject_non_json_constant(value):
    raise ValueError(f'non-JSON constant: {value}')


def read_records(path, recover=False):
    with Path(path).open('rb') as stream:
        while True:
            offset = stream.tell()
            line = stream.readline(max_record_bytes + 1)
            if not line:
                return
            try:
                if len(line) > max_record_bytes:
                    raise ValueError('record exceeds reader limit of 256 MiB')
                if not line.endswith(b'\n'):
                    raise ValueError('unterminated record')
                timestamp, separator, payload = line[:-1].partition(b' ')
                if not separator or not re.fullmatch(rb'-?(0|[1-9][0-9]*)', timestamp):
                    raise ValueError('invalid microsecond timestamp or missing space separator')
                received_us = int(timestamp)
                if not -(2 ** 63) <= received_us < 2 ** 63:
                    raise ValueError('timestamp is outside signed 64-bit range')
                text = payload.decode('utf-8')
                if '\r' in text or '\n' in text:
                    raise ValueError('JSON object must occupy one physical line')
                document = json.loads(text, parse_int=str, parse_float=str, parse_constant=reject_non_json_constant)
                if not isinstance(document, dict):
                    raise ValueError('record payload must be a JSON object')
                yield {'received_us': received_us, 'data': text}
            except (ValueError, UnicodeError) as error:
                if not recover:
                    raise ValueError(f'{path}: byte {offset}: {error}') from error
                print(f'{path}: recovery stopped at byte {offset}: {error}', file=sys.stderr)
                return


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', type=Path, nargs='+')
    parser.add_argument('--recover', action='store_true', help='emit the complete prefix of an unfinished data file')
    args = parser.parse_args()
    try:
        for path in args.files:
            for record in read_records(path, args.recover):
                print(json.dumps({'file': str(path), **record}, ensure_ascii=False))
    except (ValueError, OSError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
