// selftest_design.cpp -- s002: the design model the window edits (core/Design).
//
// THE GATE, same shape as the stones one: every verb in design::verbs(), on a
// fresh fixture -- nothing written around the door; it is a step; undoing it
// puts the design back exactly; redoing reproduces it; and the form it leaves
// still validates (the designer never hands the generator a broken form).
// Then the edges a window will hit: refusals that say why and record nothing,
// renames that carry refs, delete + undo in place, typing that merges, the
// packing a container chooses.
#include "core/Design.hpp"
#include "core/Catalog.hpp"

#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace core = mortimer::core;
namespace fs = std::filesystem;
using Check = std::function<void(const std::string&, bool, const std::string&)>;
using core::Design;
using core::Value;

namespace {

using Snap = std::vector<std::pair<std::string, core::Rec>>;
Snap snap(const Design& d) {
    Snap out;
    if (auto f = d.get(Design::kForm)) out.emplace_back(Design::kForm, *f);
    for (const auto& k : d.all()) out.emplace_back(k, *d.get(k));
    return out;
}

struct Fix {
    Design d;
    core::DesignJournal j{d};
    std::vector<std::string> sel;
    explicit Fix(const core::Form& f = core::design::fixture()) {
        d.set_door([this] { return j.open(); });
        j.set_selection_source([this] { return sel; });
        d.load(f);
    }
};

std::string first_error(const core::Form& f) {
    for (const auto& i : core::validate(f))
        if (i.error) return i.where + ": " + i.what;
    return {};
}

std::string str(const core::Rec& r, const std::string& k, bool pack = false) {
    const auto& m = pack ? r.pack : r.props;
    const auto it = m.find(k);
    return it != m.end() && std::holds_alternative<std::string>(it->second)
               ? std::get<std::string>(it->second) : std::string{};
}
std::int64_t num(const core::Rec& r, const std::string& k) {
    const auto it = r.pack.find(k);
    return it != r.pack.end() && std::holds_alternative<std::int64_t>(it->second)
               ? std::get<std::int64_t>(it->second) : -1;
}

}  // namespace

