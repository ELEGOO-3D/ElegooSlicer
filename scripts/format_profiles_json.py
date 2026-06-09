#!/usr/bin/env python3
"""
Reformat OrcaSlicer profile JSON files with tab indentation (Orca #13163 style).

Matches save_to_json() output shape: json.dumps(..., indent="\\t") + single trailing newline.
This is the Python equivalent of: jq --tab . < file.json

Usage (from repository root):
  python scripts/format_profiles_json.py
  python scripts/format_profiles_json.py --vendor Elegoo
  python scripts/format_profiles_json.py --path resources/profiles/Elegoo --dry-run
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

BOM = b'\xef\xbb\xbf'


def _check_bom(path):
    with open(path, 'rb') as f:
        if f.read(3) == BOM:
            print(f"Error: {path} contains UTF-8 BOM. "
                  f"Please re-save the file without BOM.",
                  file=sys.stderr)
            return True
    return False


def find_repo_root() -> Path:
    p = Path(__file__).resolve().parent
    return p.parent


def collect_json_files(profiles_root: Path, vendor: str | None) -> list[Path]:
    files: list[Path] = []
    if vendor:
        vendor_root = profiles_root / vendor
        vendor_json = profiles_root / f"{vendor}.json"
        if vendor_json.is_file():
            files.append(vendor_json)
        if not vendor_root.is_dir():
            print(f"[ERROR] vendor folder not found: {vendor_root}", file=sys.stderr)
            sys.exit(1)
        files.extend(vendor_root.rglob("*.json"))
    else:
        files.extend(profiles_root.rglob("*.json"))
    return sorted(set(files))


def reformat_file(path: Path, dry_run: bool) -> tuple[bool, bool]:
    """
    Returns (success, content_would_change_or_changed).
    """
    try:
        if _check_bom(path):
            return False, False
        raw = path.read_text(encoding="utf-8")
        data = json.loads(raw)
        out = json.dumps(data, indent="\t", ensure_ascii=False) + "\n"
        changed = raw != out
        if not dry_run and changed:
            path.write_text(out, encoding="utf-8", newline="\n")
        return True, changed
    except Exception as e:
        print(f"[ERROR] {path}: {e}", file=sys.stderr)
        return False, False


def main() -> int:
    repo = find_repo_root()
    default_profiles = repo / "resources" / "profiles"

    parser = argparse.ArgumentParser(
        description="Tab-format profile JSON (OrcaSlicer resources/profiles)."
    )
    parser.add_argument(
        "-p",
        "--path",
        type=Path,
        default=default_profiles,
        help=f"Profile root directory (default: {default_profiles})",
    )
    parser.add_argument(
        "-v",
        "--vendor",
        metavar="NAME",
        help="Only format vendor NAME (uses <profiles>/<NAME>.json and <profiles>/<NAME>/)",
    )
    parser.add_argument(
        "-n",
        "--dry-run",
        action="store_true",
        help="Do not write; exit 1 if any file would change",
    )
    args = parser.parse_args()

    profiles_root = args.path
    if not profiles_root.is_dir():
        print(f"[ERROR] not a directory: {profiles_root}", file=sys.stderr)
        return 1

    files = collect_json_files(profiles_root, args.vendor)
    if not files:
        print("[WARN] no .json files matched.")
        return 0

    ok = 0
    changed = 0
    for f in files:
        success, file_changed = reformat_file(f, dry_run=args.dry_run)
        if success:
            ok += 1
        if file_changed:
            changed += 1

    if args.dry_run:
        print(
            f"[INFO] dry-run: {changed} of {len(files)} file(s) would change "
            f"({ok} ok, {len(files) - ok} error(s))."
        )
        return 1 if changed else 0

    print(
        f"[OK] wrote {changed} of {len(files)} file(s); "
        f"{len(files) - ok} error(s); {len(files) - changed} unchanged."
    )
    return 0 if ok == len(files) else 1


if __name__ == "__main__":
    sys.exit(main())
