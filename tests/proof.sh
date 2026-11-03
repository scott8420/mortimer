#!/usr/bin/env bash
# tests/proof.sh -- the generator, end to end, on a real compiler.
#
#   tests/proof.sh [WORKDIR]     (default: a temp dir, removed after)
#
# Makes a fresh app with mortimer-gen, adds tests/KitchenSink.mort (EVERY widget,
# property and signal in the catalog), builds it, then walks the hook lifecycle
# and checks the build fails exactly when it should:
#
#   rename a button      -> the old override is STALE: the build must FAIL
#   delete the override  -> builds; the renamed hook is unhooked (logs, once)
#   mark a hook REQUIRED -> MISSING: the build must FAIL (abstract class)
#   write the override   -> builds
#   regenerate again     -> writes nothing (only changes are written)
#   drop a form          -> its generated files are pruned
#   rename two forms     -> your halves follow (class inside); builds
#   ...and your half (include/, src/) is never written by any of it.
#
# Needs: a built mortimer-gen (./build.sh), python3, the gtkmm build deps.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GEN="$HERE/build/mortimer-gen"
[[ -x "$GEN" ]] || { echo "build mortimer-gen first (./build.sh)" >&2; exit 1; }
WORK="${1:-$(mktemp -d)}"
[[ $# -ge 1 ]] || trap 'rm -rf "$WORK"' EXIT
pass=0; fail=0
ok()  { echo "  PASS  $*"; pass=$((pass+1)); }
bad() { echo "  FAIL  $*"; fail=$((fail+1)); }
check() { local what="$1"; shift; if "$@" >/dev/null 2>&1; then ok "$what"; else bad "$what"; fi; }
build() { cmake --build build -j"$(nproc)" > "$WORK/build.log" 2>&1; }
json() {  # json FILE PYTHON-EXPR-ON-j   -- edit a JSON file in place
    python3 - "$1" "$2" <<'PY'
import json, sys
p, expr = sys.argv[1], sys.argv[2]
j = json.load(open(p))
def node(n, i):
    if n["id"] == i: return n
    for c in n.get("children", []):
        r = node(c, i)
        if r: return r
exec(expr)
open(p, "w").write(json.dumps(j, indent=2) + "\n")
PY
}
userhash() { cat include/MainWindow.hpp src/MainWindow.cpp | sha1sum | cut -d' ' -f1; }

echo "proof: $WORK"
rm -rf "$WORK/proofapp"
"$GEN" new ProofApp --in "$WORK" > /dev/null || { echo "new failed"; exit 1; }
cd "$WORK/proofapp/proofapp" || exit 1

echo "== a new app + the kitchen sink"
cp "$HERE/tests/KitchenSink.mort" forms/
json mortimer.json 'j["forms"].append("KitchenSink")'
check "generate"                "$GEN" generate .
# s009: the bundle grows -- an icon, an image, a data file and the stylesheet,
# so the build below compiles the edited xml and generated/Style.cpp for real.
cp resources/icons/scalable/apps/proofapp-logo-symbolic.svg "$WORK/star-symbolic.svg"
cp data/*.svg "$WORK/cover.svg"
check "add an icon"             "$GEN" add icon "$WORK/star-symbolic.svg" .
check "add an image"            "$GEN" add image "$WORK/cover.svg" .
check "add a data file"         "$GEN" add data "$HERE/README.md" .
check "add the stylesheet"      "$GEN" add stylesheet .
check "configure"               cmake -B build
check "builds (every widget, property, signal)" build
check "selftest"                ./build/proofapp_selftest
# s013: the KitchenSink's Note becomes the project's -- its views, rows and hooks
# now use generated/Models' NoteItem through a using; it must still build.
check "share Note (s013)"       "$GEN" share KitchenSink Note .
check "builds with Note shared" build
H0=$(userhash)

echo "== rename hello -> greet, add a save button"
json forms/MainWindow.mort 'node(j["root"],"hello")["id"]="greet"; node(j["root"],"body")["children"].append({"type":"Button","id":"save","props":{"label":"Save"},"signals":[{"signal":"clicked"}]})'
"$GEN" generate . > "$WORK/gen.log"
check "your half untouched by generate" test "$(userhash)" = "$H0"
check "report: on_hello_clicked is STALE"   bash -c "grep -A3 STALE generated/MainWindow.hooks.txt | grep -q on_hello_clicked"
check "report: on_greet_clicked unhooked"   grep -q "unhooked  void on_greet_clicked" generated/MainWindow.hooks.txt
check "report: on_save_clicked unhooked"    grep -q "unhooked  void on_save_clicked" generated/MainWindow.hooks.txt
if build; then bad "a stale override must not build"; else
    check "the build names the stale override" grep -q "on_hello_clicked.*override\|override.*on_hello_clicked\|marked .override., but does not override" "$WORK/build.log"
fi

echo "== delete the stale override"
sed -i '/on_hello_clicked/d' include/MainWindow.hpp
python3 - <<'PY'
import re
p = "src/MainWindow.cpp"; s = open(p).read()
s = re.sub(r"\nvoid MainWindow::on_hello_clicked\(\) \{.*?\n\}\n", "\n", s, flags=re.S)
open(p, "w").write(s)
PY
check "builds"                  build
"$GEN" generate . > /dev/null
check "report: no STALE left"   bash -c '! grep -q STALE generated/MainWindow.hooks.txt'

echo "== mark save REQUIRED"
json forms/MainWindow.mort 'node(j["root"],"save")["signals"][0]["required"]=True'
"$GEN" generate . > /dev/null
check "report: on_save_clicked MISSING"    grep -q "MISSING   void on_save_clicked" generated/MainWindow.hooks.txt
check "generated: pure virtual"            grep -q "virtual void on_save_clicked() = 0;" generated/MainWindowUi.hpp
if build; then bad "a missing REQUIRED hook must not build"; else
    check "the build says the class is abstract" grep -qi "abstract" "$WORK/build.log"
fi

echo "== write the override (the lines hooks.txt offers)"
sed -i 's|^    void on_open_clicked() override;|&\n    void on_save_clicked() override;|' include/MainWindow.hpp
sed -i 's|^}  // namespace proofapp$|void MainWindow::on_save_clicked() {\n    if (auto lg = log::get(log::Area::Ui)) lg->info("MainWindow: save clicked");\n}\n\n&|' src/MainWindow.cpp
check "builds"                  build
"$GEN" generate . > /dev/null
check "report: save hooked"     grep -q "hooked    void on_save_clicked" generated/MainWindow.hooks.txt

echo "== regenerate with nothing changed"
check "writes nothing"          bash -c "'$GEN' generate . | grep -q '^  wrote' && exit 1 || exit 0"

echo "== drop the KitchenSink form"
json mortimer.json 'j["forms"].remove("KitchenSink")'
"$GEN" generate . > "$WORK/gen.log"
check "pruned KitchenSinkUi.cpp"  grep -q "removed    generated/KitchenSinkUi.cpp" "$WORK/gen.log"
check "your KitchenSink files kept" test -f src/KitchenSink.cpp
rm -f include/KitchenSink.hpp src/KitchenSink.cpp
check "builds without it"       build

echo "== rename forms (s021): About -> AboutBox, the main form MainWindow -> Main"
"$GEN" rename form About AboutBox . > /dev/null
"$GEN" rename form MainWindow Main . > /dev/null
"$GEN" generate . > "$WORK/gen.log"
check "MainWindowUi pruned"     grep -q "removed    generated/MainWindowUi.cpp" "$WORK/gen.log"
check "your halves moved"       test -f src/Main.cpp -a -f include/AboutBox.hpp -a ! -f src/MainWindow.cpp
check "the save override kept"  grep -q "void Main::on_save_clicked" src/Main.cpp
check "builds after the renames" build

echo "-----------------------------------------------"
echo "$pass pass / $fail fail"
[[ $fail == 0 ]]
