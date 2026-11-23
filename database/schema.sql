PRAGMA foreign_keys = ON;
PRAGMA busy_timeout = 10000;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

CREATE TABLE IF NOT EXISTS access_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    device_id TEXT NOT NULL,
    boot_id INTEGER NOT NULL,
    event_seq INTEGER NOT NULL,
    uptime_ms INTEGER NOT NULL,
    event_type TEXT NOT NULL,
    template_id INTEGER NOT NULL,
    user_name TEXT NOT NULL,
    confidence INTEGER NOT NULL,
    authentication_result TEXT NOT NULL,
    access_status TEXT NOT NULL,
    reason TEXT NOT NULL,
    failed_count INTEGER NOT NULL,
    latency_ms INTEGER NOT NULL,
    indicator_state TEXT NOT NULL,
    received_at TEXT NOT NULL,
    raw_line TEXT NOT NULL,
    UNIQUE(device_id, boot_id, event_seq)
);

CREATE TABLE IF NOT EXISTS security_alerts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    alert_type TEXT NOT NULL,
    device_id TEXT,
    boot_id INTEGER,
    event_seq INTEGER,
    raw_line TEXT NOT NULL,
    description TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_access_events_received_at
    ON access_events(received_at DESC, id DESC);

CREATE INDEX IF NOT EXISTS idx_security_alerts_created_at
    ON security_alerts(created_at DESC, id DESC);
