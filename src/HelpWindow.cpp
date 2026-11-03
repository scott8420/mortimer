#include "HelpWindow.hpp"
#include "core/Help.hpp"
#include "Registry.hpp"

#include <gtkmm/label.h>
#include <glibmm/main.h>

#include <algorithm>
#include <cctype>

namespace mortimer {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

HelpWindow::HelpWindow() {
    set_name("shell.help");
    registry::add("shell.help", this);
    set_title("Mortimer Help");
    m_header.set_title_widget(m_title);
    m_title.set_markup("<b>Mortimer Help</b>");
    set_titlebar(m_header);
    set_modal(false);
    set_default_size(980, 700);
    set_hide_on_close(true);   // built once by the Shell, presented again

    m_search.set_placeholder_text("Search the guide");
    m_search.set_margin(8);
    m_search.signal_search_changed().connect(sigc::mem_fun(*this, &HelpWindow::on_search_changed));
    m_list.add_css_class("navigation-sidebar");
    m_list.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (m_filling || !row) return;
        const auto i = static_cast<std::size_t>(row->get_index());
        if (i < m_shown.size() && m_shown[i] != m_current) paint(m_shown[i]);
    });
    m_list_scroll.set_child(m_list);
    m_list_scroll.set_vexpand(true);
    m_list_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_side.append(m_search);
    m_side.append(m_list_scroll);
    m_side.set_size_request(230, -1);

    m_page.set_editable(false);
    m_page.set_cursor_visible(false);
    m_page.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    m_page.set_left_margin(28);
    m_page.set_right_margin(28);
    m_page.set_top_margin(20);
    m_page.set_bottom_margin(28);
    m_page.set_pixels_below_lines(2);
    m_page_scroll.set_child(m_page);
    m_page_scroll.set_hexpand(true);

    // The marks a page is painted with. Backgrounds carry their own alpha, so
    // they read on a light theme and a dark one alike.
    auto tags = m_page.get_buffer()->get_tag_table();
    auto make = [&](const char* name) { auto t = Gtk::TextTag::create(name); tags->add(t); return t; };
    auto h1 = make("h1"); h1->property_scale() = 1.7; h1->property_weight() = 800; h1->property_pixels_below_lines() = 10;
    auto h2 = make("h2"); h2->property_scale() = 1.3; h2->property_weight() = 700;
    h2->property_pixels_above_lines() = 18; h2->property_pixels_below_lines() = 6;
    auto h3 = make("h3"); h3->property_scale() = 1.1; h3->property_weight() = 700; h3->property_pixels_above_lines() = 12;
    auto bold = make("bold"); bold->property_weight() = 700;
    auto italic = make("italic"); italic->property_style() = Pango::Style::ITALIC;
    auto code = make("code"); code->property_family() = "monospace";
    code->property_background_rgba() = Gdk::RGBA(0.5, 0.5, 0.5, 0.15);
    auto block = make("block"); block->property_family() = "monospace";
    block->property_paragraph_background_rgba() = Gdk::RGBA(0.5, 0.5, 0.5, 0.12);
    block->property_left_margin() = 44; block->property_pixels_above_lines() = 2;
    auto item = make("item"); item->property_left_margin() = 44; item->property_indent() = -16;
    item->property_pixels_above_lines() = 3;
    auto table = make("table"); table->property_family() = "monospace"; table->property_left_margin() = 44;
    auto para = make("para"); para->property_pixels_above_lines() = 6; para->property_pixels_below_lines() = 4;
    auto hit = make("hit"); hit->property_background_rgba() = Gdk::RGBA(1.0, 0.78, 0.0, 0.45);

    m_split.set_start_child(m_side);
    m_split.set_end_child(m_page_scroll);
    m_split.set_resize_start_child(false);
    m_split.set_shrink_start_child(false);
    set_child(m_split);
    on_search_changed();   // every page listed, the first shown
}

HelpWindow::~HelpWindow() { registry::remove(this); }

void HelpWindow::show(Gtk::Window& parent) {
    set_transient_for(parent);
    present();
    m_search.grab_focus();
}

void HelpWindow::show_page(std::size_t page) {
    for (std::size_t i = 0; i < m_shown.size(); ++i)
        if (m_shown[i] == page) {
            if (auto* row = m_list.get_row_at_index(static_cast<int>(i))) m_list.select_row(*row);
        }
    paint(page);
}

void HelpWindow::on_search_changed() {
    const auto& pages = core::help_pages();
    const std::string q = m_search.get_text();
    m_words.clear();
    for (std::size_t a = 0, b; a < q.size(); a = b + 1) {
        b = q.find(' ', a);
        if (b == std::string::npos) b = q.size();
        if (b > a) m_words.push_back(lower(q.substr(a, b - a)));
    }
    m_shown.clear();
    std::vector<int> hits;
    if (m_words.empty()) {
        for (std::size_t i = 0; i < pages.size(); ++i) { m_shown.push_back(i); hits.push_back(0); }
    } else {
        for (const auto& h : core::help_search(q)) { m_shown.push_back(h.page); hits.push_back(h.hits); }
    }
    m_filling = true;
    while (auto* row = m_list.get_row_at_index(0)) m_list.remove(*row);
    for (std::size_t i = 0; i < m_shown.size(); ++i) {
        auto* l = Gtk::make_managed<Gtk::Label>(core::help_title(pages[m_shown[i]]) +
                                                (hits[i] > 0 && hits[i] < 1000 ? "  (" + std::to_string(hits[i]) + ")" : ""));
        l->set_xalign(0);
        l->set_margin(6);
        m_list.append(*l);
    }
    if (m_shown.empty()) {
        auto* l = Gtk::make_managed<Gtk::Label>("Nothing mentions that.");
        l->add_css_class("dim-label");
        l->set_margin(12);
        m_list.append(*l);
    }
    m_filling = false;
    if (!m_shown.empty()) {
        if (auto* row = m_list.get_row_at_index(0)) m_list.select_row(*row);
        paint(m_shown.front());
    }
}

