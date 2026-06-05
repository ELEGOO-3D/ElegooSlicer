#!/usr/bin/env python3
"""
Upload PDB (Windows) / dSYM (macOS) / .debug (Linux) debug symbols to Sentry.

On first run, sentry-cli is downloaded and cached to tools/sentry-cli/ (gitignored).
Subsequent runs use the cached binary directly.

Usage:
    python upload_sentry_pdbs.py <debug_dir> [version]

Requires .env with: SENTRY_AUTH_TOKEN, SENTRY_ORG, SENTRY_PROJECT
Never blocks the build -- all failures are printed as warnings and exit 0.
"""

import sys
import platform
import subprocess
import urllib.request
from pathlib import Path


SENTRY_CLI_VERSION = "3.4.3"
SENTRY_CLI_BINARIES = {
    "Windows": ("sentry-cli-Windows-x86_64.exe", "sentry-cli.exe"),
    "Darwin":  ("sentry-cli-Darwin-universal",     "sentry-cli"),
    "Linux":   ("sentry-cli-Linux-x86_64",          "sentry-cli"),
}


def find_project_root() -> Path:
    """Find the project root by looking for the .env file upwards."""
    cwd = Path.cwd()
    for candidate in [cwd] + list(cwd.parents):
        if (candidate / ".env").exists():
            return candidate
    return cwd


