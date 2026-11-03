#pragma once
// core::Design -- the form the designer EDITS: a Form flattened into records
// by a stable key, so the undo journal (core/Undo.hpp) can hold it. GTK-free.
//
// Why not edit the Form tree directly? The journal works by id and by value
// (docs/undo.md): a step is the records it touched, before and after. A tree
// of nested Nodes has no ids that survive a move, and a "record" would be a
// whole subtree. So the designer keeps the form flat:
//
//   key "n7" -> Rec { type, name (the C++ id), parent key, pos, props, pack, signals }
//
// * The KEY is not the name. Renaming `ok` to `save` changes a field; the key
//   stays, so a step recorded before the rename still points at the same widget.
// * The PLACE is in the value (parent + pos), so putting an old value back puts
//   the widget back where it was -- delete + undo lands in the same slot.
// * The form's own name rides in one more record, key kForm.
//
// Loading a Form (from_form) is not a user choice: Loading scope, no step.
// Saving reads it back out as a Form (to_form) -- round-trips byte-exact
// through form_to_json, which the selftest checks on the kitchen sink.
//
// Every verb the designer offers is in namespace design below and in its verb
// table; the selftest walks the table (one door, undo restores exactly, redo
// reproduces). A button the window gains without a row here is the gap the
// gate cannot see -- add both in the same change.
#include "core/Form.hpp"
#include "core/Undo.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mortimer::core {

struct Rec {
    std::string                  type;    // catalog type; "" on the kForm record
    std::string                  name;    // the C++ id (Node::id); the form's class name on kForm
    std::string                  parent;  // key of the parent; "" for the root (and kForm)
    double                       pos = 0; // place among the parent's children (ascending)
    std::map<std::string, Value> props;
    std::map<std::string, Value> pack;
    std::vector<SignalHook>      signals;
    std::vector<ActionDef>       actions;   // s003: on the kForm record only
    std::vector<FileDef>         files;     // s005: on the kForm record only
    std::vector<ModelDef>        models;    // s006: on the kForm record only
    StringList                   keys;      // s016: on the kForm record only
    bool operator==(const Rec&) const = default;
};

class Design {
public:
    using Id    = std::string;
    using Value = Rec;
    static inline const Id kForm = "@form";

    // ── the journal's contract ──────────────────────────────────────────────
    std::optional<Rec> get(const Id& id) const;
    void put(const Id& id, const std::optional<Rec>& v);

    // ── loading / saving (a load is not a user choice) ──────────────────────
    void load(const Form& f);    // replaces everything; keys n1.. in pre-order
    Form to_form() const;        // throws std::logic_error if there is no root
    // s013: the PROJECT's models (mortimer.json) -- the form's views may show
    // them. Not the form's, not a user choice here: not in the journal.
    void set_shared(std::vector<ModelDef> m) { m_shared = std::move(m); }
    const std::vector<ModelDef>& shared() const { return m_shared; }
    std::vector<ModelDef> all_models() const;           // the form's own, then the shared

    // ── reading ─────────────────────────────────────────────────────────────
    Id              root() const;                       // "" when empty
    std::string     form_name() const;
    std::vector<Id> children(const Id& parent) const;   // in order
    std::vector<Id> subtree(const Id& id) const;        // id first, then pre-order
    std::vector<Id> all() const;                        // the widgets, pre-order from the root
    Id              find(const std::string& name) const;   // by C++ id; "" if none
    bool            is_under(const Id& id, const Id& ancestor) const;  // id == ancestor counts
    std::size_t     count() const;                      // widgets (not kForm)
    Id              fresh_key();                        // "n1", "n2", ... never reused
    std::string     fresh_name(const std::string& type) const;  // "button1", "button2", ...
    // A pos that sits at `index` among parent's children (-1 / past the end:
    // last), ignoring `moving` (a widget being moved within its own parent).
    double          pos_at(const Id& parent, int index, const Id& moving = {}) const;

    // ── the one-door guard (same shape as Stones) ───────────────────────────
    void set_door(std::function<bool()> open) { m_door = std::move(open); }
    int  strays() const { return m_strays; }
    struct Loading {
        explicit Loading(Design& d) : m_d(d) { ++m_d.m_loading; }
        ~Loading() { --m_d.m_loading; }
        Design& m_d;
    };
    void set_on_change(std::function<void()> f) { m_changed = std::move(f); }

private:
    std::map<Id, Rec>      m_recs;
    std::vector<ModelDef>  m_shared;   // s013
    int                    m_next = 1;
    int                    m_strays = 0;
    int                    m_loading = 0;
    std::function<bool()>  m_door;
    std::function<void()>  m_changed;
};

