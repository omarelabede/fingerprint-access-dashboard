"""Parser and consistency checks for the Arduino serial event protocol."""

from __future__ import annotations

from dataclasses import dataclass, field
import re


EVENT_FIELD_COUNT = 15
DEVICE_ID = "FSAS-LED-01"

SUPPORTED_EVENT_TYPES = frozenset(
    {"SYSTEM_READY", "ACCESS", "LOCKOUT_START", "LOCKOUT_END", "SYSTEM_ERROR"}
)
SUPPORTED_AUTHENTICATION_RESULTS = frozenset(
    {"SUCCESS", "FAILED", "ERROR", "LOCKOUT", "READY"}
)
SUPPORTED_ACCESS_STATUSES = frozenset({"GRANTED", "DENIED", "LOCKED", "IDLE"})
SUPPORTED_INDICATOR_STATES = frozenset({"GREEN", "RED", "OFF"})
AUTHORIZED_IDENTITIES = {1: "ADMINISTRATOR", 2: "TECHNICIAN"}
INITIALIZATION_ERROR_REASONS = frozenset({"SENSOR_INITIALIZATION_FAILED"})
TRANSIENT_ERROR_REASONS = frozenset(
    {
        "SENSOR_COMMUNICATION_ERROR",
        "IMAGE_CAPTURE_ERROR",
        "FEATURE_EXTRACTION_ERROR",
        "SENSOR_PROCESSING_ERROR",
    }
)

_INTEGER_PATTERN = re.compile(r"-?(?:0|[1-9][0-9]*)\Z")


class ProtocolError(ValueError):
    """Raised when a serial line is malformed or internally inconsistent."""


@dataclass(frozen=True, slots=True)
class Event:
    """An immutable, validated event received from the Arduino."""

    device_id: str
    boot_id: int
    event_seq: int
    uptime_ms: int
    event_type: str
    template_id: int
    user_name: str
    confidence: int
    authentication_result: str
    access_status: str
    reason: str
    failed_count: int
    latency_ms: int
    indicator_state: str
    raw_line: str = field(repr=False, compare=False)


def _parse_integer(field_name: str, value: str) -> int:
    if not _INTEGER_PATTERN.fullmatch(value):
        raise ProtocolError(f"{field_name} must be a base-10 integer")
    try:
        return int(value)
    except ValueError as exc:  # Defensive: the regular expression already limits input.
        raise ProtocolError(f"{field_name} is outside the supported integer range") from exc


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ProtocolError(message)


def _validate_ranges(event: Event) -> None:
    _require(event.device_id == DEVICE_ID, f"device_id must be {DEVICE_ID}")
    _require(event.boot_id >= 1, "boot_id must be at least 1")
    _require(event.event_seq >= 1, "event_seq must be at least 1")
    _require(event.uptime_ms >= 0, "uptime_ms cannot be negative")
    _require(event.template_id == -1 or event.template_id >= 1, "template_id must be -1 or positive")
    _require(event.confidence >= 0, "confidence cannot be negative")
    _require(0 <= event.failed_count <= 4, "failed_count must be between 0 and 4")
    _require(event.latency_ms >= 0, "latency_ms cannot be negative")
    _require(event.reason != "", "reason cannot be empty")


def _validate_access_event(event: Event) -> None:
    _require(
        event.authentication_result in {"SUCCESS", "FAILED"},
        "ACCESS must have SUCCESS or FAILED authentication_result",
    )

    if event.authentication_result == "SUCCESS":
        expected_user = AUTHORIZED_IDENTITIES.get(event.template_id)
        _require(expected_user is not None, "successful access requires template ID 1 or 2")
        _require(event.user_name == expected_user, "user_name does not match the authorized template ID")
        _require(event.access_status == "GRANTED", "successful access must be GRANTED")
        _require(event.indicator_state == "GREEN", "successful access must use GREEN indicator")
        _require(event.reason == "NONE", "successful access reason must be NONE")
        _require(event.failed_count == 0, "successful access must reset failed_count to 0")
        return

    _require(event.access_status == "DENIED", "failed access must be DENIED")
    _require(event.indicator_state == "RED", "failed access must use RED indicator")
    _require(1 <= event.failed_count <= 4, "failed access must have failed_count from 1 to 4")

    if event.template_id == -1:
        _require(event.user_name == "UNKNOWN", "unknown fingerprint user_name must be UNKNOWN")
        _require(
            event.reason == "FINGERPRINT_NOT_MATCHED",
            "unknown fingerprint reason must be FINGERPRINT_NOT_MATCHED",
        )
    else:
        _require(
            event.template_id not in AUTHORIZED_IDENTITIES,
            "an approved identity cannot produce a FAILED ACCESS event",
        )
        _require(event.user_name == "UNKNOWN", "unapproved template user_name must be UNKNOWN")
        _require(
            event.reason == "UNAPPROVED_TEMPLATE_ID",
            "unapproved template reason must be UNAPPROVED_TEMPLATE_ID",
        )


