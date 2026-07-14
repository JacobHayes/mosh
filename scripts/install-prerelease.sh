#!/usr/bin/env bash
# Install an immutable Mosh continuous prerelease on supported Ubuntu systems.
set -Eeuo pipefail
IFS=$'\n\t'

readonly REPOSITORY='JacobHayes/mosh'
readonly API_ROOT="https://api.github.com/repos/${REPOSITORY}"
readonly DOWNLOAD_ROOT="https://github.com/${REPOSITORY}/releases/download"
release_tag=''
tmpdir=''

usage() {
  cat <<'EOF'
Usage: install-prerelease.sh [--release-tag TAG]

Install the newest Mosh continuous prerelease, or the specified immutable tag.
Supported systems: Ubuntu LTS 22.04, 24.04, or 26.04 on amd64 or arm64.
EOF
}

fail() {
  printf 'install-prerelease: %s\n' "$*" >&2
  exit 1
}

cleanup() {
  if [[ -n "$tmpdir" ]]; then
    rm -rf -- "$tmpdir"
  fi
}
trap cleanup EXIT

while (($#)); do
  case "$1" in
    --release-tag)
      (($# >= 2)) || fail '--release-tag requires a value'
      [[ -z "$release_tag" ]] || fail '--release-tag may be specified only once'
      release_tag=$2
      shift 2
      ;;
    --release-tag=*)
      [[ -z "$release_tag" ]] || fail '--release-tag may be specified only once'
      release_tag=${1#*=}
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *) fail "unknown argument: $1" ;;
  esac
done

readonly TAG_PATTERN='^build-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{7}$'
if [[ -n "$release_tag" && ! "$release_tag" =~ $TAG_PATTERN ]]; then
  fail "invalid release tag: $release_tag"
fi

[[ -r /etc/os-release ]] || fail 'cannot read /etc/os-release'
# shellcheck disable=SC1091
source /etc/os-release
[[ "${ID:-}" == ubuntu ]] || fail 'this installer supports Ubuntu only'
[[ "${VERSION_ID:-}" =~ ^[0-9]+\.[0-9]+$ ]] || fail 'cannot parse Ubuntu VERSION_ID'

for command in apt-get curl dpkg dpkg-deb dpkg-query python3 sha256sum; do
  command -v "$command" >/dev/null || fail "required command not found: $command"
done
case "$VERSION_ID" in
  22.04 | 24.04 | 26.04) ;;
  *) fail 'supported Ubuntu LTS releases are 22.04, 24.04, and 26.04' ;;
esac

arch=$(dpkg --print-architecture)
case "$arch" in
  amd64 | arm64) ;;
  *) fail "unsupported architecture: $arch" ;;
esac

tmpdir=$(mktemp -d)
CURL_ARGS=(
  --fail --show-error --silent --location
  --proto '=https' --tlsv1.2
  --retry 3 --retry-all-errors
  --header 'Accept: application/vnd.github+json'
  --header 'X-GitHub-Api-Version: 2022-11-28'
)
if [[ -n "${GITHUB_TOKEN:-}" ]]; then
  CURL_ARGS+=(--header "Authorization: Bearer ${GITHUB_TOKEN}")
fi
readonly -a CURL_ARGS

