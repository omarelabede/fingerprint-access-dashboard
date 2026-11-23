"""Monitoring-only serial logger for the fingerprint access prototype."""

from __future__ import annotations

import argparse
import logging
from pathlib import Path
import time
from typing import Any

try:
    import serial
except ImportError:  # A clear error is raised when monitoring starts.
    serial = None  # type: ignore[assignment]

try:
    from .database import EventDatabase, ProcessResult
except ImportError:  # Supports: python host/access_logger.py
    from database import EventDatabase, ProcessResult


LOGGER = logging.getLogger("fingerprint_access_logger")
MAX_LINE_BYTES = 1024
RECONNECT_DELAY_SECONDS = 3.0
SERIAL_READ_WINDOW_BYTES = MAX_LINE_BYTES + 2  # Allows an exact-limit CRLF line.
PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATABASE_PATH = PROJECT_ROOT / "access_control.db"


def _payload_is_oversized(payload: bytes) -> bool:
    """Return whether the line content exceeds the configured byte limit."""

    content = payload
    if content.endswith(b"\n"):
        content = content[:-1]
        if content.endswith(b"\r"):
            content = content[:-1]
    return len(content) > MAX_LINE_BYTES


def _discard_line_remainder(connection: Any) -> None:
    """Discard the remainder of an oversized input line without writing to serial."""

    while True:
        remainder = connection.read_until(b"\n", size=SERIAL_READ_WINDOW_BYTES)
        if not remainder:
            # A serial timeout is not a line boundary. Stay in discard mode so a
            # delayed suffix cannot be interpreted as a new, valid event.
            continue
        if remainder.endswith(b"\n"):
            return


def _read_bounded_line(connection: Any) -> tuple[bytes, bool]:
    """Read one complete line or the bounded prefix of an oversized line.

    ``serial.read_until`` may return partial data when its timeout expires. Keeping
    the pending bytes here prevents a slow, valid event from being split into
    multiple malformed events. The boolean result tells the caller whether the
    unconsumed remainder of an oversized line must be discarded.
    """

    pending = bytearray()
    while True:
        remaining_window = max(1, SERIAL_READ_WINDOW_BYTES - len(pending))
        chunk = connection.read_until(b"\n", size=remaining_window)
        if not chunk:
            continue

        pending.extend(chunk)
        if pending.endswith(b"\n"):
            return bytes(pending), False

        if len(pending) > MAX_LINE_BYTES:
            # MAX_LINE_BYTES of content followed by CR may still be a valid CRLF
            # line, so wait for one more byte before classifying that case.
            if len(pending) == MAX_LINE_BYTES + 1 and pending.endswith(b"\r"):
                continue
            return bytes(pending), True


def _close_serial_connection(connection: Any) -> None:
    """Best-effort close without masking the reason monitoring is stopping."""

    if connection is None:
        return

    try:
        if connection.is_open:
            connection.close()
            LOGGER.info("Serial connection closed")
    except Exception as exc:  # pySerial/backends can fail while disconnecting.
        LOGGER.warning("Could not close the serial connection cleanly: %s", exc)


def _process_payload(database: EventDatabase, payload: bytes) -> None:
    if _payload_is_oversized(payload):
        preview = payload[:MAX_LINE_BYTES].decode("utf-8", errors="replace")
        database.record_invalid_event(preview, f"serial line exceeded {MAX_LINE_BYTES} bytes")
        LOGGER.warning("Rejected an oversized serial line")
        return

    try:
        line = payload.decode("utf-8", errors="strict").rstrip("\r\n")
    except UnicodeDecodeError:
        line = payload.decode("utf-8", errors="replace").rstrip("\r\n")
        database.record_invalid_event(line, "serial line is not valid UTF-8")
        LOGGER.warning("Rejected a serial line containing invalid UTF-8")
        return

    if not line:
        return

    result = database.process_line(line)
    if result is ProcessResult.STORED:
        LOGGER.info("Stored event: %s", line)
    elif result is ProcessResult.REPLAY_SUSPECTED:
        LOGGER.warning("Duplicate event rejected and replay alert created: %s", line)
    else:
        LOGGER.warning("Malformed event rejected and invalid-event alert created: %s", line)


def monitor_serial(port: str, baud: int, database_path: str | Path) -> None:
    """Continuously receive serial events and persist them until interrupted."""

    if serial is None:
        raise RuntimeError("pySerial is required; install dependencies from requirements.txt")
    if baud <= 0:
        raise ValueError("baud must be positive")

    with EventDatabase(database_path) as database:
        while True:
            connection = None
            retry_required = False
            try:
                LOGGER.info("Opening %s at %d baud", port, baud)
                connection = serial.Serial(port=port, baudrate=baud, timeout=1.0)
                LOGGER.info("Monitoring started (read-only; no commands are sent to Arduino)")

                while True:
                    payload, discard_remainder = _read_bounded_line(connection)
                    _process_payload(database, payload)
                    if discard_remainder:
                        _discard_line_remainder(connection)
            except KeyboardInterrupt:
                LOGGER.info("Monitoring stopped by the user")
                return
            except serial.SerialException as exc:
                LOGGER.warning(
                    "Serial connection unavailable (%s). Retrying in %.0f seconds.",
                    exc,
                    RECONNECT_DELAY_SECONDS,
                )
                retry_required = True
            finally:
                # Release a failed port before waiting or attempting a reconnect.
                _close_serial_connection(connection)

            if retry_required:
                try:
                    time.sleep(RECONNECT_DELAY_SECONDS)
                except KeyboardInterrupt:
                    LOGGER.info("Monitoring stopped by the user during reconnect wait")
                    return


def build_argument_parser() -> argparse.ArgumentParser:
    """Build the command-line parser for the monitoring application."""

    parser = argparse.ArgumentParser(description="Log fingerprint events from Arduino to SQLite")
    parser.add_argument("--port", required=True, help="Arduino serial port, for example COM7")
    parser.add_argument("--baud", type=int, default=9600, help="Serial baud rate (default: 9600)")
    parser.add_argument(
        "--database",
        default=str(DEFAULT_DATABASE_PATH),
        help=f"SQLite database path (default: {DEFAULT_DATABASE_PATH})",
    )
    return parser


def main() -> int:
    """Run the command-line serial monitor."""

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    arguments = build_argument_parser().parse_args()
    database_path = Path(arguments.database).expanduser().resolve()
    LOGGER.info("Using database: %s", database_path)
    try:
        monitor_serial(arguments.port, arguments.baud, database_path)
    except (RuntimeError, ValueError) as exc:
        LOGGER.error("%s", exc)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
