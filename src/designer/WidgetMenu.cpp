#include "designer/WidgetMenu.hpp"

#include <giomm/menuitem.h>
#include <glibmm/variant.h>

namespace mortimer::designer {

namespace {
void keyed(const Glib::RefPtr<Gio::Menu>& m, const Glib::ustring& label, const Glib::ustring& action,
           const Glib::ustring& accel) {
    auto item = Gio::MenuItem::create(label, action);
    item->set_attribute_value("accel", Glib::Variant<Glib::ustring>::create(accel));
    m->append_item(item);
}
}  // namespace

Glib::RefPtr<Gio::Menu> widget_menu(const std::string& type, const std::string& card_action) {
    auto menu = Gio::Menu::create();
    auto edit = Gio::Menu::create();
    keyed(edit, "Cut", "win.cut", "<Ctrl>x");
    keyed(edit, "Copy", "win.copy", "<Ctrl>c");
    keyed(edit, "Paste", "win.paste", "<Ctrl>v");
    keyed(edit, "Duplicate", "win.duplicate", "<Ctrl>d");
    keyed(edit, "Delete", "win.delete", "Delete");
    menu->append_section(edit);
    auto move = Gio::Menu::create();
    keyed(move, "Move Up", "win.move-up", "<Alt>Up");
    keyed(move, "Move Down", "win.move-down", "<Alt>Down");
    menu->append_section(move);
    auto what = Gio::Menu::create();
    const bool vowel = !type.empty() && std::string("AEIOU").find(type[0]) != std::string::npos;
    what->append(std::string("What is ") + (vowel ? "an " : "a ") + type + "?", card_action);
    menu->append_section(what);
    return menu;
}

}  // namespace mortimer::designer