# Select and validate one release.  Exit 3 means that this page contains no
# matching prerelease; every malformed matching release is a hard failure.
select_release() {
  local json_file=$1
  local mode=$2
  python3 - "$json_file" "$mode" "$arch" "$REPOSITORY" "$DOWNLOAD_ROOT" <<'PY'
import datetime
import json
import re
import sys
from urllib.parse import unquote, urlsplit

json_file, mode, arch, repository, download_root = sys.argv[1:]
with open(json_file, encoding="utf-8") as stream:
    document = json.load(stream)

if mode == "list":
    if not isinstance(document, list):
        raise SystemExit("GitHub releases response is not a JSON array")
    candidates = document
else:
    if not isinstance(document, dict):
        raise SystemExit("GitHub release response is not a JSON object")
    candidates = [document]

tag_re = re.compile(r"^build-(\d{8})T(\d{6})Z-([0-9a-f]{7})$")
name_re = re.compile(rf"^mosh_([0-9]+(?:\.[0-9]+)*\+git\d{{8}}\.\d{{6}}\.[0-9a-f]{{7}}-1)_{arch}\.deb$")
api_release_re = re.compile(rf"^https://api\.github\.com/repos/{re.escape(repository)}/releases/[0-9]+$")
api_asset_re = re.compile(rf"^https://api\.github\.com/repos/{re.escape(repository)}/releases/assets/[0-9]+$")

if any(not isinstance(release, dict) for release in candidates):
    raise SystemExit("release entry is not an object")
if mode == "list":
    candidates.sort(key=lambda release: str(release.get("tag_name", "")), reverse=True)

for release in candidates:
    tag = release.get("tag_name")
    match = tag_re.fullmatch(tag) if isinstance(tag, str) else None
    if match is not None:
        try:
            datetime.datetime.strptime("".join(match.groups()[:2]), "%Y%m%d%H%M%S")
        except ValueError as error:
            raise SystemExit(f"release has an invalid tag timestamp: {tag}") from error
    if (release.get("draft") is not False or release.get("prerelease") is not True
            or release.get("immutable") is not True or match is None):
        if mode == "single":
            raise SystemExit("requested release is not a matching non-draft prerelease")
        continue

    if not api_release_re.fullmatch(str(release.get("url", ""))):
        raise SystemExit("release has an unexpected API URL")
    assets = release.get("assets")
    if not isinstance(assets, list):
        raise SystemExit("release assets are not an array")
    sums = [asset for asset in assets if isinstance(asset, dict) and asset.get("name") == "SHA256SUMS"]
    debs = [asset for asset in assets if isinstance(asset, dict) and name_re.fullmatch(str(asset.get("name", "")))]
    if len(sums) != 1 or len(debs) != 1:
        raise SystemExit(f"release {tag} must contain exactly one SHA256SUMS and one {arch} package")

    tag_date, tag_time, tag_sha = match.groups()
    deb_match = name_re.fullmatch(debs[0]["name"])
    version = deb_match.group(1)
    expected_suffix = f"+git{tag_date}.{tag_time}.{tag_sha}-1"
    if not version.endswith(expected_suffix):
        raise SystemExit("package version does not correspond to the release tag")

    fields = [tag, version]
    for asset in (sums[0], debs[0]):
        name = asset["name"]
        api_url = str(asset.get("url", ""))
        url = str(asset.get("browser_download_url", ""))
        if not api_asset_re.fullmatch(api_url):
            raise SystemExit(f"asset {name} has an unexpected API URL")
        parsed = urlsplit(url)
        expected_path = f"/{repository}/releases/download/{tag}/{name}"
        if parsed.scheme != "https" or parsed.netloc != "github.com" or unquote(parsed.path) != expected_path or parsed.query or parsed.fragment:
            raise SystemExit(f"asset {name} has an unexpected download URL")
        fields.extend((name, url))
    print("\t".join(fields))
    raise SystemExit(0)

raise SystemExit(3)
PY
}

selection=''
if [[ -n "$release_tag" ]]; then
  api_url="${API_ROOT}/releases/tags/${release_tag}"
  curl "${CURL_ARGS[@]}" "$api_url" --output "$tmpdir/release.json"
  selection=$(select_release "$tmpdir/release.json" single) || fail "release validation failed for $release_tag"