def parse_dotenv(env_path: Path) -> dict:
    """Parse a .env file, return dict of KEY=VALUE (skip comments/empty lines)."""
    result = {}
    if not env_path.exists():
        return result
    with open(env_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            key, value = key.strip(), value.strip()
            if "#" in value:
                value = value.split("#")[0].strip()
            if len(value) >= 2 and value[0] == value[-1] and value[0] in ('"', "'"):
                value = value[1:-1]
            result[key] = value
    return result


def get_sentry_cli_info():
    """Return (download_filename, local_filename, download_url) for current platform."""
    system = platform.system()
    info = SENTRY_CLI_BINARIES.get(system)
    if not info:
        return None
    dl_name, local_name = info
    url = f"https://github.com/getsentry/sentry-cli/releases/download/{SENTRY_CLI_VERSION}/{dl_name}"
    return dl_name, local_name, url


def find_or_download_sentry_cli(cache_dir: Path):
    """Return path to sentry-cli: use cached copy or download one."""
    info = get_sentry_cli_info()
    if not info:
        print(f"[WARNING] Unsupported platform: {platform.system()}, skipping Sentry upload")
        return None

    _, local_name, url = info
    cache_dir.mkdir(parents=True, exist_ok=True)
    cli_path = cache_dir / local_name

    if cli_path.exists():
        print(f"[INFO] Using cached sentry-cli: {cli_path}")
        _ensure_executable(cli_path)
        return cli_path

    print(f"[INFO] sentry-cli not found, downloading v{SENTRY_CLI_VERSION}...")
    print(f"[INFO] URL: {url}")
    try:
        urllib.request.urlretrieve(url, cli_path)
    except Exception as e:
        print(f"[WARNING] Failed to download sentry-cli: {e}")
        if cli_path.exists():
            cli_path.unlink()
        return None

    file_size = cli_path.stat().st_size
    if file_size < 1_048_576:
        print(f"[WARNING] Downloaded sentry-cli too small ({file_size} bytes), corrupted?")
        cli_path.unlink()
        return None

    _ensure_executable(cli_path)
    print(f"[OK] sentry-cli v{SENTRY_CLI_VERSION} downloaded ({file_size:,} bytes)")
    return cli_path


def _ensure_executable(cli_path: Path):
    """Ensure the binary is executable on Unix."""
    if platform.system() != "Windows":
        cli_path.chmod(0o755)


def find_debug_files(debug_dir: Path) -> list:
    """Find all debug symbol files in the directory (PDB / dSYM / .sym / .debug)."""
    files = []
    for pat in ("*.pdb", "*.dSYM", "*.sym", "*.debug"):
        files.extend(debug_dir.glob(pat))
    if debug_dir.is_dir():
        # dSYM bundles are directories, glob catches them as files — ensure coverage
        for d in debug_dir.iterdir():
            if d.is_dir() and d.suffix == ".dSYM":
                files.append(d)
        for dsym in debug_dir.rglob("*.dSYM"):
            if dsym.is_dir():
                files.append(dsym)
    return sorted(set(files))


def main():
    if len(sys.argv) < 2:
        print("Usage: upload_sentry_pdbs.py <debug_dir> [version]")
        sys.exit(0)

    debug_dir = Path(sys.argv[1])
    version = sys.argv[2] if len(sys.argv) > 2 else "unknown"

    project_root = find_project_root()
    env = parse_dotenv(project_root / ".env")
    if not env:
        env = parse_dotenv(project_root / ".env.testing")
    if not env:
        print("[WARNING] .env not found, skipping Sentry upload")
        sys.exit(0)

    auth_token = env.get("SENTRY_AUTH_TOKEN", "").strip()

    if not auth_token:
        print("[INFO] SENTRY_AUTH_TOKEN not configured in .env, skipping.")
        sys.exit(0)

    system = platform.system()
    project_map = {"Windows": "elegoo-slicer-win", "Darwin": "elegoo-slicer-mac", "Linux": "elegoo-slicer-linux"}
    org = env.get("SENTRY_ORG", "elegoo-uk").strip() or "elegoo-uk"
    project = env.get("SENTRY_PROJECT", "").strip() or project_map.get(system, f"elegoo-slicer-{system.lower()}")

    if not debug_dir.exists():
        print(f"[WARNING] {debug_dir} not found, skipping.")
        sys.exit(0)

    debug_files = find_debug_files(debug_dir)
    if not debug_files:
        print(f"[WARNING] No debug symbols found in {debug_dir}, skipping.")
        sys.exit(0)
    if system == "Darwin" and not any(p.suffix == ".dSYM" for p in debug_files):
        print(f"[WARNING] No .dSYM in {debug_dir}. Rebuild with -g so CMake enables dSYM generation.")
        sys.exit(0)

    cache_dir = project_root / "tools" / "sentry-cli"
    cli_path = find_or_download_sentry_cli(cache_dir)
    if not cli_path:
        sys.exit(0)

    print("=" * 75)
    print("  Sentry Debug Symbol Upload")
    print("=" * 75)
    print(f"  Org:          {org}")
    print(f"  Project:      {project}")
    print(f"  Version:      {version}")
    print(f"  Platform:     {platform.system()} ({platform.machine()})")
    print(f"  Debug dir:    {debug_dir}")
    print(f"  CLI:          {cli_path}")
    print(f"  Files:        {len(debug_files)}")
    for f in debug_files:
        print(f"    - {f.name}")
    print("=" * 75)
    print()

    # Native debug files are matched by UUID/debug id, not --release (not supported on
    # `debug-files upload` in sentry-cli 3.x). Version is logged for traceability only.
    release = version if version.startswith("elegoo-slicer@") else f"elegoo-slicer@{version}"

    print(f"[INFO] Uploading {len(debug_files)} debug symbol file(s) to Sentry (app release tag: {release})...")
    cmd = [
        str(cli_path), "debug-files", "upload",
        "--auth-token", auth_token,
        "--org", org,
        "--project", project,
        "--log-level", "info",
    ]
    # Pass only known debug symbol files, not the whole directory
    cmd.extend(str(f) for f in debug_files)

    try:
        result = subprocess.run(cmd, timeout=1800)
        if result.returncode != 0:
            print(f"\n[WARNING] sentry-cli exited with {result.returncode}, check Sentry dashboard.")
        else:
            print(f"\n[OK] Debug symbols uploaded successfully!")
    except subprocess.TimeoutExpired:
        print(f"[WARNING] sentry-cli timed out after 30 minutes, upload may be incomplete.")
    except Exception:
        print(f"[WARNING] Failed to run sentry-cli, check network and Sentry dashboard.")

    sys.exit(0)


if __name__ == "__main__":
    main()
