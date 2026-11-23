"""Create an empty SQLite database using the application's authoritative schema."""

from __future__ import annotations

import argparse
from pathlib import Path

try:
    from .database import EventDatabase
except ImportError:  # Supports: python host/init_database.py
    from database import EventDatabase


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATABASE_PATH = PROJECT_ROOT / "access_control.db"


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Initialize the fingerprint event database")
    parser.add_argument(
        "--database",
        default=str(DEFAULT_DATABASE_PATH),
        help=f"SQLite database path (default: {DEFAULT_DATABASE_PATH})",
    )
    return parser


def main() -> int:
    arguments = build_argument_parser().parse_args()
    database_path = Path(arguments.database).expanduser().resolve()
    database_path.parent.mkdir(parents=True, exist_ok=True)
    with EventDatabase(database_path):
        pass
    print(f"SQLite database is ready: {database_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
