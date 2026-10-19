CREATE TABLE IF NOT EXISTS events (
  event_id TEXT PRIMARY KEY,
  created_at INTEGER NOT NULL,
  event_type TEXT NOT NULL CHECK (event_type IN (
    'visit', 'connect_start', 'connect_success', 'connect_failure', 'connect_cancel',
    'flash_start', 'flash_success', 'flash_failure'
  )),
  visitor_id TEXT NOT NULL,
  attempt_id TEXT,
  chip TEXT NOT NULL DEFAULT 'unknown',
  version TEXT NOT NULL DEFAULT '',
  mode TEXT NOT NULL DEFAULT '',
  duration_ms INTEGER,
  error TEXT NOT NULL DEFAULT '',
  UNIQUE(attempt_id, event_type)
);
CREATE INDEX IF NOT EXISTS events_time ON events(created_at);
CREATE INDEX IF NOT EXISTS events_visitor_time ON events(visitor_id, created_at);
CREATE INDEX IF NOT EXISTS events_attempt ON events(attempt_id);
CREATE UNIQUE INDEX IF NOT EXISTS events_flash_result ON events(attempt_id)
  WHERE event_type IN ('flash_success', 'flash_failure');
CREATE UNIQUE INDEX IF NOT EXISTS events_connect_result ON events(attempt_id)
  WHERE event_type IN ('connect_success', 'connect_failure', 'connect_cancel');