using DesignJournal = Journal<Design>;

// ── the verbs: every user choice the designer offers ───────────────────────
// Each goes through the journal. Refusals return false / "" and record nothing;
// `why` (when given) says what was wrong, in the words the window shows.
namespace design {

// Can `parent` take one more child of `type`? "" = yes, else the reason.
// When yes and `pack` is given, it is filled with where the child would sit
// (the first free slot, the next Grid row, a Stack page name).
std::string can_hold(const Design& d, const Design::Id& parent, const std::string& type,
                     std::map<std::string, Value>* pack = nullptr,
                     const Design::Id& moving = {});

// Palette -> tree. index -1 = last. Returns the new key ("" refused).
Design::Id add(DesignJournal& j, Design& d, const std::string& type,
               const Design::Id& parent, int index = -1, std::string* why = nullptr);
// The widget and everything under it. Not the root.
bool remove(DesignJournal& j, Design& d, const Design::Id& id, std::string* why = nullptr);
// Reorder within a parent, or reparent (packing re-chosen when the parent changes).
bool move(DesignJournal& j, Design& d, const Design::Id& id, const Design::Id& parent,
          int index, std::string* why = nullptr);
bool move_up(DesignJournal& j, Design& d, const Design::Id& id);
bool move_down(DesignJournal& j, Design& d, const Design::Id& id);
// The C++ id. Refs to it (a StackSidebar's stack) follow, in the same step.
bool rename(DesignJournal& j, Design& d, const Design::Id& id, const std::string& name,
            std::string* why = nullptr);
// The form's class name ("MainWindow").
bool rename_form(DesignJournal& j, Design& d, const std::string& name, std::string* why = nullptr);
// s003 -- the form's actions (on the kForm record). add_action names it
// "action1".. unless given a name; remove_action takes the menu rows that ran
// it along (same step); set_action replaces one whole (rename, key,
// required) -- a rename carries every menu row "win.<old>" with it, same step.
std::string add_action(DesignJournal& j, Design& d, const std::string& name = {}, std::string* why = nullptr);
bool remove_action(DesignJournal& j, Design& d, const std::string& name);
bool set_action(DesignJournal& j, Design& d, const std::string& name, const ActionDef& now,
                std::string* why = nullptr);
// s005 -- the form's file dialogs (on the kForm record). add_file names it
// "file1".. unless given a name (mode "open"); set_file replaces one whole
// (rename, mode, title, accept, filters, cancel, required) -- typing in one
// field folds into one step ("file:<name>").
std::string add_file(DesignJournal& j, Design& d, const std::string& name = {}, std::string* why = nullptr);
bool remove_file(DesignJournal& j, Design& d, const std::string& name);
// s016 -- the shortcuts window's own rows ("Keys=What"), the whole list at
// once. Typing in one row folds into one step; a row added / removed is its own.
bool set_keys(DesignJournal& j, Design& d, const StringList& now);
bool set_file(DesignJournal& j, Design& d, const std::string& name, const FileDef& now,
              std::string* why = nullptr);
// s006 -- the form's models (on the kForm record). add_model names it
// "Model1".. with one string field "title" (a model needs a column);
// remove_model refuses while a ColumnView shows it (says which); set_model
// replaces one whole -- a rename carries every ColumnView "model" with it,
// same step; typing a field's name or header folds per field.
std::string add_model(DesignJournal& j, Design& d, const std::string& name = {}, std::string* why = nullptr);
bool remove_model(DesignJournal& j, Design& d, const std::string& name, std::string* why = nullptr);
bool set_model(DesignJournal& j, Design& d, const std::string& name, const ModelDef& now,
               std::string* why = nullptr);

// A property: a value, or nullopt to clear it back to GTK's default. Typing in
// one field folds into one step (merge key "prop:<key>:<name>").
// fold=false (s007: a tick in a checklist) is always its own step.
bool set_prop(DesignJournal& j, Design& d, const Design::Id& id, const std::string& prop,
              const std::optional<Value>& v, bool fold = true);
bool set_pack(DesignJournal& j, Design& d, const Design::Id& id, const std::string& key,
              const std::optional<Value>& v);
// Signals: tick one (a hook), untick it, mark it required (pure virtual).
bool tick(DesignJournal& j, Design& d, const Design::Id& id, const std::string& signal, bool on);
bool require(DesignJournal& j, Design& d, const Design::Id& id, const std::string& signal, bool on);

// ── drag and drop: where a drop on a tree row lands ────────────────────────
// A container's row has three zones (top quarter before, middle half INTO,
// bottom quarter after); a leaf two (halves); the root only INTO.
// (docs/tree-and-dnd.md: Folio's 25/75 split.)
enum class Zone { Before, Inside, After };
Zone zone_at(double y, int height, bool container, bool root);

struct Landing {
    Design::Id  parent;      // "" = refused
    int         index = -1;  // among parent's children, the moving one left out
    std::string why;         // when refused
};
// `type` is what lands; `moving` its key for a move ("" for a new one from
// the palette); `target` the row hovered; `z` the zone on it. Refuses a drop
// into the moving widget's own subtree, onto itself, or where can_hold says no.
Landing landing(const Design& d, const std::string& type, const Design::Id& moving,
                const Design::Id& target, Zone z);
// A drop on a widget on the CANVAS: into it if it can take the type, else
// just after it in its parent.
Landing landing_on(const Design& d, const std::string& type, const Design::Id& moving,
                   const Design::Id& target);

// s017 m2: a drop on the CANVAS with the tree's zones. `pos` is where the
// pointer is along the target's PARENT's axis (y in a vertical Box, x in a
// horizontal one), `extent` the target's size on that axis. The zone
// (zone_at: a container's quarters, a leaf's halves) picks before / into /
// after; when that spot refuses (a full window, a parent that can't hold the
// type), it falls back to landing_on's into-or-after. `used` (optional) says
// which it came to: Before / After = beside the target, Inside = into it.
Landing landing_at(const Design& d, const std::string& type, const Design::Id& moving,
                   const Design::Id& target, double pos, int extent, Zone* used = nullptr);

// ── s018: several selected at once ─────────────────────────────────────────
// The selection is a list, primary first (the inspector shows it). These are
// its rules, pure so the selftest walks them.
//
// The ones a delete / move acts on: unknown ids and the window dropped, and
// any widget inside another selected one (it goes with its ancestor) -- in
// the tree's order.
std::vector<Design::Id> top_level(const Design& d, const std::vector<Design::Id>& ids);
// Shift+click: every widget from `a` to `b` in the tree's order (either way
// round), both included; the window only when it is one of the ends alone.
std::vector<Design::Id> range(const Design& d, const Design::Id& a, const Design::Id& b);
// Ctrl+click: `key` in or out of `sel` (primary first). Taking out the last
// one is refused (something is always selected); taking out the primary hands
// it to the next. Adding makes `key` the primary.
std::vector<Design::Id> toggle(const std::vector<Design::Id>& sel, const Design::Id& key);
// Delete them all as ONE step ("Delete 3 widgets"); the window refused.
bool remove_many(DesignJournal& j, Design& d, const std::vector<Design::Id>& ids, std::string* why = nullptr);
// After that delete, who takes the selection: the first one's neighbour if it
// stays, else the nearest ancestor that stays.
Design::Id survivor(const Design& d, const std::vector<Design::Id>& going);

// ── s019: a drag that carries several ────────────────────────────────────────
// The payload of a several-at-once drag: "move:n3,n7" -- the keys, comma-joined.
std::string join_keys(const std::vector<Design::Id>& ids);
std::vector<Design::Id> split_keys(const std::string& s);
// Move them all to (parent, index) as ONE step ("Move 3 widgets"), in the
// tree's order, side by side; `index` counts the parent's children WITHOUT the
// moving ones. A widget inside another moving one rides along with it. All or
// nothing: each is packed in turn on a scratch copy first (a Paned's second
// slot, a Grid's next row see the ones placed before), and one refusal refuses
// the lot. One alone is plain `move`.
bool move_many(DesignJournal& j, Design& d, const std::vector<Design::Id>& ids,
               const Design::Id& parent, int index, std::string* why = nullptr);
// Where a several-at-once drop lands: `landing` / `landing_at` for a group.
Landing landing_many(const Design& d, const std::vector<Design::Id>& moving, const Design::Id& target, Zone z);
Landing landing_at_many(const Design& d, const std::vector<Design::Id>& moving, const Design::Id& target,
                        double pos, int extent, Zone* used = nullptr);

// Who takes the selection when `id` goes: next sibling, else previous, else parent.
Design::Id neighbour(const Design& d, const Design::Id& id);

// The verb table runs on this form: a window holding a Box (`body`) with two
// Buttons (`ok`, `cancel`), a Label (`hint`), a Stack (`pages`) with one page
// and a StackSidebar pointing at it, a Grid (`grid`) with one child, and a
// HeaderBar titlebar (`bar`).
Form fixture();
struct Verb {
    std::string name;
    std::function<void(DesignJournal&, Design&)> run;   // on a fresh fixture()
};
const std::vector<Verb>& verbs();

}  // namespace design
}  // namespace mortimer::core