else
  page=1
  best_selection=''
  while :; do
    api_url="${API_ROOT}/releases?per_page=100&page=${page}"
    curl "${CURL_ARGS[@]}" "$api_url" --output "$tmpdir/releases.json"
    set +e
    page_selection=$(select_release "$tmpdir/releases.json" list)
    status=$?
    set -e
    if ((status == 0)); then
      page_tag=${page_selection%%$'\t'*}
      best_tag=${best_selection%%$'\t'*}
      if [[ -z "$best_selection" || "$page_tag" > "$best_tag" ]]; then
        best_selection=$page_selection
      fi
    elif ((status != 3)); then
      fail "release validation failed on API page $page"
    fi
    count=$(python3 - "$tmpdir/releases.json" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as stream:
    value = json.load(stream)
if not isinstance(value, list):
    raise SystemExit("GitHub releases response is not a JSON array")
print(len(value))
PY
    )
    ((count == 100)) || break
    ((page++))
  done
  [[ -n "$best_selection" ]] || fail 'no matching continuous prerelease was found'
  selection=$best_selection
fi

IFS=$'\t' read -r selected_tag expected_version sums_name sums_url deb_name deb_url <<<"$selection"
[[ "$selected_tag" =~ $TAG_PATTERN ]] || fail 'release selector returned an invalid tag'
if [[ -n "$release_tag" && "$selected_tag" != "$release_tag" ]]; then
  fail 'GitHub API returned a different tag than requested'
fi
[[ "$sums_name" == SHA256SUMS ]] || fail 'release selector returned an invalid checksum asset'
[[ "$deb_name" =~ ^mosh_[0-9A-Za-z.+~-]+_${arch}\.deb$ ]] || fail 'release selector returned an invalid package name'

curl "${CURL_ARGS[@]}" "$sums_url" --output "$tmpdir/$sums_name"
curl "${CURL_ARGS[@]}" "$deb_url" --output "$tmpdir/$deb_name"

expected_checksum=$(python3 - "$tmpdir/$sums_name" "$deb_name" <<'PY'
import re
import sys

path, wanted = sys.argv[1:]
matches = []
with open(path, encoding="ascii") as stream:
    for line_number, line in enumerate(stream, 1):
        line = line.rstrip("\n")
        match = re.fullmatch(r"([0-9a-f]{64}) ([ *])([^/\x00]+)", line)
        if match is None:
            raise SystemExit(f"malformed SHA256SUMS line {line_number}")
        if match.group(3) == wanted:
            matches.append(match.group(1))
if len(matches) != 1:
    raise SystemExit(f"expected exactly one checksum for {wanted}, found {len(matches)}")
print(matches[0])
PY
) || fail 'checksum manifest validation failed'
actual_checksum=$(sha256sum "$tmpdir/$deb_name")
actual_checksum=${actual_checksum%% *}
[[ "$actual_checksum" == "$expected_checksum" ]] || fail "checksum mismatch for $deb_name"

package_name=$(dpkg-deb --field "$tmpdir/$deb_name" Package)
package_version=$(dpkg-deb --field "$tmpdir/$deb_name" Version)
package_arch=$(dpkg-deb --field "$tmpdir/$deb_name" Architecture)
[[ "$package_name" == mosh ]] || fail "unexpected package name: $package_name"
[[ "$package_version" == "$expected_version" ]] || fail "unexpected package version: $package_version"
[[ "$package_arch" == "$arch" ]] || fail "unexpected package architecture: $package_arch"
[[ "$deb_name" == "mosh_${package_version}_${package_arch}.deb" ]] || fail 'package filename does not match its metadata'

installed_version=''
if installed_version=$(dpkg-query --show --showformat='${Version}' mosh 2>/dev/null); then
  if [[ "$package_version" == "$installed_version" ]]; then
    printf 'mosh %s is already installed.\n' "$package_version"
    exit 0
  fi
  if dpkg --compare-versions "$package_version" lt "$installed_version"; then
    fail "refusing downgrade from $installed_version to $package_version"
  fi
fi

printf 'Installing mosh %s (%s) from %s\n' "$package_version" "$package_arch" "$selected_tag"
if ((EUID == 0)); then
  apt-get install -y "$tmpdir/$deb_name"
else
  command -v sudo >/dev/null || fail 'sudo is required when not running as root'
  sudo apt-get install -y "$tmpdir/$deb_name"
fi
printf 'Installed mosh %s successfully.\n' "$package_version"
