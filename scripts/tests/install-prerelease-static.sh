#!/usr/bin/env bash
set -Eeuo pipefail
IFS=$'\n\t'

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
installer="$repo_root/scripts/install-prerelease.sh"
bash -n "$installer"

# Exercise the installer's actual embedded JSON selector against mocked GitHub
# responses.  This protects newest-prerelease and architecture asset selection
# without adding a production API override.
python3 - "$installer" <<'PY'
import json
import pathlib
import re
import subprocess
import sys
import tempfile

installer = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
match = re.search(r"select_release\(\) \{.*?<<'PY'\n(.*?)\nPY\n\}", installer, re.S)
if match is None:
    raise SystemExit("could not locate embedded release selector")
selector = match.group(1)
repository = "JacobHayes/mosh"
download_root = f"https://github.com/{repository}/releases/download"


def asset(tag, name, asset_id):
    return {
        "name": name,
        "url": f"https://api.github.com/repos/{repository}/releases/assets/{asset_id}",
        "browser_download_url": f"{download_root}/{tag}/{name}",
    }


def release(tag, release_id, *, prerelease=True, draft=False, duplicate_amd64=False):
    stamp = tag.removeprefix("build-")
    date = stamp[0:8]
    time = stamp[9:15]
    sha = stamp[-7:]
    version = f"1.4.0+git{date}.{time}.{sha}-1"
    assets = [
        asset(tag, "SHA256SUMS", release_id * 10),
        asset(tag, f"mosh_{version}_amd64.deb", release_id * 10 + 1),
        asset(tag, f"mosh_{version}_arm64.deb", release_id * 10 + 2),
    ]
    if duplicate_amd64:
        assets.append(asset(tag, f"mosh_{version}_amd64.deb", release_id * 10 + 3))
    return {
        "tag_name": tag,
        "draft": draft,
        "prerelease": prerelease,
        "immutable": True,
        "url": f"https://api.github.com/repos/{repository}/releases/{release_id}",
        "assets": assets,
    }


def run(document, mode="list", arch="amd64"):
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8") as fixture:
        json.dump(document, fixture)
        fixture.flush()
        return subprocess.run(
            [sys.executable, "-c", selector, fixture.name, mode, arch, repository, download_root],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

newest = release("build-20260711T120000Z-abcdef0", 20)
older = release("build-20260710T120000Z-1234567", 10)
draft = release("build-20260712T120000Z-fedcba0", 40, draft=True)
stable = release("mosh-1.4.0", 30, prerelease=False)
result = run([older, draft, stable, newest])
if result.returncode != 0 or not result.stdout.startswith("build-20260711T120000Z-abcdef0\t"):
    raise SystemExit(f"newest matching prerelease was not selected: {result.stderr}")
if not result.stdout.rstrip().endswith("_amd64.deb"):
    raise SystemExit("amd64 selector chose the wrong asset")

result = run([newest], arch="arm64")
if result.returncode != 0 or not result.stdout.rstrip().endswith("_arm64.deb"):
    raise SystemExit(f"arm64 selector chose the wrong asset: {result.stderr}")

result = run(release("build-20260711T120000Z-abcdef0", 20, duplicate_amd64=True), mode="single")
if result.returncode == 0:
    raise SystemExit("duplicate architecture assets were accepted")

mutable = release("build-20260711T120000Z-abcdef0", 20)
mutable["immutable"] = False
result = run(mutable, mode="single")
if result.returncode == 0:
    raise SystemExit("a mutable release was accepted")

bad_url = release("build-20260711T120000Z-abcdef0", 20)
bad_url["assets"][1]["browser_download_url"] = "https://example.com/mosh.deb"
result = run(bad_url, mode="single")
if result.returncode == 0:
    raise SystemExit("an untrusted asset URL was accepted")
PY