def _validate_event_consistency(event: Event) -> None:
    if event.indicator_state == "GREEN":
        _require(
            event.authentication_result == "SUCCESS" and event.access_status == "GRANTED",
            "GREEN is valid only for successful granted access",
        )
    if event.access_status == "GRANTED":
        _require(
            event.authentication_result == "SUCCESS" and event.indicator_state == "GREEN",
            "GRANTED requires SUCCESS and GREEN",
        )
    if event.authentication_result == "FAILED":
        _require(event.indicator_state != "GREEN", "FAILED cannot use GREEN indicator")

    if event.event_type == "ACCESS":
        _validate_access_event(event)
        return

    _require(event.template_id == -1, f"{event.event_type} must use template_id -1")
    _require(event.confidence == 0, f"{event.event_type} must use confidence 0")

    if event.event_type == "SYSTEM_READY":
        _require(event.user_name == "SYSTEM", "SYSTEM_READY user_name must be SYSTEM")
        _require(event.authentication_result == "READY", "SYSTEM_READY result must be READY")
        _require(event.access_status == "IDLE", "SYSTEM_READY status must be IDLE")
        _require(event.reason == "NONE", "SYSTEM_READY reason must be NONE")
        _require(event.failed_count == 0, "SYSTEM_READY failed_count must be 0")
        _require(event.indicator_state == "OFF", "SYSTEM_READY indicator must be OFF")
    elif event.event_type == "LOCKOUT_START":
        _require(event.user_name == "UNKNOWN", "LOCKOUT_START user_name must be UNKNOWN")
        _require(event.authentication_result == "LOCKOUT", "LOCKOUT_START result must be LOCKOUT")
        _require(event.access_status == "LOCKED", "LOCKOUT_START status must be LOCKED")
        _require(event.reason == "FOUR_FAILED_ATTEMPTS", "LOCKOUT_START reason is invalid")
        _require(event.failed_count == 4, "LOCKOUT_START failed_count must be 4")
        _require(event.latency_ms == 0, "LOCKOUT_START latency_ms must be 0")
        _require(event.indicator_state == "RED", "LOCKOUT_START indicator must be RED")
    elif event.event_type == "LOCKOUT_END":
        _require(event.user_name == "SYSTEM", "LOCKOUT_END user_name must be SYSTEM")
        _require(event.authentication_result == "READY", "LOCKOUT_END result must be READY")
        _require(event.access_status == "IDLE", "LOCKOUT_END status must be IDLE")
        _require(event.reason == "TIME_EXPIRED", "LOCKOUT_END reason must be TIME_EXPIRED")
        _require(event.failed_count == 0, "LOCKOUT_END failed_count must be 0")
        _require(event.latency_ms == 0, "LOCKOUT_END latency_ms must be 0")
        _require(event.indicator_state == "OFF", "LOCKOUT_END indicator must be OFF")
    elif event.event_type == "SYSTEM_ERROR":
        _require(event.user_name == "SYSTEM", "SYSTEM_ERROR user_name must be SYSTEM")
        _require(event.authentication_result == "ERROR", "SYSTEM_ERROR result must be ERROR")
        _require(event.indicator_state == "RED", "SYSTEM_ERROR indicator must be RED")
        if event.reason in INITIALIZATION_ERROR_REASONS:
            _require(
                event.access_status == "LOCKED",
                "sensor initialization failure must use LOCKED status",
            )
        elif event.reason in TRANSIENT_ERROR_REASONS:
            # The main sketch emits DENIED while showing the red LED for two
            # seconds. LOCKED remains accepted for the structured diagnostic
            # example and for a host observing a sensor safe-stop condition.
            _require(
                event.access_status in {"DENIED", "LOCKED"},
                "transient sensor error must use DENIED or LOCKED status",
            )
        else:
            raise ProtocolError(f"unsupported SYSTEM_ERROR reason: {event.reason}")


def parse_event(line: str) -> Event:
    """Parse and validate one newline-terminated protocol event.

    The newline is optional for callers that already removed it. Other leading or
    trailing whitespace is significant and is rejected by the field validators.
    """

    if not isinstance(line, str):
        raise ProtocolError("event line must be text")

    normalized = line.rstrip("\r\n")
    if not normalized:
        raise ProtocolError("event line is empty")
    if "\r" in normalized or "\n" in normalized:
        raise ProtocolError("event line contains an embedded newline")

    fields = normalized.split("|")
    if len(fields) != EVENT_FIELD_COUNT:
        raise ProtocolError(f"expected {EVENT_FIELD_COUNT} fields, received {len(fields)}")
    if fields[0] != "EVT":
        raise ProtocolError("event prefix must be EVT")

    event_type = fields[5]
    authentication_result = fields[9]
    access_status = fields[10]
    indicator_state = fields[14]

    if event_type not in SUPPORTED_EVENT_TYPES:
        raise ProtocolError(f"unsupported event_type: {event_type}")
    if authentication_result not in SUPPORTED_AUTHENTICATION_RESULTS:
        raise ProtocolError(f"unsupported authentication_result: {authentication_result}")
    if access_status not in SUPPORTED_ACCESS_STATUSES:
        raise ProtocolError(f"unsupported access_status: {access_status}")
    if indicator_state not in SUPPORTED_INDICATOR_STATES:
        raise ProtocolError(f"unsupported indicator_state: {indicator_state}")

    event = Event(
        device_id=fields[1],
        boot_id=_parse_integer("boot_id", fields[2]),
        event_seq=_parse_integer("event_seq", fields[3]),
        uptime_ms=_parse_integer("uptime_ms", fields[4]),
        event_type=event_type,
        template_id=_parse_integer("template_id", fields[6]),
        user_name=fields[7],
        confidence=_parse_integer("confidence", fields[8]),
        authentication_result=authentication_result,
        access_status=access_status,
        reason=fields[11],
        failed_count=_parse_integer("failed_count", fields[12]),
        latency_ms=_parse_integer("latency_ms", fields[13]),
        indicator_state=indicator_state,
        raw_line=normalized,
    )
    _validate_ranges(event)
    _validate_event_consistency(event)
    return event
