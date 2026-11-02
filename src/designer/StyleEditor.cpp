#include "designer/StyleEditor.hpp"
#include "Log.hpp"
#include "Registry.hpp"

#include <gtkmm/alertdialog.h>
#include <gtkmm/box.h>
#include <gtkmm/shortcut.h>
#include <gtkmm/shortcutaction.h>
#include <gtkmm/shortcutcontroller.h>
#include <gtkmm/shortcuttrigger.h>
#include <glibmm/fileutils.h>
#include <glibmm/main.h>

#include <filesystem>

namespace mortimer::designer {

StyleEditor::StyleEditor(Canvas& canvas) : m_canvas(canvas) {
    set_name("designer.style");
    registry::add("designer.style", this);
    set_default_size(560, 520);
    set_hide_on_close(true);   // built once, shown again

    m_title.add_css_class("title");
    m_bar.set_title_widget(m_title);
    m_save.add_css_class("suggested-action");
    m_save.set_tooltip_text("Write the file (Ctrl+S) -- the app loads it on its next build");
    m_revert.set_tooltip_text("Back to the file as it is on disk");
    m_bar.pack_end(m_save);
    m_bar.pack_start(m_revert);
    set_titlebar(m_bar);

    m_text.set_monospace(true);
    m_text.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    m_text.set_left_margin(8);
    m_text.set_top_margin(8);
    m_scroll.set_child(m_text);
    m_scroll.set_vexpand(true);
    m_status.set_xalign(0);
    m_status.set_wrap(true);
    m_status.set_margin(6);
    m_status.add_css_class("dim-label");
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    box->append(m_scroll);
    box->append(m_status);
    set_child(*box);

    m_error_tag = m_text.get_buffer()->create_tag("error");
    m_error_tag->property_underline() = Pango::Underline::ERROR;

    m_text.get_buffer()->signal_changed().connect(sigc::mem_fun(*this, &StyleEditor::changed));
    m_save.signal_clicked().connect(sigc::mem_fun(*this, &StyleEditor::save));
    m_revert.signal_clicked().connect(sigc::mem_fun(*this, &StyleEditor::revert));
    auto keys = Gtk::ShortcutController::create();
    keys->add_shortcut(Gtk::Shortcut::create(Gtk::ShortcutTrigger::parse_string("<Control>s"),
                                             Gtk::CallbackAction::create([this](Gtk::Widget&, const Glib::VariantBase&) {
                                                 save();
                                                 return true;
                                             })));
    add_controller(keys);
}

StyleEditor::~StyleEditor() {
    m_later.disconnect();
    registry::remove(this);
}

bool StyleEditor::dirty() const { return m_text.get_buffer()->get_text().raw() != m_saved; }

void StyleEditor::open(const std::string& path, Gtk::Window& parent) {
    set_transient_for(parent);
    if (path != m_path || !dirty()) {
        m_path = path;
        load();
    }
    present();
}

void StyleEditor::load() {
    std::string text;
    try { text = Glib::file_get_contents(m_path); } catch (const Glib::Error&) {}
    m_saved = text;
    m_loading = true;
    m_text.get_buffer()->set_text(text);
    m_loading = false;
    m_canvas.preview_stylesheet(std::nullopt);   // the canvas is on the file again
    m_status.set_text(std::filesystem::path(m_path).string());
    show_error();
    update_title();
}

void StyleEditor::update_title() {
    const std::string name = std::filesystem::path(m_path).filename().string();
    m_title.set_text((dirty() ? "• " : "") + name);
    set_title(name);
    m_save.set_sensitive(dirty());
    m_revert.set_sensitive(dirty());
}

void StyleEditor::changed() {
    if (m_loading) return;
    update_title();
    m_later.disconnect();
    m_later = Glib::signal_timeout().connect([this] { preview(); return false; }, 300);
}

void StyleEditor::preview() {
    m_canvas.preview_stylesheet(m_text.get_buffer()->get_text().raw());
    show_error();
}

void StyleEditor::show_error() {
    auto buf = m_text.get_buffer();
    buf->remove_tag(m_error_tag, buf->begin(), buf->end());
    const int line = m_canvas.style_error_line();
    if (line <= 0) {
        m_status.remove_css_class("error");
        m_status.add_css_class("dim-label");
        m_status.set_text(dirty() ? "Previewing on the canvas -- Save writes the file" : m_path);
        return;
    }
    m_status.remove_css_class("dim-label");
    m_status.add_css_class("error");
    m_status.set_text(m_canvas.style_error());
    auto from = buf->get_iter_at_line(line - 1);
    auto to = from;
    to.forward_to_line_end();
    buf->apply_tag(m_error_tag, from, to);
}

void StyleEditor::save() {
    if (!dirty()) return;
    const std::string text = m_text.get_buffer()->get_text().raw();
    try {
        Glib::file_set_contents(m_path, text);   // whole and atomic: a crash never leaves half of it
    } catch (const Glib::Error& e) {
        m_status.set_text("Couldn't save: " + std::string(e.what()));
        return;
    }
    m_saved = text;
    m_later.disconnect();
    m_canvas.preview_stylesheet(std::nullopt);   // the file now says the same; the watch reloads it
    show_error();
    update_title();
    m_status.set_text("Saved " + m_path + " -- rebuild the app to bundle it");
    if (auto lg = log::get(log::Area::Io)) lg->info("saved stylesheet '{}'", m_path);
}

void StyleEditor::revert() {
    load();
}

bool StyleEditor::on_close_request() {
    if (!dirty()) return Gtk::Window::on_close_request();
    auto alert = Gtk::AlertDialog::create("Save changes to " + std::filesystem::path(m_path).filename().string() + "?");
    alert->set_detail("If you don't save, the canvas goes back to the file as it is.");
    alert->set_buttons({"Cancel", "Discard", "Save"});
    alert->set_cancel_button(0);
    alert->set_default_button(2);
    alert->choose(*this, [this, alert](const Glib::RefPtr<Gio::AsyncResult>& result) {
        int which = 0;
        try { which = alert->choose_finish(result); } catch (const Glib::Error&) { return; }
        if (which == 1) { load(); set_visible(false); }
        else if (which == 2) { save(); if (!dirty()) set_visible(false); }
    });
    return true;   // the answer closes it (or not)
}

}  // namespace mortimer::designer
