#!/usr/bin/env bash
# scripts/@BIN@_backup.sh -- one zip of the workspace, laid out AS the workspace.
#
#   scripts/@BIN@_backup.sh sNNN_tagline     make backups/sNNN_tagline.zip
#   scripts/@BIN@_backup.sh -l               list the backups
#
# The zip holds @BIN@/ (without build/ or .git/), docs/, scripts/ and an empty
# backups/Old/ -- unzip it at a workspace root and it lands where it belongs.
# fixes/ is never in it, at any depth (it's yours). The zip that was in backups/ moves to
# backups/Old/ first; Old keeps the newest $KEEP.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP=@BIN@
KEEP=40
cd "$ROOT"

if [[ "${1:-}" == "-l" ]]; then
    echo "backups/:"; ls -1t backups/*.zip 2>/dev/null | sed 's/^/  /' || true
    echo "backups/Old/:"; ls -1t backups/Old/*.zip 2>/dev/null | sed 's/^/  /' || true
    exit 0
fi
TAG="${1:-}"
if [[ ! "$TAG" =~ ^s[0-9]{3}[a-z]?_[A-Za-z0-9_]+$ ]]; then
    echo "usage: $0 sNNN_tagline   (e.g. s002_designer_canvas)   |   $0 -l" >&2
    exit 2
fi
command -v zip >/dev/null 2>&1 || { echo "needs zip (sudo dnf install zip)" >&2; exit 1; }
[[ -d "$APP" ]] || { echo "no $APP/ under $ROOT" >&2; exit 1; }
mkdir -p backups/Old
OUT="backups/$TAG.zip"
[[ ! -e "$OUT" ]] || { echo "$OUT already exists" >&2; exit 1; }

shopt -s nullglob
for z in backups/*.zip; do mv -- "$z" backups/Old/; done
mapfile -t old < <(ls -1t backups/Old/*.zip 2>/dev/null || true)
if (( ${#old[@]} > KEEP )); then
    for z in "${old[@]:KEEP}"; do rm -f -- "$z"; done
fi

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/backups/Old"
(cd "$STAGE" && zip -q "$ROOT/$OUT" backups/ backups/Old/)
zip -qr "$OUT" "$APP" docs scripts \
    -x "$APP/build/*" "$APP/build-*/*" "$APP/.git/*" "*/.cache/*" "*/fixes/" "*/fixes/*" \
       "$APP/compile_commands.json" "docs/docs/*" "*.o" "*~"
echo "$OUT  ($(du -h "$OUT" | cut -f1))"
