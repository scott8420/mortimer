#include "Shell.hpp"
#include "Log.hpp"
#include "core/Recents.hpp"

#include <giomm/simpleactiongroup.h>
#include <gtkmm/eventcontrollerkey.h>
#include <glibmm/variant.h>

// Shell_bindings.cpp -- BINDINGS. What's wired to what: win.* actions, the
// recents group, the design <-> journal <-> panes wiring, and the window keys.
// No slot bodies here -- those are handlers.

namespace mortimer {

void Shell::bind_actions() {  // bindings: win.* + recents group
    add_action("new",           sigc::mem_fun(*this, &Shell::on_new));
    add_action("open",          sigc::mem_fun(*this, &Shell::on_open));
    add_action("save",          sigc::mem_fun(*this, &Shell::on_save));
    add_action("save-as",       sigc::mem_fun(*this, &Shell::on_save_as));
    add_action("new-project",   sigc::mem_fun(*this, &Shell::on_new_project));
    add_action("open-project",  sigc::mem_fun(*this, &Shell::on_open_project));
    m_act_add_form = add_action("add-window", [this] { on_add_form("Window"); });
    m_act_add_dialog = add_action("add-dialog", [this] { on_add_form("Dialog"); });
    m_act_add_about = add_action("add-about", sigc::mem_fun(*this, &Shell::on_add_about));
    m_act_edit_style = add_action("edit-style", sigc::mem_fun(*this, &Shell::on_edit_style));   // s012
    for (const char* k : {"icon", "image", "data", "stylesheet"})   // s009: the navigator's +
        m_act_add_res.push_back(add_action(std::string("add-") + k, [this, k] { on_add_resource(k); }));
    add_action_with_parameter("open-form", Glib::Variant<Glib::ustring>::variant_type(),
        [this](const Glib::VariantBase& param) {
            on_open_form(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(param).get());
        });
    add_action("dump-registry", sigc::mem_fun(*this, &Shell::on_dump_registry));
    add_action("about",         sigc::mem_fun(*this, &Shell::on_about));
    add_action("shortcuts",     sigc::mem_fun(*this, &Shell::on_shortcuts));
    add_action("quit",          [this] { close(); });
    m_act_undo   = add_action("undo",      sigc::mem_fun(*this, &Shell::on_undo));
    m_act_redo   = add_action("redo",      sigc::mem_fun(*this, &Shell::on_redo));
    m_act_delete = add_action("delete",    sigc::mem_fun(*this, &Shell::on_delete));
    m_act_up     = add_action("move-up",   sigc::mem_fun(*this, &Shell::on_move_up));
    m_act_down   = add_action("move-down", sigc::mem_fun(*this, &Shell::on_move_down));

    // Open Recent: one parameterised action serves every row (recents.open(<path>)).
    auto group = Gio::SimpleActionGroup::create();
    group->add_action_with_parameter(
        "open", Glib::Variant<Glib::ustring>::variant_type(),
        [this](const Glib::VariantBase& param) {
            on_recent_open(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(param).get());
        });
    m_recents_clear = Gio::SimpleAction::create("clear");
    m_recents_clear->signal_activate().connect([this](const Glib::VariantBase&) { on_recent_clear(); });
    group->add_action(m_recents_clear);
    insert_action_group("recents", group);

    m_recents_file = recents_file();
    m_recents = core::load_recents(m_recents_file);
    rebuild_recents_menu();

    signal_close_request().connect(sigc::mem_fun(*this, &Shell::on_close), false);
}

void Shell::bind_model() {  // bindings: design <-> journal <-> panes
    // The one-door guard: the design asks the journal whether a write is
    // coming through it. The journal reads the selection (so undo brings it
    // back) and tells us when Undo / Redo change. Every write queues ONE repaint.
    m_design.set_door([this] { return m_journal.open(); });
    m_design.set_on_change([this] { queue_repaint(); });
    m_journal.set_selection_source([this] {
        return m_sel.empty() ? std::vector<std::string>{} : std::vector<std::string>{m_sel};
    });
    m_journal.set_on_change([this] { update_undo_ui(); });

    // What the panes report -> handlers. None of them writes the design itself
    // (the inspector writes only through core::design:: verbs).
    m_palette.signal_add().connect(sigc::mem_fun(*this, &Shell::on_add));
    m_tree.signal_select().connect(sigc::mem_fun(*this, &Shell::on_select));
    // s009: the navigator's project list.
    m_project.signal_open_form().connect(sigc::mem_fun(*this, &Shell::on_open_form));
    m_project.signal_remove_form().connect(sigc::mem_fun(*this, &Shell::on_remove_form));
    m_project.signal_remove_resource().connect(sigc::mem_fun(*this, &Shell::on_remove_resource));
    m_project.signal_open_style().connect(sigc::mem_fun(*this, &Shell::on_edit_style));
    m_project.signal_copy().connect([this](const std::string& text, const std::string& what) {
        get_clipboard()->set_text(text);
        say(what.empty() ? text : "Copied the " + what + ": " + text);
    });
    m_canvas.signal_pick().connect(sigc::mem_fun(*this, &Shell::on_select));
    m_tree.signal_drop().connect(sigc::mem_fun(*this, &Shell::on_drop));
    m_canvas.signal_drop().connect(sigc::mem_fun(*this, &Shell::on_drop));
    m_tree.signal_refused().connect([this](const std::string& s) { say("Can't drop there: " + s, 2); });
    m_canvas.signal_refused().connect([this](const std::string& s) { say("Can't drop there: " + s, 2); });
    m_inspector.signal_said().connect([this](const std::string& s) { say(s, 2); });
    m_inspector.signal_share().connect(sigc::mem_fun(*this, &Shell::on_share_model));   // s013
    m_inspector.signal_edit_shared().connect(sigc::mem_fun(*this, &Shell::on_edit_shared));
    m_inspector.signal_unshare().connect(sigc::mem_fun(*this, &Shell::on_unshare));
    m_inspector.signal_add_icon().connect([this](std::function<void(const std::string&)> pick) {   // s015 m2
        on_add_resource("icon", [this, pick](const std::string& name) {
            pick(name);
            say("Added icon " + name + " to the project and picked it -- rebuild the app to bundle it.");
        });
    });
    m_canvas.signal_style_said().connect([this](const std::string& s, int level) { say(s, level); });   // s012
    m_hooks.signal_select().connect([this](const std::string& name) {
        if (const auto k = m_design.find(name); !k.empty()) on_select(k);
    });
}

void Shell::bind_keys() {  // bindings: Ctrl+Z, Delete, Alt+arrows
    // NOT accelerators: an accel fires before the focused widget, so it would
    // take Ctrl+Z and Delete away from every Entry in the inspector. The WINDOW
    // hears these in the BUBBLE phase -- only keys nothing else took -- and
    // steps aside when the keyboard is in a text box (docs/undo.md, "the keys").
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            const auto mods = state & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK |
                                       Gdk::ModifierType::ALT_MASK);
            const auto CTRL = Gdk::ModifierType::CONTROL_MASK;
            const auto SHIFT = Gdk::ModifierType::SHIFT_MASK;
            const auto ALT = Gdk::ModifierType::ALT_MASK;
            const bool z = keyval == GDK_KEY_z || keyval == GDK_KEY_Z;
            const char* act = nullptr;
            if (z && mods == CTRL)                                           act = "win.undo";
            else if (z && mods == (CTRL | SHIFT))                            act = "win.redo";
            else if ((keyval == GDK_KEY_y || keyval == GDK_KEY_Y) && mods == CTRL) act = "win.redo";
            else if (keyval == GDK_KEY_Delete && mods == Gdk::ModifierType{}) act = "win.delete";
            else if (keyval == GDK_KEY_Up && mods == ALT)                    act = "win.move-up";
            else if (keyval == GDK_KEY_Down && mods == ALT)                  act = "win.move-down";
            if (!act || focus_is_text()) return false;
            if (auto lg = log::get(log::Area::Shell)) lg->debug("window key -> {}", act);
            activate_action(act);   // the FULL name: "undo" alone does nothing, silently
            return true;
        }, false);
    add_controller(keys);
}

}  // namespace mortimer
