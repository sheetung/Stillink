-- Existing databases only: execute once before deploying the updated Worker.
ALTER TABLE events ADD COLUMN browser TEXT NOT NULL DEFAULT 'unknown';
ALTER TABLE events ADD COLUMN os TEXT NOT NULL DEFAULT 'unknown';
