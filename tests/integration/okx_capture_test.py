#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Exercise JSON configuration, TLS capture, shutdown and readable archive output locally."""

import argparse
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import signal
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time


def exact(connection, count):
    data = bytearray()
    while len(data) < count:
        chunk = connection.recv(count - len(data))
        if not chunk:
            raise EOFError('connection closed')
        data.extend(chunk)
    return bytes(data)


def receive(connection):
    first, second = exact(connection, 2)
    assert second & 128
    size = second & 127
    if size == 126:
        size, = struct.unpack('!H', exact(connection, 2))
    elif size == 127:
        size, = struct.unpack('!Q', exact(connection, 8))
    assert size < 65536
    mask = exact(connection, 4)
    return first & 15, bytes(value ^ mask[index % 4] for index, value in enumerate(exact(connection, size)))


def send(connection, payload, opcode=1):
    prefix = bytes([128 | opcode])
    prefix += bytes([len(payload)]) if len(payload) < 126 else b'\x7e' + struct.pack('!H', len(payload))
    connection.sendall(prefix + payload)


def upgrade(connection):
    request = bytearray()
    while not request.endswith(b'\r\n\r\n'):
        request.extend(exact(connection, 1))
        assert len(request) < 16384
    lines = request.decode().split('\r\n')
    assert lines[0] == 'GET /feed HTTP/1.1'
    headers = dict(line.split(': ', 1) for line in lines[1:] if ': ' in line)
    key = next(value for name, value in headers.items() if name.lower() == 'sec-websocket-key')
    accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
    connection.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                       b'Sec-WebSocket-Accept: ' + accept + b'\r\n\r\n')


