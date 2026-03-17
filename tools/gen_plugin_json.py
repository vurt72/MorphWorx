import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_H = ROOT / "src" / "version.h"
PLUGIN_JSON = ROOT / "plugin.json"

def read_version():
    """Parse MORPHWORX_VERSION_* macros from src/version.h.

    We intentionally keep this parser simple and robust against
    whitespace or formatting differences by just tokenizing lines
    that mention MORPHWORX_VERSION_*.
    """
    if not VERSION_H.is_file():
        print(f"[gen_plugin_json] version header not found: {VERSION_H}", file=sys.stderr)
        return None

    parts = {"MAJOR": None, "MINOR": None, "PATCH": None}
    with VERSION_H.open("r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line.startswith("#define MORPHWORX_VERSION_"):
                continue
            tokens = line.split()
            if len(tokens) < 3:
                continue
            name = tokens[1]
            value = tokens[2]
            suffix = None
            if name.endswith("MAJOR"):
                suffix = "MAJOR"
            elif name.endswith("MINOR"):
                suffix = "MINOR"
            elif name.endswith("PATCH"):
                suffix = "PATCH"
            if suffix is None:
                continue
            try:
                parts[suffix] = int(value)
            except ValueError:
                print(f"[gen_plugin_json] Non-integer value for {name}: {value}", file=sys.stderr)
                return None

    if None in parts.values():
        print(f"[gen_plugin_json] Failed to parse MORPHWORX_VERSION_* macros from {VERSION_H}", file=sys.stderr)
        return None

    return f"{parts['MAJOR']}.{parts['MINOR']}.{parts['PATCH']}"


def update_plugin_json(version: str) -> bool:
    if not PLUGIN_JSON.is_file():
        print(f"[gen_plugin_json] plugin.json not found at {PLUGIN_JSON}", file=sys.stderr)
        return False

    try:
        with PLUGIN_JSON.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print(f"[gen_plugin_json] Failed to parse plugin.json: {e}", file=sys.stderr)
        return False

    old_version = data.get("version")
    if old_version != version:
        data["version"] = version
        try:
            with PLUGIN_JSON.open("w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
                f.write("\n")
        except Exception as e:
            print(f"[gen_plugin_json] Failed to write plugin.json: {e}", file=sys.stderr)
            return False
        print(f"[gen_plugin_json] Updated plugin.json version: {old_version!r} -> {version!r}")
    else:
        print(f"[gen_plugin_json] plugin.json already at version {version}")

    return True


def main() -> int:
    version = read_version()
    if version is None:
        return 1
    if not update_plugin_json(version):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
