"""Set firmware, page and extra-service versions from a release tag."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
tag = sys.argv[1]
if not re.fullmatch(r"v\d+\.\d+\.\d+", tag):
    raise SystemExit("Expected tag vX.Y.Z")
version = tag.upper()
for path, pattern, replacement in (
    (root / "main/version.h", r'(#define\s+EPAPER_VERSION\s+)"[^"]+"', rf'\g<1>"{version}"'),
    (root / "page/index.html", r'(<meta\s+name="epaper-page-version"\s+content=")[^"]+("\s*>)', rf'\g<1>{version}\g<2>'),
):
    text, count = re.subn(pattern, replacement, path.read_text(encoding="utf-8"))
    if count != 1: raise SystemExit(f"Expected one version in {path}")
    path.write_text(text, encoding="utf-8")

for folder, package in (("server", "stillink-services"), ("music-client-win", "stillink-music")):
    directory = root / "extra-services" / folder
    for filename, pattern in (
        ("pyproject.toml", r'(?m)^(version = ")[^"]+("$)'),
        ("uv.lock", rf'(name = "{package}"\nversion = ")[^"]+(")'),
    ):
        path = directory / filename
        text, count = re.subn(pattern, rf'\g<1>{tag[1:]}\g<2>', path.read_text(encoding="utf-8"), count=1)
        if count != 1: raise SystemExit(f"Expected package version in {path}")
        path.write_text(text, encoding="utf-8")

path = root / "extra-services/server/app.py"
text, count = re.subn(r'(?m)^VERSION = "[^"]+"', f'VERSION = "{version}"', path.read_text(encoding="utf-8"))
if count != 1: raise SystemExit(f"Expected service version in {path}")
path.write_text(text, encoding="utf-8")
