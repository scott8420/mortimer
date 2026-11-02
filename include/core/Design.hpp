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
// A property: a value, or nullopt to clear it back to GTK's default. Typing in
// one field folds into one step (merge key "prop:<key>:<name>").
bool set_prop(DesignJournal& j, Design& d, const Design::Id& id, const std::string& prop,
              const std::optional<Value>& v);
bool set_pack(DesignJournal& j, Design& d, const Design::Id& id, const std::string& key,
              const std::optional<Value>& v);
// Signals: tick one (a hook), untick it, mark it required (pure virtual).
bool tick(DesignJournal& j, Design& d, const Design::Id& id, const std::string& signal, bool on);
bool require(DesignJournal& j, Design& d, const Design::Id& id, const std::string& signal, bool on);

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