def run_case(client, root, context, certificate, reader, case, reader_path):
    output = root / case / 'md.log'
    config = root / f'{case}.json'
    payloads = [b'{"event":"subscribe","arg":{"channel":"trades","instId":"BTC-USDT"}}',
                b'{"arg":{"instId":"BTC-USDT"},\r\n"data":[{\n"px":"1"}]}',
                b'{"arg":{"instId":"ETH-USDT"},"data":[{"px":"2"}]}', b'{"note":"backslash \\\\ and escaped newline \\n"}', '{"note":"café"}'.encode()]
    later_payload = b'{"arg":{"instId":"BTC-USDT"},"data":[{"px":"3"}]}'
    expected_payloads = [payload.replace(b'\r', b' ').replace(b'\n', b' ') for payload in payloads]
    errors = []
    ready = threading.Event()
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(8)
        port = listener.getsockname()[1]
        app_log = root / 'capture_app.log'
        settings = {
            'md_subscriber': {
                'connection': {'endpoint': f'wss://localhost:{port}/feed', 'heartbeat_sec': 1},
                'subscription': {'symbols': ['btc-usdt', 'ETH-USDT'], 'channels': ['trades']}},
            'exit_after_sec': 0 if case == 'zero' else 1 if case == 'flush_error' else 3,
            'health_checker': {'period_msec': 50},
            'md_sink': {'md_logger': {'file': str(root / 'unused_output'),
                                      'flush_ms': 60000 if case in ('signal', 'flush_error') else 10}}}
        if case == 'capture':
            settings['log'] = {'file': {'name': str(app_log)}}
        elif case == 'zero':
            settings['log'] = {'level': 'error'}
        config.write_text(json.dumps(settings))
        if case in ('storage_error', 'flush_error'):
            output.mkdir(parents=True)  # An output directory makes open-for-append fail.
        elif case == 'capture':
            output.parent.mkdir(parents=True)
            output.write_bytes(b'1 {}\n')

        def serve():
            try:
                raw, _ = listener.accept()
                with raw:
                    raw.settimeout(8)
                    with context.wrap_socket(raw, server_side=True) as connection:
                        upgrade(connection)
                        if case == 'zero':
                            assert connection.recv(1) == b''
                            return
                        opcode, subscription = receive(connection)
                        assert opcode == 1
                        assert json.loads(subscription) == {'op': 'subscribe', 'args': [
                            {'channel': 'trades', 'instId': 'BTC-USDT'},
                            {'channel': 'trades', 'instId': 'ETH-USDT'}]}
                        for payload in payloads:
                            send(connection, payload)
                        send(connection, b'barrier', 9)
                        assert receive(connection) == (10, b'barrier')
                        ready.set()
                        if case == 'disconnect':
                            send(connection, b'\x03\xe8', 8)
                        while True:
                            try:
                                opcode, payload = receive(connection)
                            except EOFError:
                                break
                            assert (opcode, payload) == (1, b'ping')
                            if case != 'missing_pong':
                                send(connection, b'pong')
                                if case == 'capture':
                                    send(connection, later_payload)
            except BaseException as error:
                errors.append(error)
                ready.set()

        worker = threading.Thread(target=serve, daemon=True)
        worker.start()
        environment = dict(os.environ, SSL_CERT_FILE=str(certificate), SSL_CERT_DIR=str(root / 'empty'))
        started_us = time.time_ns() // 1000
        process = subprocess.Popen([client, '--config', str(config), '--set', '/md_sink/md_logger/file=' + str(output)], env=environment,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if case == 'signal':
            assert ready.wait(8)
            process.send_signal(signal.SIGTERM)
        stdout, stderr = process.communicate(timeout=12)
        assert not (root / 'unused_output').exists(), 'string override was not applied'
        worker.join(9)
        assert not worker.is_alive(), case
        assert not errors, (case, errors, stdout, stderr)
        assert process.returncode == (1 if case in ('disconnect', 'missing_pong', 'storage_error', 'flush_error') else 0), (case, stdout, stderr)
        finished_us = time.time_ns() // 1000
        if case == 'capture':
            assert stdout == '' and stderr == '', (stdout, stderr)
            operational_log = app_log.read_text()
            assert 'Connected:' in operational_log and 'Capture stopped:' in operational_log, operational_log
            assert 'BTC-USDT' not in operational_log and '"px"' not in operational_log
        elif case == 'zero':
            assert stdout == '' and stderr == '', 'configured log level must suppress INFO messages'
        if case in ('storage_error', 'flush_error'):
            assert output.is_dir()
            if case == 'flush_error':
                assert 'Capture finalization failed:' in stdout + stderr, (stdout, stderr)
                assert 'MD logger failed:' not in stdout + stderr, 'failure must first occur during stop'
            else:
                assert 'MD logger failed:' in stdout + stderr, (stdout, stderr)
            print(f'{case}: passed')
            return
        if case == 'zero':
            assert not output.exists(), (case, stdout, stderr)
            print(f'{case}: passed')
            return
        assert output.is_file(), (case, stdout, stderr)
        assert list(output.parent.iterdir()) == [output]
        records = list(reader.read_records(output))
        if case == 'capture':
            assert records[0] == {'received_us': 1, 'data': '{}'}, records
            records = records[1:]
        assert all(started_us <= record['received_us'] <= finished_us for record in records)
        received = [record['data'].encode('utf-8') for record in records]
        assert received[:len(payloads)] == expected_payloads, (case, received)
        if case == 'capture':
            assert received[len(payloads):] and all(payload == later_payload for payload in received[len(payloads):])
        else:
            assert received == expected_payloads, (case, received)
        raw = output.read_bytes()
        assert len(raw.splitlines()) == len(list(reader.read_records(output)))
        assert raw.endswith(b'\n') and b'NOMMD' not in raw
        lines = raw.splitlines()[1:] if case == 'capture' else raw.splitlines()
        for line in lines:
            timestamp, separator, body = line.partition(b' ')
            assert timestamp.isdigit() and separator == b' ', line
            assert isinstance(json.loads(body), dict), line
            assert body in expected_payloads + [later_payload], 'JSON changed beyond CR/LF replacement'
        assert b'BTC-USDT' not in stdout.encode() and b'"px"' not in stdout.encode()
        if case == 'capture':
            assert b'pong' not in received
            data = output.read_bytes()
            damaged = root / 'damaged.txt.partial'
            damaged.write_bytes(data[:-1])
            try:
                list(reader.read_records(damaged))
                raise AssertionError('strict reader accepted truncated capture')
            except ValueError:
                pass
            assert len(list(reader.read_records(damaged, recover=True))) == len(list(reader.read_records(output))) - 1
            for invalid in [b'bad timestamp {}\n', b'123\t{}\n', b'123 {bad}\n', b'123 {"v":"bad\\escape"}\n',
                            b'123 {"v":"\xff"}\n', b'123 []\n', b'123 pong\n', b'123 {"v":NaN}\n', b'123 {}\r\n']:
                damaged.write_bytes(data + invalid)
                try:
                    list(reader.read_records(damaged))
                    raise AssertionError('strict reader accepted malformed capture')
                except ValueError:
                    pass
                assert list(reader.read_records(damaged, recover=True)) == list(reader.read_records(output))
            inspected = subprocess.run([sys.executable, reader_path, str(output)],
                                       capture_output=True, text=True, timeout=5)
            assert inspected.returncode == 0, inspected
            assert [json.loads(line)['data'] for line in inspected.stdout.splitlines()] == [
                record['data'] for record in reader.read_records(output)]
        print(f'{case}: passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', required=True)
    parser.add_argument('--openssl', required=True)
    parser.add_argument('--reader', required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('capture_reader', args.reader)
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    with tempfile.TemporaryDirectory(prefix='nom_okx_capture_') as directory:
        root = Path(directory)
        config = root / 'check.json'
        side_effect = root / 'must_not_exist'
        minimal = {'md_subscriber': {'subscription': {'symbols': ['BTC-USDT']}}}
        config.write_text(json.dumps({**minimal, 'md_sink': {'md_logger': {'file': str(side_effect)}},
                                      'log': {'file': {'name': str(root / 'must_not_log')}}}))
        for mode, options in [('full', ['--check-config']), ('view', ['-k', 'view']),
                              ('silent', ['--check-config=silent'])]:
            checked = subprocess.run([args.client, '-c', str(config), '--set',
                                      '/md_subscriber/subscription/symbols=["ETH-USDT"]', *options],
                                     capture_output=True, text=True, timeout=5)
            assert checked.returncode == 0 and checked.stderr == '', checked
            if mode == 'silent':
                assert checked.stdout == 'OK\n', checked
            else:
                output = checked.stdout
                if mode == 'full':
                    assert 'Config lookup:' in output and f'Config OK (command line): {config}' in output, checked
                    output = output.split('Configuration:\n', 1)[1]
                normalized = json.loads(output)
                assert normalized['md_subscriber']['subscription'] == {
                    'symbols': ['ETH-USDT'], 'channels': ['tickers', 'trades', 'books']}, normalized
                assert normalized['md_subscriber']['connection'] == {
                    'endpoint': 'wss://ws.okx.com/ws/v5/public', 'heartbeat_sec': 20}, normalized
                assert normalized['md_sink']['md_logger']['file'] == str(side_effect), normalized
                assert normalized['md_sink']['md_logger']['flush_records'] == 4096, normalized
                assert normalized['health_checker'] == {'period_msec': 2000}, normalized
                assert set(normalized['md_sink']) == {'md_logger'}, normalized
                assert normalized['log'] == {'level': 'info', 'file': {
                    'name': str(root / 'must_not_log'), 'rotation_size': None}}, normalized
                assert normalized['exit_after_sec'] is None, normalized
            assert not side_effect.exists() and not (root / 'must_not_log').exists()
        # --set updates existing JSON paths; materialize defaults before overriding those paths.
        config.write_text(json.dumps(normalized))
        # Component defaults survive partial nested objects, serialization and overrides.
        for writer in [{}, {'flush_ms': 17}, {'flush_ms': None, 'flush_records': None}]:
            nested = root / 'writer_defaults.json'
            nested.write_text(json.dumps({**minimal, 'md_sink': {'md_logger': {'file': str(side_effect), **writer}}}))
            checked = subprocess.run([args.client, '-c', str(nested), '--check-config=view'],
                                     capture_output=True, text=True, timeout=5)
            assert checked.returncode == 0 and checked.stderr == '', checked
            normalized = json.loads(checked.stdout)
            assert normalized['md_sink']['md_logger'] == {
                'file': str(side_effect), 'flush_records': 4096,
                'flush_ms': writer.get('flush_ms') or 200}, normalized
            assert normalized['log'] == {'level': 'info', 'file': None}, normalized
            nested.write_text(checked.stdout)
            roundtrip = subprocess.run([args.client, '-c', str(nested), '-k', 'view'],
                                       capture_output=True, text=True, timeout=5)
            assert roundtrip.returncode == 0 and json.loads(roundtrip.stdout) == normalized, roundtrip
            overridden = subprocess.run([args.client, '-c', str(nested), '--set',
                                         '/md_sink/md_logger/flush_records=8', '--check-config=view'],
                                        capture_output=True, text=True, timeout=5)
            normalized['md_sink']['md_logger']['flush_records'] = 8
            assert overridden.returncode == 0 and json.loads(overridden.stdout) == normalized, overridden
        for health in [{}, {'period_msec': None}, {'period_msec': 0}, {'period_msec': 25}, {'period_msec': 60000}]:
            nested = root / 'health_defaults.json'
            nested.write_text(json.dumps({**minimal, 'md_sink': {'md_logger': {'file': str(side_effect)}},
                                          'health_checker': health}))
            checked = subprocess.run([args.client, '-c', str(nested), '--check-config=view'],
                                     capture_output=True, text=True, timeout=5)
            assert checked.returncode == 0 and checked.stderr == '', checked
            period = health.get('period_msec')
            assert json.loads(checked.stdout)['health_checker'] == {
                'period_msec': 2000 if period is None else period}, checked
        for period in [0, 25]:
            checked = subprocess.run([args.client, '-c', str(config), '--set',
                                      f'/health_checker/period_msec={period}', '--check-config=view'],
                                     capture_output=True, text=True, timeout=5)
            assert checked.returncode == 0 and checked.stderr == '', checked
            assert json.loads(checked.stdout)['health_checker'] == {'period_msec': period}, checked
        for level in ['debug', 'info', 'warn', 'error', 'always', 'DEBUG', 'INFO', 'WARN', 'ERROR', 'ALWAYS', None]:
            checked = subprocess.run([args.client, '-c', str(config), '--set',
                                      '/log/level=' + json.dumps(level), '--set',
                                      '/log/file/rotation_size=4096', '-k', 'view'],
                                     capture_output=True, text=True, timeout=5)
            assert checked.returncode == 0 and checked.stderr == '', checked
            normalized = json.loads(checked.stdout)
            assert normalized['log']['level'] == (level.lower() if level else 'info'), normalized
            assert normalized['log']['file']['rotation_size'] == 4096, normalized
            assert not (root / 'must_not_log').exists()
        for option in ['--help', '-h']:
            help_result = subprocess.run([args.client, '-c', str(root / 'missing.json'), option],
                                         capture_output=True, text=True, timeout=5)
            assert help_result.returncode == 0 and help_result.stderr == '', help_result
            for detail in ['--config', '--set', '--check-config', '-c', '-k', 'full', 'view', 'silent']:
                assert detail in help_result.stdout, help_result
            assert not side_effect.exists()
        for document, missing in [({}, 'md_subscriber'), ({'md_subscriber': {}}, 'subscription'),
                                  ({'md_subscriber': {'subscription': {}}}, 'symbols'),
                                  (minimal, 'md_sink'), ({**minimal, 'md_sink': {}}, 'md_logger'),
                                  ({**minimal, 'md_sink': {'md_logger': {}}}, 'file')]:
            required = root / 'missing_required.json'
            required.write_text(json.dumps(document))
            rejected = subprocess.run([args.client, '-c', str(required), '-k'],
                                      capture_output=True, text=True, timeout=5)
            assert rejected.returncode != 0 and missing in rejected.stderr, rejected
        invalid_overrides = [
            '/md_subscriber/subscription/symbols=[]',
            '/md_subscriber/subscription/symbols=null',
            '/md_subscriber/subscription/symbols=["btc-usdt","BTC-USDT"]',
            '/md_subscriber/subscription/symbols=["café-USDT"]',
            '/md_subscriber/subscription/channels=["invalid"]',
            '/md_subscriber/connection/heartbeat_sec=30',
            '/md_subscriber/connection/endpoint=ws://localhost',
            '/md_sink/md_logger/flush_records=0', '/md_sink/md_logger/flush_records=-1',
            '/md_sink/md_logger/flush_records=1.5', '/md_sink/md_logger/flush_records=true',
            '/md_sink/md_logger/flush_ms=0', '/md_sink/md_logger/flush_ms=-1',
            '/md_sink/md_logger/flush_ms=60001', '/md_sink/md_logger/flush_ms=1.5',
            '/md_sink/md_logger/file=', '/health_checker/period_msec=60001',
            '/health_checker/period_msec=-1', '/health_checker/period_msec=1.5',
            '/health_checker/period_msec=18446744073709551615',
            '/log/level=unknown', '/log/level=Info', '/log/level=1', '/log/level=true', '/log/file/name=',
            '/log/file/rotation_size=0', '/log/file/rotation_size=-1', '/exit_after_sec=86401']
        for override in invalid_overrides:
            invalid = subprocess.run([args.client, '-c', str(config), '--set', override, '--check-config'],
                                     capture_output=True, text=True, timeout=5)
            assert invalid.returncode != 0, override
        # Validation errors identify the owning component, rather than a misleading root field.
        for override, path in [('/md_subscriber/connection/heartbeat_sec=0',
                                '/md_subscriber/connection/heartbeat_sec'),
                               ('/md_sink/md_logger/flush_ms=0', '/md_sink/md_logger:'),
                               ('/health_checker/period_msec=60001', '/health_checker/period_msec'),
                               ('/log/file/name=', '/log/file/name')]:
            invalid = subprocess.run([args.client, '-c', str(config), '--set', override, '-k'],
                                     capture_output=True, text=True, timeout=5)
            assert invalid.returncode != 0 and path in invalid.stderr, invalid
        oversized = root / 'oversized_subscription.json'
        oversized.write_text(json.dumps({'md_subscriber': {'subscription': {'symbols': ['B' * 65536 + '-USDT']}},
                                         'md_sink': {'md_logger': {'file': str(side_effect)}}}))
        checked = subprocess.run([args.client, '-c', str(oversized), '--check-config=silent'],
                                 capture_output=True, text=True, timeout=5)
        assert checked.returncode == 0 and checked.stdout == 'OK\n' and checked.stderr == '', checked
        assert not side_effect.exists()
        rejected = subprocess.run([args.client, '-c', str(oversized)],
                                  capture_output=True, text=True, timeout=5)
        assert rejected.returncode != 0 and 'Subscription exceeds 64 KiB' in rejected.stderr, rejected
        assert not side_effect.exists()
        certificate, key = root / 'cert.pem', root / 'key.pem'
        subprocess.run([args.openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-noenc', '-keyout', str(key),
                        '-out', str(certificate), '-days', '1', '-subj', '/CN=localhost',
                        '-addext', 'subjectAltName=DNS:localhost'], check=True, capture_output=True)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        for case in ['capture', 'disconnect', 'missing_pong', 'signal', 'zero', 'storage_error', 'flush_error']:
            run_case(args.client, root, context, certificate, reader, case, args.reader)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
