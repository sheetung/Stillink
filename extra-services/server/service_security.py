"""Persistent service credentials, independent of photos and device login keys."""
import secrets
from pathlib import Path


def load_key(directory, rotate=False):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / "service.key"
    if not rotate and path.exists():
        key = path.read_text(encoding="ascii").strip()
        if len(key) != 32 or any(c not in "0123456789abcdef" for c in key):
            raise ValueError("Invalid data/service.key; restore its backup or explicitly rotate the key")
        return key
    key = secrets.token_hex(16)
    if rotate:
        temporary = path.with_suffix(".tmp")
        temporary.write_text(key + "\n", encoding="ascii")
        temporary.chmod(0o600)
        temporary.replace(path)
    else:
        try:
            with path.open("x", encoding="ascii") as stream:
                stream.write(key + "\n")
            path.chmod(0o600)
        except FileExistsError:
            return load_key(directory)
    return key
