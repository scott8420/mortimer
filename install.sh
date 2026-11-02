#!/usr/bin/env bash
# install.sh -- put Mortimer on this machine.
#   ./install.sh               into ~/.local  (no sudo)
#   ./install.sh --system      into /usr/local (sudo)
#   ./install.sh --prefix DIR  anywhere
#   ./install.sh --build       build first
# Every file it puts down is listed in <prefix>/share/mortimer/install-manifest.txt,
# which is exactly what uninstall.sh takes away again.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
APP=mortimer
APP_ID=io.github.scott8420.Mortimer
BINS=(mortimer mortimer-gen)
PREFIX="$HOME/.local"
BUILD=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --system) PREFIX=/usr/local ;;
        --prefix) PREFIX="${2:?--prefix needs a directory}"; shift ;;
        --build)  BUILD=1 ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "install.sh: unknown option $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
if [[ $BUILD == 1 ]]; then ./build.sh; fi
for b in "${BINS[@]}"; do
    [[ -x "build/$b" ]] || { echo "build/$b is missing -- run ./build.sh (or ./install.sh --build)" >&2; exit 1; }
done

# sudo only when the prefix isn't ours to write.
SUDO=""
probe="$PREFIX"
while [[ ! -e "$probe" ]]; do probe="$(dirname "$probe")"; done
[[ -w "$probe" ]] || SUDO=sudo

MANIFEST="$PREFIX/share/$APP/install-manifest.txt"
files=()
put() {   # put SRC DEST MODE
    $SUDO install -D -m "$3" "$1" "$2"
    files+=("$2")
    echo "  $2"
}
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

echo "Installing Mortimer into $PREFIX"
for b in "${BINS[@]}"; do put "build/$b" "$PREFIX/bin/$b" 755; done
sed "s#@BINDIR@#$PREFIX/bin#g" "data/$APP_ID.desktop.in" > "$tmp"
put "$tmp" "$PREFIX/share/applications/$APP_ID.desktop" 644
put "data/$APP_ID.svg" "$PREFIX/share/icons/hicolor/scalable/apps/$APP_ID.svg" 644
put "resources/icons/scalable/apps/$APP-logo-symbolic.svg" \
    "$PREFIX/share/icons/hicolor/symbolic/apps/$APP_ID-symbolic.svg" 644
files+=("$MANIFEST")
printf '%s\n' "${files[@]}" > "$tmp"
$SUDO install -D -m 644 "$tmp" "$MANIFEST"
echo "  $MANIFEST"

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    $SUDO gtk-update-icon-cache -q -t -f "$PREFIX/share/icons/hicolor" || true
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    $SUDO update-desktop-database -q "$PREFIX/share/applications" || true
fi
if [[ "$PREFIX" == "$HOME/.local" ]]; then how=""; elif [[ "$PREFIX" == /usr/local ]]; then how=" --system"; else how=" --prefix $PREFIX"; fi
echo "Done. To remove: ./uninstall.sh$how"
