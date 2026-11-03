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
#include "core/Project.hpp"
#include "core/Resources.hpp"

#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
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

    // ── s003: actions -- rename and delete carry the menu rows ──────────────
    {
        Fix f;
        const auto menu = f.d.find("menu");
        auto rows = [&] { return std::get<core::StringList>(f.d.get(menu)->props.at("menu")); };
        core::design::set_action(f.j, f.d, "save", {"store", "<Control>s", false});
        check("action rename: the menu row follows", rows().front() == "Save=win.store", rows().front());
        check("action rename: one step", f.j.size() == 1);
        f.j.undo();
        check("action rename+undo: row back", rows().front() == "Save=win.save");
        core::design::remove_action(f.j, f.d, "save");
        check("action delete: its row goes too", rows().size() == 2 && rows().front() == "-");
        f.j.undo();
        check("action delete+undo: row back in place", rows().front() == "Save=win.save" && rows().size() == 3);
        std::string why;
        check("action: a duplicate is refused", core::design::add_action(f.j, f.d, "save", &why).empty() &&
                                                why.find("already") != std::string::npos, why);
        check("action: auto-named", core::design::add_action(f.j, f.d) == "action1");
        check("gio name: save_as -> save-as", core::gio_action_name("save_as") == "save-as");
        auto bad = f.d.to_form();
        bad.actions.push_back({"x", "<Control", false});
        check("validate: a broken key is refused", !first_error(bad).empty(), first_error(bad));
    }

    // ── s011: a ListView's row in the design ────────────────────────────────
    {
        core::Form lf;
        lf.name = "Lists";
        lf.models = {{"Task", {{"title", "string", ""}, {"done", "bool", ""}}}};
        core::Node t{"Label", "t_title", {{"bind", Value(std::string("title"))}}, {}, {}, {}};
        core::Node c{"CheckButton", "t_done", {{"bind", Value(std::string("done"))}}, {}, {}, {}};
        core::Node row{"Box", "t_row", {}, {}, {}, {t, c}};
        core::Node view{"ListView", "tasks", {{"model", Value(std::string("Task"))}}, {}, {}, {row}};
        core::Node empty_view{"ListView", "spare", {}, {}, {}, {}};
        core::Node body{"Box", "body", {}, {}, {}, {view, empty_view}};
        lf.root = {"Window", "lists", {}, {}, {}, {body}};
        Fix f(lf);
        check("s011 design: the fixture is valid", first_error(lf).empty(), first_error(lf));
        const auto v = f.d.find("tasks"), r = f.d.find("t_row"), sp = f.d.find("spare");
        check("s011 can_hold: a view holds ONE row", !core::design::can_hold(f.d, v, "Box").empty(), "");
        check("s011 can_hold: an empty view takes its row", core::design::can_hold(f.d, sp, "Box").empty(), "");
        check("s011 can_hold: a row takes a Label", core::design::can_hold(f.d, r, "Label").empty(), "");
        check("s011 can_hold: a row refuses a ListBox / a ColumnView / a Stack",
              !core::design::can_hold(f.d, r, "ListBox").empty() && !core::design::can_hold(f.d, r, "ColumnView").empty() &&
              !core::design::can_hold(f.d, r, "Stack").empty(), "");
        check("s011 can_hold: an empty view refuses a Paned as its row",
              !core::design::can_hold(f.d, sp, "Paned").empty(), "");

        // A field renamed carries the row's binds, same step; removed -> unbound.
        auto m = f.d.to_form().models.front();
        m.fields[0].name = "name";
        check("s011 rename field: done", core::design::set_model(f.j, f.d, "Task", m), "");
        check("s011 rename field: the bind follows, the form stays valid",
              str(*f.d.get(f.d.find("t_title")), "bind") == "name" && first_error(f.d.to_form()).empty(),
              first_error(f.d.to_form()));
        f.j.undo();
        check("s011 rename field: undo puts the bind back", str(*f.d.get(f.d.find("t_title")), "bind") == "title", "");
        m = f.d.to_form().models.front();
        m.fields.erase(m.fields.begin() + 1);   // drop "done"
        core::design::set_model(f.j, f.d, "Task", m);
        check("s011 remove field: its bind goes, still valid",
              !f.d.get(f.d.find("t_done"))->props.count("bind") && first_error(f.d.to_form()).empty(),
              first_error(f.d.to_form()));
        std::string why;
        check("s011 delete model: refused while a ListView shows it",
              !core::design::remove_model(f.j, f.d, "Task", &why) && why.find("ListView tasks") != std::string::npos, why);
        m = f.d.to_form().models.front();
        m.name = "Job";
        core::design::set_model(f.j, f.d, "Task", m);
        check("s011 rename model: the ListView follows", str(*f.d.get(v), "model") == "Job" &&
              first_error(f.d.to_form()).empty(), first_error(f.d.to_form()));
    }

    // ── s013: the project's models in the design ────────────────────────────
    {
        Fix f;
        const core::ModelDef task{"Task", {{"title", "string", ""}}};
        f.d.set_shared({task});
        const auto all = f.d.all_models();
        check("s013 design: all_models = the form's own, then the shared",
              !all.empty() && all.back().name == "Task" && all.size() == f.d.to_form().models.size() + 1, "");
        check("s013 design: to_form carries the shared (validate sees them)",
              f.d.to_form().shared.size() == 1 && f.j.size() == 0, "");
        check("s013 design: setting them isn't a step", f.d.strays() == 0, std::to_string(f.d.strays()));
    }

    // ── s010: actions -- key / title typing folds per field; opens never ────
    {
        Fix f;
        auto cur = [&] { return f.d.to_form().actions.front(); };
        for (const char* t : {"S", "Sa", "Save it"}) {
            auto x = cur(); x.title = t;
            core::design::set_action(f.j, f.d, "save", x);
        }
        check("action title typing: one step", f.j.size() == 1, std::to_string(f.j.size()));
        { auto x = cur(); x.opens = core::kShortcutsTarget; core::design::set_action(f.j, f.d, "save", x); }
        check("action opens: its own step", f.j.size() == 2, std::to_string(f.j.size()));
        for (const char* k : {"<Control>", "<Control>k"}) {
            auto x = cur(); x.accel = k;
            core::design::set_action(f.j, f.d, "save", x);
        }
        check("action key typing: one step, not folded into opens", f.j.size() == 3, std::to_string(f.j.size()));
        { auto x = cur(); x.title = "Save it!"; core::design::set_action(f.j, f.d, "save", x); }
        check("action: title after the key is a new step", f.j.size() == 4, std::to_string(f.j.size()));
        f.j.undo(); f.j.undo();
        check("action undo x2: key back, opens kept", cur().accel == "<Control>s" &&
              cur().opens == core::kShortcutsTarget && cur().title == "Save it");
        f.j.undo();
        check("action undo x3: runs its hook again", cur().opens.empty() && cur().title == "Save it");
        std::string why;
        { auto x = cur(); x.opens = "not a form";
          check("action: opens a non-name refused", !core::design::set_action(f.j, f.d, "save", x, &why) &&
                why.find("isn't a form") != std::string::npos, why); }
    }

    // ── s005: file dialogs -- typing folds per field, a tick or mode never ──
    {
        Fix f;
        auto cur = [&] { return f.d.to_form().files.front(); };
        for (const char* t : {"P", "Pi", "Pick"}) {
            auto x = cur(); x.title = t;
            core::design::set_file(f.j, f.d, "open_doc", x);
        }
        check("file typing: a title is one step", f.j.size() == 1, std::to_string(f.j.size()));
        { auto x = cur(); x.mode = "save"; core::design::set_file(f.j, f.d, "open_doc", x); }
        check("file mode: its own step (not folded into the typing)", f.j.size() == 2, std::to_string(f.j.size()));
        { auto x = cur(); x.filters = {"A=*.a"}; core::design::set_file(f.j, f.d, "open_doc", x); }
        { auto x = cur(); x.cancel = true; core::design::set_file(f.j, f.d, "open_doc", x); }
        check("file filters + tick: two more steps", f.j.size() == 4, std::to_string(f.j.size()));
        f.j.undo(); f.j.undo(); f.j.undo();
        check("file undo x3: the title stays, the mode is back", cur().title == "Pick" && cur().mode == "open");
        f.j.undo();
        check("file undo x4: the title is gone in one", cur().title.empty());
        std::string why;
        check("file: a duplicate is refused", core::design::add_file(f.j, f.d, "open_doc", &why).empty() &&
                                              why.find("already") != std::string::npos, why);
        check("file: auto-named", core::design::add_file(f.j, f.d) == "file1");
        { auto x = cur(); x.name = "file1";
          check("file rename onto a taken name: refused",
                !core::design::set_file(f.j, f.d, "open_doc", x, &why) && why.find("already") != std::string::npos, why); }
        { auto x = cur(); x.mode = "browse";
          check("file: an unknown mode is refused", !core::design::set_file(f.j, f.d, "open_doc", x, &why), why); }
        core::design::remove_file(f.j, f.d, "open_doc");
        check("file delete", f.d.to_form().files.size() == 1);
        f.j.undo();
        check("file delete+undo: back, in place", f.d.to_form().files.size() == 2 &&
                                                  f.d.to_form().files.front().name == "open_doc");
    }

    // ── s006: models -- a rename carries the views; a shown model can't go ──
    {
        auto fx = core::design::fixture();
        fx.root.children[1].children.push_back({"ColumnView", "table", {{"model", Value(std::string("Note"))}}, {}, {}, {}});
        Fix f(fx);
        const auto table = f.d.find("table");
        auto model_of = [&] { return std::get<std::string>(f.d.get(table)->props.at("model")); };
        core::design::set_model(f.j, f.d, "Note", {"Task", {{"title", "string", ""}, {"done", "bool", ""}}});
        check("model rename: the view follows, one step", model_of() == "Task" && f.j.size() == 1, model_of());
        check("model rename: still validates", first_error(f.d.to_form()).empty(), first_error(f.d.to_form()));
        f.j.undo();
        check("model rename+undo: view back", model_of() == "Note");
        std::string why;
        check("model delete while shown: refused, says which",
              !core::design::remove_model(f.j, f.d, "Note", &why) && why.find("table") != std::string::npos, why);
        check("model: auto-named, with a field", core::design::add_model(f.j, f.d) == "Model1" &&
                                                 f.d.to_form().models.back().fields.size() == 1);
        check("model: a new one validates", first_error(f.d.to_form()).empty(), first_error(f.d.to_form()));
        for (const char* t : {"N", "Na", "Name"}) {
            auto m = f.d.to_form().models.front();
            m.fields[0].title = t;
            core::design::set_model(f.j, f.d, "Note", m);
        }
        check("model typing: a header is one step", f.j.size() == 2, std::to_string(f.j.size()));
        { auto m = f.d.to_form().models.front(); m.fields[0].type = "date";
          check("model: an unknown field type is refused", !core::design::set_model(f.j, f.d, "Note", m, &why), why); }
    }

    // ── s007: a field renamed / removed carries the views' "editable" ───────
    {
        auto fx = core::design::fixture();
        fx.root.children[1].children.push_back({"ColumnView", "table",
            {{"model", Value(std::string("Note"))}, {"editable", Value(core::StringList{"title"})}}, {}, {}, {}});
        Fix f(fx);
        const auto table = f.d.find("table");
        auto editable = [&] { return core::view_editable(f.d.to_form().root.children[1].children.back()); };
        auto m = f.d.to_form().models.front();
        const std::string first = m.fields[0].name;
        check("edit carry: fixture edits the first field", first == "title" && editable() == core::StringList{"title"});
        m.fields[0].name = "name";
        core::design::set_model(f.j, f.d, "Note", m);
        check("edit carry: a field rename follows, one step", editable() == core::StringList{"name"} && f.j.size() == 1);
        check("edit carry: still validates", first_error(f.d.to_form()).empty(), first_error(f.d.to_form()));
        f.j.undo();
        check("edit carry+undo: back", editable() == core::StringList{"title"});
        for (const char* t : {"", "n", "na"}) {   // select-all + type passes through ""
            auto k = f.d.to_form().models.front();
            k.fields[0].name = t;
            core::design::set_model(f.j, f.d, "Note", k);
        }
        check("edit carry: typing through an empty name keeps the tick", editable() == core::StringList{"na"},
              editable().empty() ? "(none)" : editable()[0]);
        f.j.undo();
        { auto k = f.d.to_form().models.front();
          k.fields.erase(k.fields.begin());
          if (k.fields.empty()) k.fields.push_back({"other", "string", ""});
          core::design::set_model(f.j, f.d, "Note", k);
          check("edit carry: a removed field leaves editable", editable().empty() &&
                                                                !f.d.get(table)->props.count("editable"));
          check("edit carry: removal still validates", first_error(f.d.to_form()).empty(), first_error(f.d.to_form()));
          f.j.undo();
          check("edit carry: removal+undo: back", editable() == core::StringList{"title"}); }
        // m2: the inspector's checks call set_prop unfolded -- two quick ticks, two steps.
        core::design::set_prop(f.j, f.d, table, "editable", Value(core::StringList{"done"}), false);   // drops the redo tail
        const auto n0 = f.j.size();
        core::design::set_prop(f.j, f.d, table, "editable", Value(core::StringList{"title", "done"}), false);
        core::design::set_prop(f.j, f.d, table, "editable", Value(core::StringList{"done"}), false);
        check("edit ticks: unfolded, each its own step", f.j.size() == n0 + 2, std::to_string(f.j.size() - n0));
        core::design::set_prop(f.j, f.d, table, "selection", Value(std::string("multiple")));
        core::design::set_prop(f.j, f.d, table, "selection", Value(std::string("none")));
        check("selection: a dropdown still folds (one step)", f.j.size() == n0 + 3, std::to_string(f.j.size() - n0));
    }

    // ── s002 m4: where a drop lands ─────────────────────────────────────────
    {
        using core::design::Zone;
        namespace dz = core::design;
        check("zone: container quarters", dz::zone_at(2, 40, true, false) == Zone::Before &&
                                          dz::zone_at(20, 40, true, false) == Zone::Inside &&
                                          dz::zone_at(38, 40, true, false) == Zone::After);
        check("zone: leaf halves", dz::zone_at(10, 40, false, false) == Zone::Before &&
                                   dz::zone_at(30, 40, false, false) == Zone::After);
        check("zone: root is only inside; zero height is after",
              dz::zone_at(1, 40, true, true) == Zone::Inside && dz::zone_at(1, 0, true, false) == Zone::After);
        Fix f;
        const auto ok = f.d.find("ok"), cancel = f.d.find("cancel"), body = f.d.find("body"),
                   grid = f.d.find("grid"), hint = f.d.find("hint"), pages = f.d.find("pages");
        auto l = dz::landing(f.d, "Button", cancel, ok, Zone::Before);
        check("landing: cancel before ok -> body, 0", l.parent == body && l.index == 0, l.why);
        l = dz::landing(f.d, "Button", ok, hint, Zone::After);
        check("landing: ok after hint -> index counted without ok", l.parent == body && l.index == 2,
              std::to_string(l.index));
        l = dz::landing(f.d, "Box", body, grid, Zone::Inside);
        check("landing: a Box into its own child -> refused", l.parent.empty() && l.why.find("itself") != std::string::npos, l.why);
        l = dz::landing(f.d, "Button", ok, ok, Zone::Inside);
        check("landing: onto itself -> refused", l.parent.empty());
        l = dz::landing(f.d, "Button", "", hint, Zone::Inside);
        check("landing: into a Label -> refused (says why)", l.parent.empty() && l.why.find("cannot hold") != std::string::npos, l.why);
        l = dz::landing(f.d, "Label", ok, pages, Zone::Inside);
        check("landing: into a Stack -> allowed", l.parent == pages);
        l = dz::landing(f.d, "Label", "", f.d.root(), Zone::Inside);
        check("landing: a third thing in the window -> refused", l.parent.empty());
        l = dz::landing_on(f.d, "Button", "", hint);
        check("landing_on: a Label -> just after it", l.parent == body && l.index == 3, std::to_string(l.index));
        l = dz::landing_on(f.d, "Entry", "", grid);
        check("landing_on: a Grid -> into it", l.parent == grid);
        // s017 m2: the canvas's zones (pos along the parent's axis, of 40).
        Zone used = Zone::Inside;
        l = dz::landing_at(f.d, "Button", "", hint, 5, 40, &used);
        check("landing_at: a leaf's first half -> before it", l.parent == body && l.index == 2 && used == Zone::Before,
              std::to_string(l.index));
        l = dz::landing_at(f.d, "Button", "", hint, 35, 40, &used);
        check("landing_at: a leaf's second half -> after it", l.parent == body && l.index == 3 && used == Zone::After,
              std::to_string(l.index));
        l = dz::landing_at(f.d, "Entry", "", grid, 20, 40, &used);
        check("landing_at: a container's middle -> into it", l.parent == grid && used == Zone::Inside);
        l = dz::landing_at(f.d, "Entry", "", grid, 2, 40, &used);
        check("landing_at: a container's top quarter -> before it", l.parent == body && l.index == 5 && used == Zone::Before,
              std::to_string(l.index));
        l = dz::landing_at(f.d, "Button", cancel, ok, 1, 40, &used);
        check("landing_at: cancel onto ok's first half -> first", l.parent == body && l.index == 0, std::to_string(l.index));
        l = dz::landing_at(f.d, "Label", "", f.d.root(), 1, 40, &used);
        check("landing_at: the window -> into or nothing (refused, full)", l.parent.empty());
        l = dz::landing_at(f.d, "Button", "", f.d.find("menu"), 2, 40, &used);
        check("landing_at: a spot that refuses falls back to into-or-after",
              l.parent == dz::landing_on(f.d, "Button", "", f.d.find("menu")).parent, l.why);
        // ...and the verb takes the landing as given.
        const auto r = dz::landing(f.d, "Button", cancel, ok, Zone::Before);
        dz::move(f.j, f.d, cancel, r.parent, r.index);
        check("landing -> move: cancel now first", f.d.children(body).front() == cancel);
    }

    // ── s018 m1: several selected ────────────────────────────────────────────
    {
        Fix f;
        namespace dz = core::design;
        const auto ok = f.d.find("ok"), cancel = f.d.find("cancel"), hint = f.d.find("hint"),
                   body = f.d.find("body"), grid = f.d.find("grid"), cell = f.d.find("cell"),
                   root = f.d.root();
        auto t = dz::top_level(f.d, {hint, cell, grid, root, "nope", ok});
        check("top_level: tree order, window + unknown dropped, cell goes with grid",
              t == std::vector<std::string>{ok, hint, grid});
        auto r = dz::range(f.d, hint, ok);
        check("range: either way round, both ends", r == std::vector<std::string>{ok, cancel, hint});
        check("range: one end alone", dz::range(f.d, ok, ok) == std::vector<std::string>{ok});
        r = dz::range(f.d, root, cancel);
        check("range: the window left out of a span", !r.empty() && r.front() != root && r.back() == cancel);
        auto s = dz::toggle({ok}, hint);
        check("toggle: adding makes it primary", s == std::vector<std::string>{hint, ok});
        s = dz::toggle(s, hint);
        check("toggle: taking the primary out hands it on", s == std::vector<std::string>{ok});
        check("toggle: the last one stays", dz::toggle({ok}, ok) == std::vector<std::string>{ok});
        check("survivor: past the deleted run", dz::survivor(f.d, {ok, cancel}) == hint);
        check("survivor: the last ones -> an earlier sibling",
              dz::survivor(f.d, {grid}) == f.d.find("pages"));
        const auto before = f.d.all().size();
        std::string why;
        check("remove_many: three (and a child of one) in one step",
              dz::remove_many(f.j, f.d, {ok, hint, grid, cell}, &why) && f.d.all().size() == before - 4, why);
        check("remove_many: the step is named", f.j.undo_label() == "Delete 3 widgets", f.j.undo_label());
        f.j.undo();
        check("remove_many: one undo brings all back, in place",
              f.d.all().size() == before && f.d.children(body).front() == ok && f.d.get(cell) &&
              f.d.get(cell)->parent == grid);
        check("remove_many: the window alone refused",
              !dz::remove_many(f.j, f.d, {root}, &why) && why.find("window") != std::string::npos, why);
    }

    // ── s019 m1: a drag that carries several ──────────────────────────────────
    {
        Fix f;
        namespace dz = core::design;
        using core::design::Zone;
        const auto ok = f.d.find("ok"), cancel = f.d.find("cancel"), hint = f.d.find("hint"),
                   body = f.d.find("body"), grid = f.d.find("grid"), cell = f.d.find("cell"),
                   pages = f.d.find("pages"), side = f.d.find("side");
        check("keys: join / split", dz::split_keys(dz::join_keys({ok, hint})) == std::vector<std::string>{ok, hint} &&
                                    dz::split_keys("a,,b,") == std::vector<std::string>{"a", "b"});
        // body: ok, cancel, hint, side, pages, grid. ok + hint after grid.
        auto l = dz::landing_many(f.d, {hint, ok}, grid, Zone::After);
        check("landing_many: after the last -> index counted without them", l.parent == body && l.index == 4,
              std::to_string(l.index) + " " + l.why);
        l = dz::landing_many(f.d, {ok, grid}, cell, Zone::Before);
        check("landing_many: into one of their own -> refused", l.parent.empty() && l.why.find("itself") != std::string::npos, l.why);
        l = dz::landing_many(f.d, {ok, hint}, hint, Zone::Before);
        check("landing_many: onto one of them -> refused", l.parent.empty());
        std::string why;
        check("move_many: ok + hint to the end, one step",
              dz::move_many(f.j, f.d, {hint, ok}, body, 4, &why), why);
        check("move_many: their order kept, side by side",
              f.d.children(body) == std::vector<std::string>{cancel, side, pages, grid, ok, hint});
        check("move_many: the step is named", f.j.undo_label() == "Move 2 widgets", f.j.undo_label());
        f.j.undo();
        check("move_many: one undo puts both back",
              f.d.children(body) == std::vector<std::string>{ok, cancel, hint, side, pages, grid});
        check("move_many: into a Grid, each packed in turn (next rows)",
              dz::move_many(f.j, f.d, {ok, cancel}, grid, -1, &why) && f.d.get(ok)->parent == grid &&
              f.d.get(cancel)->parent == grid && f.d.get(ok)->pack != f.d.get(cancel)->pack, why);
        f.j.undo();
        // A Paned holds two: start + end. Three refused, all or nothing.
        const auto paned = dz::add(f.j, f.d, "Paned", body, -1);
        const auto before = f.d.children(body);
        check("move_many: more than a Paned holds -> refused, nothing moved",
              !dz::move_many(f.j, f.d, {ok, cancel, hint}, paned, -1, &why) && f.d.children(body) == before, why);
        l = dz::landing_at_many(f.d, {ok, cancel}, hint, 35, 40);
        check("landing_at_many: a leaf's second half -> after it", l.parent == body && l.index == 1,
              std::to_string(l.index));
        (void)pages;
    }

    // ── s020 m1: copy / paste / duplicate ─────────────────────────────────────
    {
        Fix f;
        namespace dz = core::design;
        const auto ok = f.d.find("ok"), cancel = f.d.find("cancel"), hint = f.d.find("hint"),
                   body = f.d.find("body"), grid = f.d.find("grid"), cell = f.d.find("cell"),
                   pages = f.d.find("pages"), side = f.d.find("side");
        check("copy_name: hello -> hello2, label1 -> label2",
              dz::copy_name(f.d, "ok") == "ok2" && dz::copy_name(f.d, "label1") == "label2");
        std::string why;
        const auto n0 = f.d.count();
        auto made = dz::paste(f.j, f.d, dz::copy_of(f.d, {ok}), body, 1, &why);
        check("paste: one, after ok", made.size() == 1 && f.d.children(body)[1] == made[0], why);
        const auto ok2 = made.empty() ? std::string() : made[0];
        check("paste: a fresh name, the hook comes along (its own)",
              f.d.get(ok2) && f.d.get(ok2)->name == "ok2" && f.d.get(ok2)->signals.size() == 1 &&
              f.d.get(ok2)->signals[0].handler.empty());
        check("paste: the step names the copy", f.j.undo_label() == "Paste “ok2”", f.j.undo_label());
        f.j.undo();
        check("paste: one undo, gone", f.d.count() == n0 && !f.d.get(ok2));
        made = dz::paste(f.j, f.d, dz::copy_of(f.d, {side, pages}), body, -1, &why);
        const auto side2 = f.d.find("side2"), pages2 = f.d.find("pages2"), page_a2 = f.d.find("page_a2");
        check("paste: a sidebar + its stack -> the copy's sidebar points at the copy's stack",
              made.size() == 2 && f.d.get(side2) && f.d.get(side2)->props.at("stack") == Value(std::string("pages2")), why);
        check("paste: the subtree comes whole (a page keeps its pack)",
              f.d.get(page_a2) && f.d.get(page_a2)->parent == pages2 &&
              f.d.get(page_a2)->pack == f.d.get(f.d.find("page_a"))->pack);
        check("paste: valid after (names, hooks unique)", first_error(f.d.to_form()).empty(), first_error(f.d.to_form()));
        f.j.undo();
        made = dz::paste(f.j, f.d, dz::copy_of(f.d, {cell}), grid, -1, &why);
        check("paste: into a Grid -> packed afresh (next row)",
              made.size() == 1 && f.d.get(made[0])->pack != f.d.get(cell)->pack, why);
        f.j.undo();
        made = dz::duplicate(f.j, f.d, {cancel, ok}, &why);
        const auto kids = f.d.children(body);
        check("duplicate: two, right after the last, in order",
              made.size() == 2 && kids.size() > 3 && kids[2] == made[0] && kids[3] == made[1] &&
              f.d.get(made[0])->name == "ok2" && f.d.get(made[1])->name == "cancel2", why);
        check("duplicate: one step", f.j.undo_label() == "Duplicate 2 widgets", f.j.undo_label());
        f.j.undo();
        check("duplicate: from two containers -> refused", dz::duplicate(f.j, f.d, {ok, cell}, &why).empty() &&
              why.find("one container") != std::string::npos, why);
        const auto paned = dz::add(f.j, f.d, "Paned", body, -1);
        const auto n1 = f.d.count();
        check("paste: more than a Paned holds -> refused, nothing added",
              dz::paste(f.j, f.d, dz::copy_of(f.d, {ok, cancel, hint}), paned, -1, &why).empty() && f.d.count() == n1, why);
        check("paste: an empty clip says so", dz::paste(f.j, f.d, dz::Clip{}, body, -1, &why).empty() &&
              why.find("copy something") != std::string::npos);
    }

    // ── s024: an app made before the gate gets it on generate, once ──────────
    {
        const fs::path tmp = fs::temp_directory_path() / "mortimer_gate_selftest";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        std::string app;
        try { app = core::create_workspace(core::derive_app("Older"), tmp.string()); }
        catch (const std::exception& e) { check("s024 workspace", false, e.what()); }
        if (!app.empty()) {
            auto slurp = [](const fs::path& p) { std::ifstream in(p); std::stringstream s; s << in.rdbuf(); return s.str(); };
            const fs::path cm = fs::path(app) / "CMakeLists.txt";
            std::string text = slurp(cm);
            check("s024: a new app's CMakeLists has the gate", text.find("older_undo_gate") != std::string::npos);
            check("s024: a new app has generated/UndoGate.cpp", fs::exists(fs::path(app) / "generated" / "UndoGate.cpp"));
            auto at = text.find("older_undo_gate -- ");
            if (at != std::string::npos) {
                at = text.rfind('\n', at) + 1;
                text.erase(at);
                std::ofstream(cm) << text;   // as an app made before s024
            }
            const auto r = core::generate_project(app);
            const std::string after = slurp(cm);
            bool noted = false;
            for (const auto& n : r.notes) noted = noted || n.find("the undo gate added") != std::string::npos;
            check("s024: generate appends the gate to an older app's CMakeLists, and says so",
                  at != std::string::npos && after.find("add_executable(older_undo_gate") != std::string::npos && noted);
            core::generate_project(app);
            check("s024: ...once", slurp(cm) == after);
            // s024 m2: and the regenerate block, before the first glob.
            std::string t2 = slurp(cm);
            const auto rb = t2.find("# \u2500\u2500 forms/ -> generated/");
            const auto cb = t2.find("# \u2500\u2500 older_core");
            if (rb != std::string::npos && cb != std::string::npos) { t2.erase(rb, cb - rb); std::ofstream(cm) << t2; }
            const auto r2 = core::generate_project(app);
            const std::string a2 = slurp(cm);
            bool said = false;
            for (const auto& n : r2.notes) said = said || n.find("now regenerates") != std::string::npos;
            check("s024 m2: an older app gets the regenerate block, before its globs, and is told",
                  rb != std::string::npos && said && a2.find("MORTIMER_GEN") != std::string::npos &&
                  a2.find("execute_process(COMMAND \"${MORTIMER_GEN}\"") < a2.find("file(GLOB CORE_SOURCES"));
            core::generate_project(app);
            check("s024 m2: ...once", slurp(cm) == a2);
        }
        fs::remove_all(tmp);
    }

    // ── s002 m3: the window's project awareness ─────────────────────────────
    {
        const fs::path tmp = fs::temp_directory_path() / "mortimer_design_selftest";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        std::string app;
        try {
            app = core::create_workspace(core::derive_app("Probe"), tmp.string());
        } catch (const std::exception& e) {
            check("project: workspace", false, e.what());
        }
        if (!app.empty()) {
            const auto main = (fs::path(app) / "forms" / "MainWindow.mort").string();
            check("project_of: a project's form -> its app", core::project_of(main) == app,
                  core::project_of(main));
            check("project_of: a loose form -> \"\"",
                  core::project_of((fs::path(MORTIMER_SOURCE_DIR) / "tests" / "KitchenSink.mort").string()).empty());
            std::string made, err;
            try { made = core::add_form(app, "Prefs"); } catch (const std::exception& e) { err = e.what(); }
            check("add_form: writes forms/Prefs.mort", !made.empty() && fs::exists(made), err);
            check("add_form: listed in mortimer.json", core::project_of(made) == app);
            bool dup = false, bad = false;
            try { core::add_form(app, "Prefs"); } catch (const std::exception&) { dup = true; }
            try { core::add_form(app, "class"); } catch (const std::exception&) { bad = true; }
            check("add_form: refuses a duplicate and a keyword", dup && bad);
            // s004: a Dialog form -- modal + hides on close, and present_for().
            std::string dlg;
            try { dlg = core::add_form(app, "AskDialog", "Dialog"); } catch (const std::exception& e) { check("add_form Dialog", false, e.what()); }
            bool badroot = false;
            try { core::add_form(app, "X", "Box"); } catch (const std::exception&) { badroot = true; }
            check("add_form: a Box isn't a window", badroot);
            const auto gen = core::generate_project(app);
            {
                std::ifstream h((fs::path(app) / "generated" / "AskDialogUi.hpp").string()), c((fs::path(app) / "generated" / "AskDialogUi.cpp").string());
                std::stringstream hs, cs; hs << h.rdbuf(); cs << c.rdbuf();
                check("Dialog: present_for(parent) generated", hs.str().find("void present_for(Gtk::Window& parent)") != std::string::npos);
                check("Dialog: modal + hide_on_close always set",
                      cs.str().find("set_modal(true)") != std::string::npos &&
                      cs.str().find("set_hide_on_close(true)") != std::string::npos);
            }
            check("add_form: the new form generates", gen.ok() &&
                  fs::exists(fs::path(app) / "generated" / "PrefsUi.hpp") &&
                  fs::exists(fs::path(app) / "include" / "Prefs.hpp"));
            // Live report: a signal ticked in the window, not yet saved, shows unhooked.
            Fix f(core::load_form(made));
            core::design::tick(f.j, f.d, f.d.root(), "close_request", true);
            const auto rep = core::live_report(app, f.d.to_form());
            check("live_report: an unsaved tick shows, unhooked",
                  rep.header_found && rep.hooks.size() == 1 && !rep.hooks[0].hooked && rep.unhooked_count() == 1);
            core::design::require(f.j, f.d, f.d.root(), "close_request", true);
            check("live_report: required + unwritten -> missing",
                  core::live_report(app, f.d.to_form()).missing.size() == 1);
            check("live_report: no project -> no header", !core::live_report("", f.d.to_form()).header_found);

            // s009: the bundle and the forms, as the navigator's + and delete do them.
            {
                const fs::path srcs = tmp / "srcs";
                fs::create_directories(srcs);
                auto put = [&](const char* name, const char* text) {
                    std::ofstream(srcs / name) << text; return (srcs / name).string(); };
                const auto svg = put("star-symbolic.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"/>");
                const auto png = put("cover.png", "not really a png");
                const auto json = put("words.json", "[]");
                const auto bad = put("my file.png", "x");
                auto list = [&] { return core::list_resources(app); };
                check("resources: a new app has its logo", list().size() == 1 && list()[0].kind == "icon" &&
                                                            list()[0].use == "probe-logo-symbolic", list().empty() ? "" : list()[0].use);
                std::string err;
                core::Resource r;
                try { r = core::add_resource(app, "icon", svg); } catch (const std::exception& e) { err = e.what(); }
                check("add icon: copied + bundled, named by its stem",
                      fs::exists(fs::path(app) / "resources/icons/scalable/apps/star-symbolic.svg") &&
                      r.use == "star-symbolic" && r.path == "/io/github/scott8420/Probe/icons/scalable/apps/star-symbolic.svg", err);
                try { r = core::add_resource(app, "image", png); } catch (const std::exception& e) { err = e.what(); }
                check("add image: under images/, its resource path is what code names",
                      r.kind == "image" && r.use == "/io/github/scott8420/Probe/images/cover.png", err + r.use);
                try { r = core::add_resource(app, "data", json); } catch (const std::exception& e) { err = e.what(); }
                check("add data: under data/", r.kind == "data" && r.file == "data/words.json", err);
                try { r = core::add_stylesheet(app); } catch (const std::exception& e) { err = e.what(); }
                check("add stylesheet: <bin>.css made + bundled at the id path",
                      r.kind == "css" && r.path == "/io/github/scott8420/Probe/probe.css" &&
                      fs::exists(fs::path(app) / "resources/probe.css"), err);
                auto refused = [&](auto fn) { try { fn(); } catch (const std::exception&) { return true; } return false; };
                check("add: refuses a duplicate", refused([&] { core::add_resource(app, "image", png); }));
                check("add: refuses a name with a space", refused([&] { core::add_resource(app, "image", bad); }));
                check("add: an icon must be an .svg", refused([&] { core::add_resource(app, "icon", png); }));
                check("add: refuses an unknown kind / a missing file",
                      refused([&] { core::add_resource(app, "font", json); }) &&
                      refused([&] { core::add_resource(app, "image", (srcs / "nope.png").string()); }));
                check("add: a second stylesheet refused", refused([&] { core::add_stylesheet(app); }));
                check("resources: all five listed", list().size() == 5, std::to_string(list().size()));
                std::string went;
                try { went = core::remove_resource(app, "images/cover.png"); } catch (const std::exception& e) { err = e.what(); }
                check("remove resource: unbundled, the file moved to removed/",
                      went == "removed/resources/images/cover.png" && fs::exists(fs::path(app) / went) &&
                      !fs::exists(fs::path(app) / "resources/images/cover.png") && list().size() == 4, err + went);
                check("remove resource: the logo stays",
                      refused([&] { core::remove_resource(app, "icons/scalable/apps/probe-logo-symbolic.svg"); }));
                try { r = core::add_resource(app, "image", png); } catch (const std::exception& e) { err = e.what(); }
                try { went = core::remove_resource(app, "images/cover.png"); } catch (const std::exception& e) { err = e.what(); }
                check("remove resource: a second removal doesn't overwrite the first",
                      went == "removed/resources/images/cover.png.2", went);
                // The xml still compiles (the real tool), if it's here.
                if (std::system("command -v glib-compile-resources >/dev/null 2>&1") == 0) {
                    const std::string cmd = "cd '" + (fs::path(app) / "resources").string() +
                                            "' && glib-compile-resources --generate-source --target=/dev/null probe.gresource.xml >/dev/null 2>&1";
                    check("resources: the edited xml compiles (glib-compile-resources)", std::system(cmd.c_str()) == 0);
                }
                check("form_kind: reads the root type", core::form_kind(app, "AskDialog") == "Dialog" &&
                                                         core::form_kind(app, "Nope").empty());
                check("remove form: the main form stays", refused([&] { core::remove_form(app, "MainWindow"); }));
                try { went = core::remove_form(app, "Prefs"); } catch (const std::exception& e) { err = e.what(); }
                const auto pr = core::load_project(app);
                check("remove form: unlisted; .mort + your halves moved to removed/Prefs",
                      went == "removed/Prefs" && std::find(pr.forms.begin(), pr.forms.end(), "Prefs") == pr.forms.end() &&
                      fs::exists(fs::path(app) / "removed/Prefs/Prefs.mort") &&
                      fs::exists(fs::path(app) / "removed/Prefs/Prefs.hpp") &&
                      fs::exists(fs::path(app) / "removed/Prefs/Prefs.cpp") &&
                      !fs::exists(fs::path(app) / "src/Prefs.cpp"), err + went);
                const auto g2 = core::generate_project(app);
                check("remove form: generate prunes its generated files",
                      g2.ok() && !fs::exists(fs::path(app) / "generated/PrefsUi.hpp"));
                check("support: Style.cpp loads the bundled <bin>.css",
                      fs::exists(fs::path(app) / "generated/Style.cpp"));
            }
        }
        fs::remove_all(tmp);
    }

    // ── s021: rename a project's form ───────────────────────────────────────
    {
        // The scanner alone: code and #includes follow, strings never do.
        const std::string code =
            "#include \"About.hpp\"   // About -- your half\n"
            "#include \"generated/AboutUi.hpp\"\n"
            "#include \"AboutBox.hpp\"\n"
            "class About : public ui::AboutUi {   /* About */\n"
            "    About();  int About2 = 1'000;\n"
            "    const char* t = \"About Notes\"; char c = 'A';\n"
            "    const char* r = R\"x(About \"About\")x\";\n"
            "};\n";
        const std::string own = core::rename_class_in(code, "About", "Info", true);
        const std::string other = core::rename_class_in(code, "About", "Info", false);
        check("s021 rename_class_in: class, base, includes follow; a longer name doesn't",
              own.find("class Info : public ui::InfoUi") != std::string::npos &&
              own.find("#include \"Info.hpp\"") != std::string::npos &&
              own.find("#include \"generated/InfoUi.hpp\"") != std::string::npos &&
              own.find("#include \"AboutBox.hpp\"") != std::string::npos &&
              own.find("    Info();  int About2 = 1'000;") != std::string::npos, own);
        check("s021 rename_class_in: strings, chars, raw strings never change",
              own.find("\"About Notes\"") != std::string::npos && own.find("'A'") != std::string::npos &&
              own.find("R\"x(About \"About\")x\"") != std::string::npos, own);
        check("s021 rename_class_in: comments follow in your form's own files, not in others",
              own.find("// Info -- your half") != std::string::npos && own.find("/* Info */") != std::string::npos &&
              other.find("// About -- your half") != std::string::npos && other.find("/* About */") != std::string::npos &&
              other.find("class Info : public ui::InfoUi") != std::string::npos, other);
        check("s021 rename_class_in: nothing to rename -> the same text",
              core::rename_class_in("int x = 1; // y\n", "About", "Info", true) == "int x = 1; // y\n");

        const fs::path tmp = fs::temp_directory_path() / "mortimer_rename_selftest";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        std::string app, err;
        try { app = core::create_workspace(core::derive_app("Probe"), tmp.string()); }
        catch (const std::exception& e) { err = e.what(); }
        check("s021 rename: a workspace (with About)", !app.empty(), err);
        if (!app.empty()) {
            const fs::path A(app);
            auto slurp = [](const fs::path& p) { std::ifstream in(p); std::stringstream s; s << in.rdbuf(); return s.str(); };
            auto refused = [&](auto fn) { try { fn(); } catch (const std::exception&) { return true; } return false; };
            core::generate_project(app);
            // A mention of About in another file of yours (code + a string).
            {
                std::ofstream(A / "src" / "Extra.cpp") << "#include \"About.hpp\"\nnamespace probe {\n"
                                                          "void show_about() { auto* a = new About(); a->set_title(\"About\"); }\n}\n";
            }
            check("s021 rename: not a class name / taken / the app's own file -> refused",
                  refused([&] { core::rename_project_form(app, "About", "class"); }) &&
                  refused([&] { core::rename_project_form(app, "About", "MainWindow"); }) &&
                  refused([&] { core::rename_project_form(app, "About", "App"); }) &&
                  refused([&] { core::rename_project_form(app, "Nope", "Info"); }) &&
                  fs::exists(A / "forms/About.mort"));
            core::RenamedForm r;
            err.clear();
            try { r = core::rename_project_form(app, "About", "Info"); } catch (const std::exception& e) { err = e.what(); }
            const auto pr = core::load_project(app);
            check("s021 rename: the file, its name inside, the list in its place",
                  err.empty() && fs::exists(A / "forms/Info.mort") && !fs::exists(A / "forms/About.mort") &&
                  core::load_form((A / "forms/Info.mort").string()).name == "Info" &&
                  pr.forms.size() == 2 && pr.forms[1] == "Info", err);
            check("s021 rename: your halves moved, the class renamed inside",
                  fs::exists(A / "include/Info.hpp") && !fs::exists(A / "include/About.hpp") &&
                  fs::exists(A / "src/Info.cpp") && !fs::exists(A / "src/About.cpp") &&
                  slurp(A / "include/Info.hpp").find("class Info : public ui::InfoUi") != std::string::npos &&
                  slurp(A / "src/Info.cpp").find("Info::Info()") != std::string::npos);
            const std::string extra = slurp(A / "src/Extra.cpp");
            check("s021 rename: another file of yours follows (include, code), its string doesn't",
                  extra.find("#include \"Info.hpp\"") != std::string::npos && extra.find("new Info()") != std::string::npos &&
                  extra.find("set_title(\"About\")") != std::string::npos, extra);
            bool opens = false;
            for (const auto& a : core::load_form((A / "forms/MainWindow.mort").string()).actions) opens |= a.opens == "Info";
            check("s021 rename: the main window's About action opens Info now",
                  opens && std::find(r.forms.begin(), r.forms.end(), "MainWindow") != r.forms.end());
            check("s021 rename: the originals kept in removed/About.renamed/",
                  r.kept == "removed/About.renamed" && fs::exists(A / "removed/About.renamed/include/About.hpp") &&
                  fs::exists(A / "removed/About.renamed/src/Extra.cpp") &&
                  slurp(A / "removed/About.renamed/src/Extra.cpp").find("new About()") != std::string::npos, r.kept);
            check("s021 rename: the hooks manifest moved with it", fs::exists(A / "generated/Info.hooks.json") &&
                                                                 !fs::exists(A / "generated/About.hooks.json"));
            const auto g = core::generate_project(app);
            check("s021 rename: generate writes InfoUi, prunes AboutUi, the main window includes Info",
                  g.ok() && fs::exists(A / "generated/InfoUi.hpp") && !fs::exists(A / "generated/AboutUi.hpp") &&
                  slurp(A / "generated/MainWindowUi.cpp").find("#include \"Info.hpp\"") != std::string::npos);
            // The main form: main_form follows, and App.cpp's `new MainWindow()`.
            err.clear();
            try { core::rename_project_form(app, "MainWindow", "Main"); } catch (const std::exception& e) { err = e.what(); }
            check("s021 rename: the main form -- main_form and App.cpp follow",
                  err.empty() && core::load_project(app).main_form == "Main" &&
                  slurp(A / "src/App.cpp").find("new Main()") != std::string::npos &&
                  slurp(A / "src/App.cpp").find("#include \"Main.hpp\"") != std::string::npos, err);
            // s021 m2: rename a resource.
            {
                const fs::path srcs = tmp / "srcs";
                fs::create_directories(srcs);
                std::ofstream(srcs / "star-symbolic.svg") << "<svg xmlns=\"http://www.w3.org/2000/svg\"/>";
                std::ofstream(srcs / "cover.png") << "png";
                core::add_resource(app, "icon", (srcs / "star-symbolic.svg").string());
                const auto img = core::add_resource(app, "image", (srcs / "cover.png").string());
                core::add_stylesheet(app);   // listed, so its refusal below is the real one
                // A form names the icon (a button), and code names the image's path.
                core::Form mf = core::load_form((A / "forms/Main.mort").string());
                mf.root.children[0].props["icon_name"] = core::Value(std::string("star-symbolic"));
                core::save_form(mf, (A / "forms/Main.mort").string());
                std::ofstream(A / "src" / "Cover.cpp") << "const char* k = \"" << img.path << "\";\n";
                const auto list = [&] { return core::list_resources(app); };
                check("s021 m2 rename resource: refused -- the stylesheet, the logo, a bad name, an icon not .svg, taken",
                      refused([&] { core::rename_resource(app, "probe.css", "x.css"); }) &&
                      refused([&] { core::rename_resource(app, "icons/scalable/apps/probe-logo-symbolic.svg", "l.svg"); }) &&
                      refused([&] { core::rename_resource(app, "images/cover.png", "my cover.png"); }) &&
                      refused([&] { core::rename_resource(app, "icons/scalable/apps/star-symbolic.svg", "star.png"); }) &&
                      refused([&] { core::rename_resource(app, "images/cover.png", "cover"); }) &&
                      refused([&] { core::rename_resource(app, "images/nope.png", "x.png"); }));
                core::RenamedResource rr;
                err.clear();
                try { rr = core::rename_resource(app, "icons/scalable/apps/star-symbolic.svg", "sun-symbolic"); }
                catch (const std::exception& e) { err = e.what(); }
                check("s021 m2 rename icon: the file, the xml (extension kept), the icon name",
                      err.empty() && rr.now.file == "icons/scalable/apps/sun-symbolic.svg" && rr.now.use == "sun-symbolic" &&
                      rr.was == "star-symbolic" && fs::exists(A / "resources/icons/scalable/apps/sun-symbolic.svg") &&
                      !fs::exists(A / "resources/icons/scalable/apps/star-symbolic.svg") &&
                      slurp(A / "resources/probe.gresource.xml").find("preprocess=\"xml-stripblanks\" alias=\"sun-symbolic.svg\">icons/scalable/apps/sun-symbolic.svg<") != std::string::npos, err);
                check("s021 m2 rename icon: the form that named it follows",
                      rr.forms == std::vector<std::string>{"Main"} &&
                      std::get<std::string>(core::load_form((A / "forms/Main.mort").string()).root.children[0].props["icon_name"]) == "sun-symbolic");
                err.clear();
                try { rr = core::rename_resource(app, "images/cover.png", "front"); } catch (const std::exception& e) { err = e.what(); }
                check("s021 m2 rename image: path follows; code naming the old path listed, not changed",
                      err.empty() && rr.now.file == "images/front.png" && rr.now.path.ends_with("/images/front.png") &&
                      rr.mentions == std::vector<std::string>{"src/Cover.cpp"} &&
                      slurp(A / "src/Cover.cpp").find("/images/cover.png") != std::string::npos && list().size() == 4,
                      err + " list " + std::to_string(list().size()));
                if (std::system("command -v glib-compile-resources >/dev/null 2>&1") == 0) {
                    const std::string cmd = "cd '" + (A / "resources").string() +
                                            "' && glib-compile-resources --generate-source --target=/dev/null probe.gresource.xml >/dev/null 2>&1";
                    check("s021 m2 rename resource: the xml still compiles", std::system(cmd.c_str()) == 0);
                }
                fs::remove(A / "src" / "Cover.cpp");
            }
            // And back: a second rename keeps its originals beside the first.
            err.clear();
            try { r = core::rename_project_form(app, "Info", "About"); } catch (const std::exception& e) { err = e.what(); }
            check("s021 rename: back again; the second originals don't overwrite the first",
                  err.empty() && fs::exists(A / "src/About.cpp") && r.kept == "removed/Info.renamed" &&
                  slurp(A / "src/Extra.cpp").find("new About()") != std::string::npos, err + r.kept);
        }
        fs::remove_all(tmp);
    }
}
