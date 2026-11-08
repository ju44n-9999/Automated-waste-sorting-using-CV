"""Conservative, persistent rolling quota for one demo backend."""
import math
from pathlib import Path
import sqlite3
import time

from fastapi import HTTPException


class GroqQuota:
    def __init__(self, path: str, interval: float, daily_limit: int):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.path, self.interval, self.daily_limit = path, interval, daily_limit
        with sqlite3.connect(path) as db:
            db.execute('CREATE TABLE IF NOT EXISTS attempts (started REAL NOT NULL)')
            db.execute('CREATE TABLE IF NOT EXISTS cooldown (until REAL NOT NULL)')

    def reserve(self, now=None):
        now = time.time() if now is None else now
        with sqlite3.connect(self.path) as db:
            db.execute('BEGIN IMMEDIATE')
            db.execute('DELETE FROM attempts WHERE started <= ?', (now - 86400,))
            count, first, last = db.execute(
                'SELECT COUNT(*), MIN(started), MAX(started) FROM attempts').fetchone()
            cooldown = db.execute('SELECT MAX(until) FROM cooldown').fetchone()[0] or 0
            daily_until = first + 86400 if count >= self.daily_limit and first is not None else 0
            interval_until = last + self.interval if last is not None else 0
            until = max(cooldown, interval_until, daily_until)
            if now < until:
                reason = 'daily_quota' if now < daily_until else 'Groq demo quota/cooldown reached'
                raise HTTPException(429, reason,
                                    headers={'Retry-After': str(max(1, math.ceil(until - now)))})
            # Count failed upstream attempts too; never auto-refund uncertain charges.
            db.execute('INSERT INTO attempts VALUES (?)', (now,))

    def daily_block(self, now=None):
        """Seconds until a new call is allowed, or 0. Only the daily cap blocks."""
        now = time.time() if now is None else now
        with sqlite3.connect(self.path) as db:
            db.execute('DELETE FROM attempts WHERE started <= ?', (now - 86400,))
            count, first = db.execute('SELECT COUNT(*), MIN(started) FROM attempts').fetchone()
            if count >= self.daily_limit and first is not None and now < first + 86400:
                return max(1, math.ceil(first + 86400 - now))
            return 0

    def backoff(self, seconds):
        with sqlite3.connect(self.path) as db:
            db.execute('DELETE FROM cooldown')
            db.execute('INSERT INTO cooldown VALUES (?)', (time.time() + seconds,))
