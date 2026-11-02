#include "designer/HooksPane.hpp"
#include "core/Codegen.hpp"

#include <gdkmm/clipboard.h>
#include <gtkmm/label.h>
#include <gtkmm/listboxrow.h>

namespace mortimer::designer {

HooksPane::HooksPane()
    : widgets::Box("designer.hooks", Gtk::Orientation::VERTICAL),
      m_summary("designer.hooks.summary"),
      m_copy("designer.hooks.copy"),
      m_note("designer.hooks.note"),
      m_scroll("designer.hooks.scroll"),
      m_list("designer.hooks.list") {
    add_css_class("mortimer-hooks");
    auto* head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* title = Gtk::make_managed<Gtk::Label>("Hooks");
    title->add_css_class("mortimer-pane-title");
    head->append(*title);
    m_summary.add_css_class("dim-label");
    m_summary.set_hexpand(true);
    m_summary.set_xalign(0);
    head->append(m_summary);
    m_copy.set_label("Copy lines to paste");
    m_copy.set_has_frame(false);
    m_copy.set_tooltip_text("The override declarations for your header and empty bodies for your .cpp");
    m_copy.set_margin_end(6);
    m_copy.signal_clicked().connect([this] {
        if (!m_paste.empty()) get_clipboard()->set_text(m_paste);
    });
    head->append(m_copy);
    append(*head);

    m_note.add_css_class("doc");
    m_note.set_xalign(0);
    m_note.set_wrap(true);
    m_note.set_margin_start(10);
    m_note.set_margin_end(10);
    append(m_note);

    m_list.set_selection_mode(Gtk::SelectionMode::NONE);
    m_list.set_activate_on_single_click(true);
    m_list.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const auto i = static_cast<std::size_t>(row->get_index());
        if (i < m_row_widgets.size()) m_select.emit(m_row_widgets[i]);
    });
    m_scroll.set_child(m_list);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    append(m_scroll);
}

void HooksPane::show(const core::HookReport& r, const std::string& app_dir) {
    while (auto* row = m_list.get_row_at_index(0)) m_list.remove(*row);
    m_row_widgets.clear();

    int hooked = 0;
    for (const auto& h : r.hooks) hooked += h.hooked ? 1 : 0;
    std::string sum = std::to_string(r.hooks.size()) + " ticked · " + std::to_string(hooked) + " hooked · " +
                      std::to_string(r.unhooked_count()) + " unhooked";
    if (!r.missing.empty()) sum += " · " + std::to_string(r.missing.size()) + " MISSING";
    if (!r.stale.empty()) sum += " · " + std::to_string(r.stale.size()) + " STALE";
    m_summary.set_text(sum);

    if (app_dir.empty())
        m_note.set_text("Not in a project -- nothing can be hooked yet. Save the form into an app's forms/ "
                        "(or File ▸ New Project) and your include/" + r.form + ".hpp is read here.");
    else if (!r.header_found)
        m_note.set_text("include/" + r.form + ".hpp doesn't exist yet -- Save writes it (once) with an "
                        "override for every hook.");
    else if (r.hooks.empty())
        m_note.set_text("No signals ticked. Tick one in the inspector's Signals to get a hook.");
    else
        m_note.set_text("Against " + app_dir + "/include/" + r.form + ".hpp -- live, before you save.");

    auto add = [&](const std::string& tag, const std::string& css, const std::string& decl,
                   const std::string& where, const std::string& widget) {
        auto* line = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        line->set_margin_start(10);
        auto* t = Gtk::make_managed<Gtk::Label>(tag);
        t->add_css_class("tag");
        t->add_css_class(css);
        t->set_size_request(76, -1);
        t->set_xalign(0);
        auto* d = Gtk::make_managed<Gtk::Label>(decl);
        d->add_css_class("decl");
        d->set_xalign(0);
        d->set_selectable(true);
        d->set_focusable(false);
        auto* w = Gtk::make_managed<Gtk::Label>(where);
        w->add_css_class("dim-label");
        w->set_xalign(0);
        w->set_hexpand(true);
        line->append(*t);
        line->append(*d);
        line->append(*w);
        m_list.append(*line);
        m_row_widgets.push_back(widget);
    };
    for (const auto& h : r.hooks) {
        const bool missing = !h.hooked && h.hook.required;
        add(h.hooked ? "hooked" : missing ? "MISSING" : "unhooked", h.hooked ? "ok" : missing ? "bad" : "warn",
            h.hook.decl(), h.hook.widget + " · " + h.hook.signal + (h.hook.required ? " · required" : ""),
            h.hook.widget);
    }
    for (const auto& s : r.stale)
        add("STALE", "bad", s, "overridden in your header, gone from the form -- the build rejects it", "");

    m_paste.clear();
    std::string decls, defs;
    for (const auto& h : r.hooks)
        if (!h.hooked) {
            decls += core::override_decl(h.hook) + "\n";
            defs += "\n" + core::override_def(h.hook, r.form);
        }
    if (!decls.empty())
        m_paste = "// include/" + r.form + ".hpp (class " + r.form + ", protected:)\n" + decls +
                  "\n// src/" + r.form + ".cpp\n" + defs;
    m_copy.set_sensitive(!m_paste.empty());
}

}  // namespace mortimer::designer