void selftest_design(const Check& check_) {
    const auto check = [&](const std::string& n, bool ok, const std::string& d = "") { check_(n, ok, d); };
    // ── load / save round-trips ─────────────────────────────────────────────
    {
        const auto fx = core::design::fixture();
        check("design: the fixture validates", first_error(fx).empty(), first_error(fx));
        Fix f;
        check("design: load is not a step", f.j.size() == 0 && f.d.strays() == 0);
        check("design: fixture round-trips exactly",
              core::form_to_json(f.d.to_form()) == core::form_to_json(fx));
        const fs::path ks = fs::path(MORTIMER_SOURCE_DIR) / "tests" / "KitchenSink.mort";
        try {
            const auto k = core::load_form(ks.string());
            Fix g(k);
            check("design: KitchenSink round-trips exactly",
                  core::form_to_json(g.d.to_form()) == core::form_to_json(k));
        } catch (const std::exception& e) {
            check("design: KitchenSink round-trips exactly", false, e.what());
        }
    }

    // ── THE GATE ────────────────────────────────────────────────────────────
    for (const auto& v : core::design::verbs()) {
        Fix f;
        const auto before = snap(f.d);
        v.run(f.j, f.d);
        const auto after = snap(f.d);
        const auto steps = f.j.size();
        check("design gate/" + v.name + ": through the one door", f.d.strays() == 0,
              std::to_string(f.d.strays()) + " stray write(s)");
        check("design gate/" + v.name + ": is a step", steps > 0 && !(after == before));
        const auto err = first_error(f.d.to_form());
        check("design gate/" + v.name + ": the form still validates", err.empty(), err);
        for (std::size_t i = 0; i < steps; ++i) f.j.undo();
        check("design gate/" + v.name + ": undo puts it back exactly", snap(f.d) == before);
        for (std::size_t i = 0; i < steps; ++i) f.j.redo();
        check("design gate/" + v.name + ": redo reproduces it", snap(f.d) == after);
    }
    {
        Fix f;
        auto r = *f.d.get(f.d.find("ok"));
        r.name = "sneaky";
        f.d.put(f.d.find("ok"), r);
        check("design gate: a write around the door is a stray", f.d.strays() == 1);
    }

    // ── refusals say why and record nothing ─────────────────────────────────
    {
        Fix f;
        std::string why;
        const auto body = f.d.find("body");
        auto refused = [&](const std::string& name, bool did, const std::string& want) {
            check("design refuse: " + name, !did && f.j.size() == 0 && why.find(want) != std::string::npos,
                  "why = \"" + why + "\"");
            why.clear();
        };
        refused("a window inside a Box", !core::design::add(f.j, f.d, "Window", body, -1, &why).empty(),
                "top level");
        refused("a child in a Label", !core::design::add(f.j, f.d, "Button", f.d.find("hint"), -1, &why).empty(),
                "cannot hold");
        refused("an unknown type", !core::design::add(f.j, f.d, "Frobnitz", body, -1, &why).empty(),
                "doesn't know");
        refused("a third child in the window",
                !core::design::add(f.j, f.d, "Label", f.d.root(), -1, &why).empty(), "titlebar");
        refused("deleting the window", core::design::remove(f.j, f.d, f.d.root(), &why), "can't be deleted");
        refused("a Box into its own child", core::design::move(f.j, f.d, body, f.d.find("grid"), 0, &why),
                "inside itself");
        refused("a taken name", core::design::rename(f.j, f.d, f.d.find("ok"), "cancel", &why), "taken");
        refused("a keyword", core::design::rename(f.j, f.d, f.d.find("ok"), "class", &why), "C++ name");
        refused("a bad form name", core::design::rename_form(f.j, f.d, "9lives", &why), "class name");
        check("design refuse: unknown prop", !core::design::set_prop(f.j, f.d, f.d.find("ok"), "frob",
                                                                  Value(true)) && f.j.size() == 0);
        check("design refuse: unknown signal",
              !core::design::tick(f.j, f.d, f.d.find("ok"), "frobbed", true) && f.j.size() == 0);
        check("design refuse: pack key the parent doesn't use",
              !core::design::set_pack(f.j, f.d, f.d.find("ok"), "column", Value(std::int64_t{1})) &&
                  f.j.size() == 0);
        check("design refuse: no-op prop is no step",
              !core::design::set_prop(f.j, f.d, f.d.find("ok"), "label", Value(std::string("OK"))) &&
                  f.j.size() == 0);
        check("design: no strays from refusals", f.d.strays() == 0);
    }

    // ── what the window relies on ───────────────────────────────────────────
    {   // Delete a container: the whole subtree goes; undo brings it back under the same keys, in place.
        Fix f;
        const auto body = f.d.find("body");
        const auto sub = f.d.subtree(body);
        const auto before = snap(f.d);
        f.sel = {body};
        core::design::remove(f.j, f.d, body);
        f.sel = {core::design::neighbour(f.d, body)};
        check("design delete: subtree gone", f.d.count() == before.size() - 1 - sub.size(),
              std::to_string(f.d.count()));
        const auto r = f.j.undo();
        check("design delete+undo: same keys, same place", snap(f.d) == before);
        check("design delete+undo: selection back", r.selection == std::vector<std::string>{body});
        check("design delete: label names it", r.label == "Delete “body”", r.label);
    }
    {   // Rename carries refs: the sidebar follows its stack, one step.
        Fix f;
        core::design::rename(f.j, f.d, f.d.find("pages"), "book");
        const auto side = *f.d.get(f.d.find("side"));
        check("design rename: ref follows", str(side, "stack") == "book", str(side, "stack"));
        check("design rename: one step", f.j.size() == 1);
        f.j.undo();
        check("design rename+undo: ref back", str(*f.d.get(f.d.find("side")), "stack") == "pages");
    }
    {   // Typing a label: keystrokes inside the window fold into one step.
        Fix f;
        const auto ok = f.d.find("ok");
        const auto t0 = core::DesignJournal::Clock::now();
        int i = 0;
        for (const char* s : {"S", "Sa", "Sav", "Save"}) {
            auto r = *f.d.get(ok);
            r.props["label"] = Value(std::string(s));
            f.j.run("label", {ok}, [&] { f.d.put(ok, r); }, "prop:label:" + ok,
                    t0 + std::chrono::milliseconds(200 * i++));
        }
        check("design typing: one step", f.j.size() == 1, std::to_string(f.j.size()));
        f.j.undo();
        check("design typing+undo: back to OK", str(*f.d.get(ok), "label") == "OK");
    }
    {   // Moving into a Grid packs it in the next row; within a Box it keeps its (empty) pack.
        Fix f;
        const auto cancel = f.d.find("cancel");
        core::design::move(f.j, f.d, cancel, f.d.find("grid"), -1);
        const auto r = *f.d.get(cancel);
        check("design move: into Grid -> column 0, next row", num(r, "column") == 0 && num(r, "row") == 1,
              std::to_string(num(r, "column")) + "," + std::to_string(num(r, "row")));
        core::design::move_up(f.j, f.d, f.d.find("hint"));
        const auto kids = f.d.children(f.d.find("body"));
        // (cancel left the Box above, so hint was second; now it leads.)
        check("design move_up: hint now first", !kids.empty() && kids[0] == f.d.find("hint"));
        check("design move_up at the top: refused", !core::design::move_up(f.j, f.d, kids[0]));
    }
    {   // A Paned offers start, then end, then refuses; a HeaderBar child sits at start.
        Fix f;
        const auto p = core::design::add(f.j, f.d, "Paned", f.d.find("body"));
        const auto a = core::design::add(f.j, f.d, "Label", p);
        const auto b = core::design::add(f.j, f.d, "Label", p);
        std::string why;
        const auto c = core::design::add(f.j, f.d, "Label", p, -1, &why);
        check("design Paned: start, end, then no",
              str(*f.d.get(a), "slot", true) == "start" && str(*f.d.get(b), "slot", true) == "end" && c.empty(),
              why);
        const auto hb = core::design::add(f.j, f.d, "Button", f.d.find("bar"));
        check("design HeaderBar: child at start", str(*f.d.get(hb), "slot", true) == "start");
        const auto pg = core::design::add(f.j, f.d, "Label", f.d.find("pages"));
        check("design Stack: a new page is named", str(*f.d.get(pg), "name", true) == "page1" &&
                                                       str(*f.d.get(pg), "title", true) == "Page 1");
        check("design add: fresh names", f.d.get(a)->name == "label1" && f.d.get(b)->name == "label2",
              f.d.get(a)->name + "," + f.d.get(b)->name);
        const auto err = first_error(f.d.to_form());
        check("design: all of that still validates", err.empty(), err);
        check("design: add labels the step", f.j.undo_label() == "Add Label “label3”", f.j.undo_label());
    }
    {   // Require ticks too; unrequire leaves it ticked; untick removes it.
        Fix f;
        const auto c = f.d.find("cancel");
        core::design::require(f.j, f.d, c, "clicked", true);
        auto r = *f.d.get(c);
        check("design require: ticks + requires", r.signals.size() == 1 && r.signals[0].required);
        core::design::require(f.j, f.d, c, "clicked", false);
        r = *f.d.get(c);
        check("design unrequire: still ticked", r.signals.size() == 1 && !r.signals[0].required);
        core::design::tick(f.j, f.d, c, "clicked", false);
        check("design untick: gone", f.d.get(c)->signals.empty());
        check("design signals: three steps", f.j.size() == 3);
    }
}