void HelpWindow::insert_spans(Gtk::TextBuffer::iterator& at, const std::string& line,
                              const std::vector<Glib::RefPtr<Gtk::TextTag>>& extra) {
    auto buf = m_page.get_buffer();
    auto table = buf->get_tag_table();
    for (const auto& s : core::help_spans(line)) {
        std::vector<Glib::RefPtr<Gtk::TextTag>> tags = extra;
        if (s.bold) tags.push_back(table->lookup("bold"));
        if (s.code) tags.push_back(table->lookup("code"));
        if (s.italic) tags.push_back(table->lookup("italic"));
        at = tags.empty() ? buf->insert(at, s.text) : buf->insert_with_tags(at, s.text, tags);
    }
}

void HelpWindow::paint(std::size_t page) {
    const auto& pages = core::help_pages();
    if (page >= pages.size()) return;
    m_current = page;
    auto buf = m_page.get_buffer();
    auto table = buf->get_tag_table();
    buf->set_text("");
    auto at = buf->end();
    using K = core::HelpBlock::Kind;
    bool first = true;
    for (const auto& b : core::help_blocks(pages[page].text)) {
        if (!first) at = buf->insert(at, "\n");
        first = false;
        switch (b.kind) {
            case K::H1: insert_spans(at, b.text, {table->lookup("h1")}); break;
            case K::H2: insert_spans(at, b.text, {table->lookup("h2")}); break;
            case K::H3: insert_spans(at, b.text, {table->lookup("h3")}); break;
            case K::Para: insert_spans(at, b.text, {table->lookup("para")}); break;
            case K::Bullet:
                at = buf->insert_with_tag(at, "•  ", table->lookup("item"));
                insert_spans(at, b.text, {table->lookup("item")});
                break;
            case K::Number: insert_spans(at, b.text, {table->lookup("item")}); break;
            case K::Code: at = buf->insert_with_tag(at, b.text, table->lookup("block")); break;
            case K::Table: {
                // Columns padded to their widest cell (counted in characters, not bytes).
                auto chars = [](const std::string& s) {
                    std::size_t n = 0;
                    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
                    return n;
                };
                auto plain = [](const std::string& s) {
                    std::string o;
                    for (const auto& sp : core::help_spans(s)) o += sp.text;
                    return o;
                };
                std::vector<std::size_t> w;
                for (const auto& r : b.rows)
                    for (std::size_t c = 0; c < r.size(); ++c) {
                        if (w.size() <= c) w.push_back(0);
                        w[c] = std::max(w[c], chars(plain(r[c])));
                    }
                for (std::size_t r = 0; r < b.rows.size(); ++r) {
                    std::string line;
                    for (std::size_t c = 0; c < b.rows[r].size(); ++c) {
                        const std::string cell = plain(b.rows[r][c]);
                        line += cell + (c + 1 < b.rows[r].size() ? std::string(w[c] - chars(cell) + 3, ' ') : "");
                    }
                    std::vector<Glib::RefPtr<Gtk::TextTag>> tags{table->lookup("table")};
                    if (r == 0) tags.push_back(table->lookup("bold"));
                    at = buf->insert_with_tags(at, line + (r + 1 < b.rows.size() ? "\n" : ""), tags);
                }
                break;
            }
        }
    }
    mark_words();
}

void HelpWindow::mark_words() {
    auto buf = m_page.get_buffer();
    const std::string text = lower(buf->get_text());
    Gtk::TextBuffer::iterator first_hit;
    bool any = false;
    for (const auto& w : m_words) {
        // Find in the lowered copy, map the byte offset back to characters.
        for (auto at = text.find(w); !w.empty() && at != std::string::npos; at = text.find(w, at + w.size())) {
            const auto chars_before = g_utf8_strlen(text.c_str(), static_cast<gssize>(at));
            const auto chars_word = g_utf8_strlen(w.c_str(), static_cast<gssize>(w.size()));
            auto a = buf->get_iter_at_offset(static_cast<int>(chars_before));
            auto b = buf->get_iter_at_offset(static_cast<int>(chars_before + chars_word));
            buf->apply_tag_by_name("hit", a, b);
            if (!any || a.get_offset() < first_hit.get_offset()) first_hit = a;
            any = true;
        }
    }
    auto top = buf->begin();
    auto mark = buf->create_mark(any ? first_hit : top);
    // After the layout settles, or the view scrolls to a line not yet measured.
    Glib::signal_idle().connect_once([this, mark] {
        m_page.scroll_to(mark, 0.1);
        m_page.get_buffer()->delete_mark(mark);
    });
}

}  // namespace mortimer
