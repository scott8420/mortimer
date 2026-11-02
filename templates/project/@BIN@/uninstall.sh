#!/usr/bin/env bash
# uninstall.sh -- take @APP@ off this machine: exactly what install.sh put down.
#   ./uninstall.sh               from ~/.local
#   ./uninstall.sh --system      from /usr/local (sudo)
#   ./uninstall.sh --prefix DIR
#   ./uninstall.sh --purge       also settings, recents and cache -- NEVER your
#                                documents or anything else you made with it
set -euo pipefail
APP=@BIN@
PREFIX="$HOME/.local"
PURGE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --system) PREFIX=/usr/local ;;
        --prefix) PREFIX="${2:?--prefix needs a directory}"; shift ;;
        --purge)  PURGE=1 ;;
        -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
        *) echo "uninstall.sh: unknown option $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
PREFIX="${PREFIX%/}"
MANIFEST="$PREFIX/share/$APP/install-manifest.txt"
SUDO=""
if [[ -f "$MANIFEST" ]]; then
    [[ -w "$(dirname "$MANIFEST")" ]] || SUDO=sudo
    echo "Removing @APP@ from $PREFIX"
    while IFS= read -r f; do
        [[ -n "$f" ]] || continue
        case "$f" in
            "$PREFIX"/*) ;;                                   # inside the prefix: ours
            *) echo "  skipped (outside $PREFIX): $f"; continue ;;
        esac
        if [[ -e "$f" || -L "$f" ]]; then $SUDO rm -f -- "$f"; echo "  $f"; fi
    done < "$MANIFEST"
    $SUDO rmdir --ignore-fail-on-non-empty "$PREFIX/share/$APP" 2>/dev/null || true
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        $SUDO gtk-update-icon-cache -q -t -f "$PREFIX/share/icons/hicolor" || true
    fi
    if command -v update-desktop-database >/dev/null 2>&1; then
        $SUDO update-desktop-database -q "$PREFIX/share/applications" || true
    fi
else
    echo "No install manifest at $MANIFEST -- nothing installed there."
fi

if [[ $PURGE == 1 ]]; then
    # Settings, recents, cache. The data dir is NOT removed wholesale: it is
    # where an app keeps things people made (libraries, documents) -- only the
    # known state files in it go.
    cfg="${XDG_CONFIG_HOME:-$HOME/.config}/$APP"
    cache="${XDG_CACHE_HOME:-$HOME/.cache}/$APP"
    data="${XDG_DATA_HOME:-$HOME/.local/share}/$APP"
    for d in "$cfg" "$cache"; do
        if [[ -d "$d" ]]; then rm -rf -- "$d"; echo "  purged $d"; fi
    done
    for f in recent.json recents.json settings.json state.json window.json; do
        if [[ -f "$data/$f" ]]; then rm -f -- "$data/$f"; echo "  purged $data/$f"; fi
    done
    rmdir --ignore-fail-on-non-empty "$data" 2>/dev/null || true
fi
echo "Done."
