"""Chunked UDP announcements: SKM1 + SHA256 + part/count/length + JSON."""
import hashlib
import struct

HEADER = struct.Struct(">4s32sHHI")
CHUNK = 1200
MAX_MESSAGE = 6 * 1024 * 1024


def packets(body):
    if not body or len(body) > MAX_MESSAGE: raise ValueError("Music announcement too large")
    digest = hashlib.sha256(body).digest()
    count = (len(body) + CHUNK - 1) // CHUNK
    return [HEADER.pack(b"SKM1", digest, i, count, len(body)) + body[i*CHUNK:(i+1)*CHUNK]
            for i in range(count)]
