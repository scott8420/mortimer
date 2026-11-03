#include "designer/WidgetCard.hpp"
#include "core/Catalog.hpp"
#include "core/WidgetAbout.hpp"

#include <gtkmm/button.h>
#include <gtkmm/calendar.h>
#include <gtkmm/centerbox.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/colordialog.h>
#include <gtkmm/colordialogbutton.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/entry.h>
#include <gtkmm/expander.h>
#include <gtkmm/fontdialog.h>
#include <gtkmm/fontdialogbutton.h>
#include <gtkmm/frame.h>
#include <gtkmm/grid.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/linkbutton.h>
#include <gtkmm/listbox.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/notebook.h>
#include <gtkmm/overlay.h>
#include <gtkmm/paned.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/scale.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/separator.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/stack.h>
#include <gtkmm/stacksidebar.h>
#include <gtkmm/stringlist.h>
#include <gtkmm/switch.h>
#include <gtkmm/textview.h>
#include <gtkmm/togglebutton.h>

#include <initializer_list>

namespace mortimer::designer {

namespace {

Gtk::Label* label(const std::string& text) { return Gtk::make_managed<Gtk::Label>(text); }

Gtk::Button* button(const std::string& text) { return Gtk::make_managed<Gtk::Button>(text); }

// A pane's stand-in: a framed label, so containers show their SHAPE.
Gtk::Widget* panel(const std::string& text) {
    auto* f = Gtk::make_managed<Gtk::Frame>();
    auto* l = label(text);
    l->set_margin(10);
    l->add_css_class("dim-label");
    f->set_child(*l);
    return f;
}

// A table-ish stand-in for the model-backed views: rows of cells.
Gtk::Widget* rows(std::initializer_list<std::initializer_list<const char*>> data, bool header) {
    auto* g = Gtk::make_managed<Gtk::Grid>();
    g->set_column_spacing(16);
    g->set_row_spacing(4);
    int r = 0;
    for (const auto& row : data) {
        int c = 0;
        for (const char* cell : row) {
            auto* l = label(cell);
            l->set_xalign(0);
            if (header && r == 0) l->add_css_class("heading");
            g->attach(*l, c++, r);
        }
        ++r;
    }
    return g;
}

}  // namespace

Gtk::Widget* make_sample(const std::string& t) {
    using O = Gtk::Orientation;
    if (t == "Box") {
        auto* b = Gtk::make_managed<Gtk::Box>(O::HORIZONTAL, 6);
        for (const char* s : {"One", "Two", "Three"}) b->append(*button(s));
        return b;
    }
    if (t == "Grid") {
        auto* g = Gtk::make_managed<Gtk::Grid>();
        g->set_row_spacing(6);
        g->set_column_spacing(8);
        int r = 0;
        for (const char* s : {"Name", "Email"}) {
            auto* l = label(s);
            l->set_xalign(1);
            g->attach(*l, 0, r);
            auto* e = Gtk::make_managed<Gtk::Entry>();
            e->set_hexpand(true);
            g->attach(*e, 1, r++);
        }
        return g;
    }
    if (t == "Paned") {
        auto* p = Gtk::make_managed<Gtk::Paned>(O::HORIZONTAL);
        p->set_start_child(*panel("Sidebar"));
        p->set_end_child(*panel("Document"));
        p->set_position(110);
        p->set_size_request(-1, 80);
        return p;
    }
    if (t == "Stack" || t == "StackSidebar") {
        auto* row = Gtk::make_managed<Gtk::Box>(O::HORIZONTAL, 0);
        auto* st = Gtk::make_managed<Gtk::Stack>();
        st->add(*panel("General settings"), "general", "General");
        st->add(*panel("Appearance settings"), "look", "Appearance");
        st->add(*panel("About this app"), "about", "About");
        auto* sb = Gtk::make_managed<Gtk::StackSidebar>();
        sb->set_stack(*st);
        st->set_hexpand(true);
        row->append(*sb);
        row->append(*st);
        row->set_size_request(-1, 110);
        return row;
    }
    if (t == "ScrolledWindow") {
        auto* sw = Gtk::make_managed<Gtk::ScrolledWindow>();
        auto* l = label("A long text, longer than the room it gets --\nline two\nline three\nline four\n"
                        "line five\nline six\nline seven");
        l->set_xalign(0);
        sw->set_child(*l);
        sw->set_size_request(-1, 80);
        sw->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::ALWAYS);
        return sw;
    }
    if (t == "Frame") {
        auto* f = Gtk::make_managed<Gtk::Frame>("Account");
        auto* b = Gtk::make_managed<Gtk::Box>(O::VERTICAL, 4);
        b->set_margin(8);
        b->append(*Gtk::make_managed<Gtk::CheckButton>("Remember me"));
        b->append(*Gtk::make_managed<Gtk::CheckButton>("Sign in on start"));
        f->set_child(*b);
        return f;
    }
    if (t == "Expander") {
        auto* x = Gtk::make_managed<Gtk::Expander>("Details");
        x->set_child(*label("Shown when the user opens it."));
        x->set_expanded(true);
        return x;
    }
    if (t == "Notebook") {
        auto* n = Gtk::make_managed<Gtk::Notebook>();
        for (const char* s : {"General", "Fonts", "Keys"}) n->append_page(*panel(std::string(s) + " page"), s);
        return n;
    }
    if (t == "Overlay") {
        auto* o = Gtk::make_managed<Gtk::Overlay>();
        auto* base = panel("The main child");
        base->set_size_request(-1, 80);
        o->set_child(*base);
        auto* badge = button("Over it");
        badge->set_halign(Gtk::Align::END);
        badge->set_valign(Gtk::Align::START);
        badge->set_margin(6);
        badge->add_css_class("suggested-action");
        o->add_overlay(*badge);
        return o;
    }
    if (t == "CenterBox") {
        auto* c = Gtk::make_managed<Gtk::CenterBox>();
        c->set_start_widget(*button("Start"));
        c->set_center_widget(*label("Center"));
        c->set_end_widget(*button("End"));
        return c;
    }
    if (t == "HeaderBar") {
        auto* h = Gtk::make_managed<Gtk::HeaderBar>();
        h->set_show_title_buttons(false);   // the real window's controls aren't the sample's
        auto* open = Gtk::make_managed<Gtk::Button>();
        open->set_icon_name("document-open-symbolic");
        h->pack_start(*open);
        h->set_title_widget(*label("My App"));
        auto* menu = Gtk::make_managed<Gtk::MenuButton>();
        menu->set_icon_name("open-menu-symbolic");
        h->pack_end(*menu);
        return h;
    }
    if (t == "ListBox") {
        auto* lb = Gtk::make_managed<Gtk::ListBox>();
        lb->add_css_class("boxed-list");
        for (const char* s : {"Wi-Fi", "Bluetooth", "Sound"}) {
            auto* r = Gtk::make_managed<Gtk::Box>(O::HORIZONTAL, 8);
            r->set_margin(6);
            auto* l = label(s);
            l->set_hexpand(true);
            l->set_xalign(0);
            r->append(*l);
            r->append(*Gtk::make_managed<Gtk::Switch>());
            lb->append(*r);
        }
        return lb;
    }
    if (t == "ListView") return rows({{"•  Buy milk"}, {"•  Call the vet"}, {"•  Water the garden"}}, false);
    if (t == "ColumnView")
        return rows({{"Title", "Due", "Done"}, {"Report", "Mon", "✓"}, {"Taxes", "Apr 15", ""}, {"Garden", "Sat", ""}}, true);
    if (t == "MenuButton") {
        auto* m = Gtk::make_managed<Gtk::MenuButton>();
        m->set_icon_name("open-menu-symbolic");
        m->set_halign(Gtk::Align::START);
        return m;
    }
    if (t == "Label") {
        auto* l = Gtk::make_managed<Gtk::Label>();
        l->set_markup("Text to read -- <b>bold</b>, <i>italic</i>, wrapped when it's long.");
        l->set_wrap(true);
        return l;
    }
    if (t == "Button") {
        auto* b = Gtk::make_managed<Gtk::Box>(O::HORIZONTAL, 6);
        b->append(*button("Save"));
        auto* s = button("Send");
        s->add_css_class("suggested-action");
        b->append(*s);
        auto* i = Gtk::make_managed<Gtk::Button>();
        i->set_icon_name("edit-delete-symbolic");
        b->append(*i);
        return b;
    }
    if (t == "ToggleButton") {
        auto* b = Gtk::make_managed<Gtk::Box>(O::HORIZONTAL, 0);
        b->add_css_class("linked");
        Gtk::ToggleButton* first = nullptr;
        for (const char* s : {"Day", "Week", "Month"}) {
            auto* tb = Gtk::make_managed<Gtk::ToggleButton>(s);
            if (first) tb->set_group(*first); else { first = tb; tb->set_active(true); }
            b->append(*tb);
        }
        return b;
    }
    if (t == "CheckButton") {
        auto* b = Gtk::make_managed<Gtk::Box>(O::VERTICAL, 2);
        auto* a = Gtk::make_managed<Gtk::CheckButton>("Show hidden files");
        a->set_active(true);
        b->append(*a);
        b->append(*Gtk::make_managed<Gtk::CheckButton>("Sort folders first"));
        return b;
    }
    if (t == "Switch") {
        auto* b = Gtk::make_managed<Gtk::Box>(O::HORIZONTAL, 12);
        auto* l = label("Dark style");
        l->set_hexpand(true);
        l->set_xalign(0);
        b->append(*l);
        auto* s = Gtk::make_managed<Gtk::Switch>();
        s->set_active(true);
        b->append(*s);
        return b;
    }
    if (t == "LinkButton") return Gtk::make_managed<Gtk::LinkButton>("https://www.gtk.org", "The GTK website");
    if (t == "Entry") {
        auto* e = Gtk::make_managed<Gtk::Entry>();
        e->set_placeholder_text("Your name");
        return e;
    }
    if (t == "SearchEntry") {
        auto* e = Gtk::make_managed<Gtk::SearchEntry>();
        e->set_text("gtkmm");
        return e;
    }
    if (t == "SpinButton") {
        auto* s = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(12, 0, 100, 1, 10));
        s->set_halign(Gtk::Align::START);
        return s;
    }
    if (t == "Scale") {
        auto* s = Gtk::make_managed<Gtk::Scale>(Gtk::Adjustment::create(60, 0, 100, 1, 10), O::HORIZONTAL);
        s->set_draw_value(true);
        return s;
    }
    if (t == "DropDown") {
        auto* d = Gtk::make_managed<Gtk::DropDown>(Gtk::StringList::create({"Centimetres", "Inches", "Points"}));
        d->set_halign(Gtk::Align::START);
        return d;
    }
    if (t == "TextView") {
        auto* sw = Gtk::make_managed<Gtk::ScrolledWindow>();
        auto* tv = Gtk::make_managed<Gtk::TextView>();
        tv->get_buffer()->set_text("Many lines of text,\nedited in place.\n");
        tv->set_margin(4);
        sw->set_child(*tv);
        sw->set_size_request(-1, 70);
        sw->add_css_class("frame");
        return sw;
    }
    if (t == "ProgressBar") {
        auto* p = Gtk::make_managed<Gtk::ProgressBar>();
        p->set_fraction(0.62);
        p->set_show_text(true);
        return p;
    }
    if (t == "Separator") {
        auto* b = Gtk::make_managed<Gtk::Box>(O::VERTICAL, 6);
        b->append(*label("Above"));
        b->append(*Gtk::make_managed<Gtk::Separator>(O::HORIZONTAL));
        b->append(*label("Below"));
        return b;
    }
    if (t == "Image") {
        auto* b = Gtk::make_managed<Gtk::Box>(O::HORIZONTAL, 12);
        for (const char* n : {"starred-symbolic", "folder-symbolic", "emblem-photos-symbolic"}) {
            auto* i = Gtk::make_managed<Gtk::Image>();
            i->set_from_icon_name(n);
            i->set_pixel_size(32);
            b->append(*i);
        }
        return b;
    }
    if (t == "Calendar") return Gtk::make_managed<Gtk::Calendar>();
    if (t == "ColorDialogButton") {
        auto* c = Gtk::make_managed<Gtk::ColorDialogButton>(Gtk::ColorDialog::create());
        c->set_rgba(Gdk::RGBA("#3584e4"));
        c->set_halign(Gtk::Align::START);
        return c;
    }
    if (t == "FontDialogButton") {
        auto* f = Gtk::make_managed<Gtk::FontDialogButton>(Gtk::FontDialog::create());
        f->set_halign(Gtk::Align::START);
        return f;
    }
    auto* l = label(t);   // a type without a sample (a form root): its name
    l->add_css_class("dim-label");
    return l;
}

