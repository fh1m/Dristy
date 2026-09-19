#!/usr/bin/env python3
"""HackyLens MicroPython storage/runtime command-line client."""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict
from pathlib import Path

from hmpy_client import (
    DEFAULT_CONNECT_TIMEOUT,
    HmpyClient,
    HmpyClientError,
    HmpyEvent,
    open_serial_transport,
)
from hmpy_protocol import MessageType


def load_serial():
    try:
        import serial
        from serial.tools import list_ports
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc
    return serial, list_ports


def available_ports() -> list[str]:
    _, list_ports = load_serial()
    return [port.device for port in list_ports.comports()]


def choose_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    ports = available_ports()
    if len(ports) == 1:
        return ports[0]
    if not ports:
        raise SystemExit("no serial ports found; pass --port")
    raise SystemExit("multiple serial ports found; pass --port: " + ", ".join(ports))


def print_event(event: HmpyEvent) -> None:
    if event.type is MessageType.STDOUT:
        sys.stdout.buffer.write(event.data)
        sys.stdout.buffer.flush()
    elif event.type is MessageType.STDERR:
        sys.stderr.buffer.write(event.data)
        sys.stderr.buffer.flush()
    elif event.type is MessageType.DROPPED:
        print(
            f"\n[HMPY dropped stream={event.dropped_stream} bytes={event.dropped_count}]",
            file=sys.stderr,
        )
    elif event.type is MessageType.STATE:
        print(
            f"[HMPY state={event.state} exit={event.exit_reason} run={event.run_id}]",
            file=sys.stderr,
        )
    elif event.type is MessageType.FILE_CHANGED:
        print(
            f"[HMPY file op={event.file_operation} name={event.file_name}]",
            file=sys.stderr,
        )


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--port", help="serial port; auto-selected when unique")
    result.add_argument("--baud", type=int, default=115200)
    result.add_argument("--timeout", type=float, default=5.0)
    result.add_argument(
        "--connect-timeout",
        type=float,
        default=DEFAULT_CONNECT_TIMEOUT,
        help="seconds to retry the line handshake while the board boots",
    )
    commands = result.add_subparsers(dest="command", required=True)
    commands.add_parser("ports", help="list serial ports")
    commands.add_parser("hello")
    commands.add_parser("list")
    stat = commands.add_parser("stat")
    stat.add_argument("name")
    read = commands.add_parser("read")
    read.add_argument("name")
    read.add_argument("output", nargs="?", default="-")
    upload = commands.add_parser("upload")
    upload.add_argument("source", type=Path)
    upload.add_argument("--name")
    upload.add_argument("--startup", action="store_true")
    upload.add_argument("--run", action="store_true")
    upload.add_argument("--time-limit-ms", type=int, default=0)
    delete = commands.add_parser("delete")
    delete.add_argument("name")
    startup = commands.add_parser("startup")
    startup.add_argument("name", nargs="?")
    format_cmd = commands.add_parser("format")
    format_cmd.add_argument("--confirm", required=True, metavar="'ERASE USERFS'")
    run = commands.add_parser("run")
    run.add_argument("name", nargs="?")
    run.add_argument("--time-limit-ms", type=int, default=0)
    commands.add_parser("stop")
    commands.add_parser("status")
    monitor = commands.add_parser("monitor")
    monitor.add_argument("--seconds", type=float, default=0.0)
    return result


def main() -> int:
    args = parser().parse_args()
    if args.command == "ports":
        for port in available_ports():
            print(port)
        return 0

    serial, _ = load_serial()
    port = choose_port(args.port)
    device = open_serial_transport(
        serial,
        port,
        args.baud,
        timeout=0.05,
        write_timeout=args.timeout,
    )
    client = HmpyClient(
        device,
        timeout=args.timeout,
        connect_timeout=args.connect_timeout,
        event_handler=print_event,
    )
    try:
        client.open()
        if args.command == "hello":
            print(json.dumps(asdict(client.hello()), indent=2))
        elif args.command == "list":
            for entry in client.list_files():
                print(f"{entry.size:8d}  {entry.name}")
        elif args.command == "stat":
            print(json.dumps(asdict(client.stat(args.name)), indent=2))
        elif args.command == "read":
            content = client.read_file(args.name)
            if args.output == "-":
                sys.stdout.buffer.write(content)
            else:
                Path(args.output).write_bytes(content)
        elif args.command == "upload":
            content = args.source.read_bytes()
            name = args.name or args.source.name
            client.upload(name, content)
            if args.startup:
                client.set_startup(name)
            if args.run:
                print(client.run(name, args.time_limit_ms))
        elif args.command == "delete":
            client.delete(args.name)
        elif args.command == "startup":
            client.set_startup(args.name)
        elif args.command == "format":
            client.format_userfs(args.confirm)
        elif args.command == "run":
            print(client.run(args.name, args.time_limit_ms))
        elif args.command == "stop":
            client.stop()
        elif args.command == "status":
            print(json.dumps(asdict(client.status()), indent=2))
        elif args.command == "monitor":
            started = time.monotonic()
            ping_at = started
            while args.seconds <= 0 or time.monotonic() - started < args.seconds:
                client.poll(0.2)
                if time.monotonic() >= ping_at:
                    client.ping(b"hkpy")
                    ping_at = time.monotonic() + 2.0
        return 0
    except (HmpyClientError, OSError, ValueError) as exc:
        print(f"hkpy: {exc}", file=sys.stderr)
        return 2
    finally:
        try:
            client.close()
        except Exception:
            pass
        device.close()


if __name__ == "__main__":
    raise SystemExit(main())
