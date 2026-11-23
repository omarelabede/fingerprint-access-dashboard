"""Authenticated, bilingual, read-only Flask monitoring dashboard."""

from __future__ import annotations

import argparse
import hmac
import os
from pathlib import Path
from typing import Any

from flask import Flask, Response, g, jsonify, redirect, render_template, request, url_for

try:
    from .database import EventDatabase
except ImportError:  # Supports: python host/dashboard.py
    from database import EventDatabase


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATABASE_PATH = PROJECT_ROOT / "access_control.db"
USER_KEYS = {
    "ADMINISTRATOR": "userAdmin",
    "TECHNICIAN": "userTech",
    "UNKNOWN": "userUnknown",
    "SYSTEM": "userSystem",
}


def _percent(part: int, total: int) -> str:
    """Return a dashboard percentage without dividing by zero."""

    return f"{(part / total * 100.0) if total else 0.0:.1f}"


def _ui_events(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Convert database rows to the compact browser view model."""

    return [
        {
            "id": event["id"],
            "at": event["received_at"],
            "user": USER_KEYS.get(event["user_name"], event["user_name"]),
            "raw_user": event["user_name"],
            "result": event["authentication_result"],
            "status": event["access_status"],
            "reason": event["reason"],
            "failed": event["failed_count"],
            "latency": event["latency_ms"],
            "indicator": event["indicator_state"],
        }
        for event in events
    ]


def _ui_alerts(alerts: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Convert stored alerts to cards without inventing descriptions."""

    mapped: list[dict[str, Any]] = []
    for alert in alerts:
        alert_type = alert["alert_type"]
        visual_type = "replay" if alert_type == "REPLAY_SUSPECTED" else "invalid"
        mapped.append(
            {
                "type": visual_type,
                "title": "replayAlert" if visual_type == "replay" else "invalidAlert",
                "text": alert["description"],
                "meta": (
                    f"{alert.get('device_id') or '-'} · "
                    f"boot {alert.get('boot_id') if alert.get('boot_id') is not None else '-'} · "
                    f"seq {alert.get('event_seq') if alert.get('event_seq') is not None else '-'}"
                ),
                "date": alert["created_at"],
                "badge": "review",
            }
        )
    return mapped


def _constant_time_text_equal(left: str, right: str) -> bool:
    """Compare credentials safely, including non-ASCII usernames/passwords."""

    return hmac.compare_digest(left.encode("utf-8"), right.encode("utf-8"))


def _dashboard_payload(snapshot: dict[str, Any]) -> dict[str, Any]:
    """Build the shared HTML/API view model from one database snapshot."""

    summary = snapshot["summary"]
    raw_events = snapshot["events"]
    total = int(summary["total_attempts"])
    successful = int(summary["successful_attempts"])
    failed = int(summary["failed_attempts"])
    return {
        "summary": summary,
        "events": _ui_events(raw_events),
        "alerts": _ui_alerts(snapshot["alerts"]),
        "latest": raw_events[0] if raw_events else None,
        "latest_administrator": snapshot["latest_administrator"],
        "latest_technician": snapshot["latest_technician"],
        "authorized_percent": _percent(successful, total),
        "denied_percent": _percent(failed, total),
        "administrator_percent": _percent(
            int(summary["administrator_attempts"]), total
        ),
        "technician_percent": _percent(int(summary["technician_attempts"]), total),
        "unknown_percent": _percent(int(summary["unknown_attempts"]), total),
    }


def create_app(database_path: str | Path = DEFAULT_DATABASE_PATH) -> Flask:
    """Create the Basic-Auth-protected dashboard with GET routes only."""

    username = os.environ.get("DASHBOARD_USERNAME")
    password = os.environ.get("DASHBOARD_PASSWORD")
    if not username or not password:
        raise RuntimeError("DASHBOARD_USERNAME and DASHBOARD_PASSWORD must both be set")

    app = Flask(
        __name__,
        template_folder=str(PROJECT_ROOT / "templates"),
        static_folder=str(PROJECT_ROOT / "static"),
    )
    app.config.update(
        DATABASE_PATH=str(Path(database_path).expanduser().resolve()),
        DASHBOARD_USERNAME=username,
        DASHBOARD_PASSWORD=password,
    )

    def get_database() -> EventDatabase:
        if "event_database" not in g:
            g.event_database = EventDatabase(app.config["DATABASE_PATH"])
        return g.event_database

    @app.teardown_appcontext
    def close_database(exception: BaseException | None) -> None:
        database = g.pop("event_database", None)
        if database is not None:
            database.close()

    @app.before_request
    def require_basic_auth() -> Response | None:
        authorization = request.authorization
        valid = bool(
            authorization
            and authorization.type.lower() == "basic"
            and _constant_time_text_equal(
                authorization.username or "", app.config["DASHBOARD_USERNAME"]
            )
            and _constant_time_text_equal(
                authorization.password or "", app.config["DASHBOARD_PASSWORD"]
            )
        )
        if valid:
            return None
        return Response(
            "Authentication required",
            401,
            {"WWW-Authenticate": 'Basic realm="Fingerprint Dashboard", charset="UTF-8"'},
        )

    def render_dashboard(language: str) -> str:
        payload = _dashboard_payload(get_database().dashboard_snapshot())

        return render_template(
            "dashboard.html",
            language=language,
            direction="rtl" if language == "ar" else "ltr",
            summary=payload["summary"],
            events_json=payload["events"],
            alerts_json=payload["alerts"],
            latest=payload["latest"],
            latest_administrator=payload["latest_administrator"],
            latest_technician=payload["latest_technician"],
            record_count=int(payload["summary"]["total_events"]),
            alert_count=int(payload["summary"]["total_alerts"]),
            authorized_percent=payload["authorized_percent"],
            denied_percent=payload["denied_percent"],
            administrator_percent=payload["administrator_percent"],
            technician_percent=payload["technician_percent"],
            unknown_percent=payload["unknown_percent"],
        )

    @app.get("/")
    def index() -> Response:
        return redirect(url_for("dashboard_ar"))

    @app.get("/en")
    def dashboard_en() -> str:
        return render_dashboard("en")

    @app.get("/ar")
    def dashboard_ar() -> str:
        return render_dashboard("ar")

    @app.get("/api/summary")
    def api_summary() -> Response:
        return jsonify(get_database().summary())

    @app.get("/api/events")
    def api_events() -> Response:
        return jsonify({"events": get_database().recent_events(_requested_limit())})

    @app.get("/api/alerts")
    def api_alerts() -> Response:
        return jsonify({"alerts": get_database().recent_alerts(_requested_limit())})

    @app.get("/api/snapshot")
    def api_snapshot() -> Response:
        """Return the live dashboard model from one consistent read transaction."""

        response = jsonify(_dashboard_payload(get_database().dashboard_snapshot()))
        response.headers["Cache-Control"] = "no-store"
        return response

    def _requested_limit() -> int:
        try:
            return int(request.args.get("limit", "50"))
        except ValueError:
            return 50

    return app


def build_argument_parser() -> argparse.ArgumentParser:
    """Build command-line arguments for the local dashboard server."""

    parser = argparse.ArgumentParser(description="Run the read-only fingerprint dashboard")
    parser.add_argument(
        "--database",
        default=str(DEFAULT_DATABASE_PATH),
        help=f"SQLite database path (default: {DEFAULT_DATABASE_PATH})",
    )
    return parser


def main() -> int:
    """Run the local dashboard on 127.0.0.1:5000."""

    arguments = build_argument_parser().parse_args()
    try:
        database_path = Path(arguments.database).expanduser().resolve()
        app = create_app(database_path)
    except RuntimeError as exc:
        print(f"Configuration error: {exc}")
        return 2
    print(f"Database: {database_path}")
    print("Arabic dashboard: http://127.0.0.1:5000/ar")
    print("English dashboard: http://127.0.0.1:5000/en")
    app.run(host="127.0.0.1", port=5000, debug=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