WidgetCard::WidgetCard() {
    set_child(m_box);
    m_box.set_margin(12);
    m_box.set_size_request(340, -1);
    add_css_class("mortimer-widget-card");
}

void WidgetCard::show_for(const std::string& type, const Gdk::Rectangle& at) {
    while (auto* c = m_box.get_first_child()) m_box.remove(*c);
    const core::WidgetSpec* spec = core::find_widget(type);
    const core::WidgetAbout about = core::widget_about(type);

    auto* head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto* name = label(type);
    name->add_css_class("title-3");
    name->set_xalign(0);
    name->set_hexpand(true);
    head->append(*name);
    if (spec) {
        auto* cls = label(spec->gtk_class);
        cls->add_css_class("dim-label");
        cls->add_css_class("monospace");
        head->append(*cls);
    }
    m_box.append(*head);

    // The picture: a live sample, in a frame, taking no clicks.
    auto* stage = Gtk::make_managed<Gtk::Frame>();
    stage->add_css_class("view");
    auto* sample = make_sample(type);
    sample->set_margin(12);
    stage->set_child(*sample);
    stage->set_can_target(false);   // it's a picture: a click on it does nothing
    stage->set_focusable(false);
    m_box.append(*stage);

    auto* text = label(about.about.empty() ? (spec ? spec->doc : type) : about.about);
    text->set_wrap(true);
    text->set_xalign(0);
    text->set_max_width_chars(44);
    m_box.append(*text);
    if (!about.tip.empty()) {
        auto* tip = label(about.tip);
        tip->set_wrap(true);
        tip->set_xalign(0);
        tip->set_max_width_chars(44);
        tip->add_css_class("dim-label");
        m_box.append(*tip);
    }

    // More…: GTK's own pages, opened in the browser.
    auto* more = Gtk::make_managed<Gtk::Expander>("More…");
    auto* links = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    const std::string cls = spec ? (spec->gtk_class.rfind("Gtk::", 0) == 0 ? "Gtk" + spec->gtk_class.substr(5)
                                                                           : spec->gtk_class)
                                 : type;
    if (const std::string url = core::widget_doc_url(type); !url.empty()) {
        auto* doc = Gtk::make_managed<Gtk::LinkButton>(url, cls + " on docs.gtk.org");
        doc->set_halign(Gtk::Align::START);
        doc->set_tooltip_text(url);
        links->append(*doc);
    }
    auto* gallery = Gtk::make_managed<Gtk::LinkButton>(core::kWidgetGalleryUrl, "Every GTK widget, pictured");
    gallery->set_halign(Gtk::Align::START);
    gallery->set_tooltip_text(core::kWidgetGalleryUrl);
    links->append(*gallery);
    more->set_child(*links);
    m_box.append(*more);

    set_pointing_to(at);
    popup();
}

}  // namespace mortimer::designer
