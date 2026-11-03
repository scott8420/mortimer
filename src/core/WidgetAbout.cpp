#include "core/WidgetAbout.hpp"
#include "core/Catalog.hpp"

#include <map>

namespace mortimer::core {

WidgetAbout widget_about(const std::string& type) {
    static const std::map<std::string, WidgetAbout> kAbout = {
        // ── form roots ─────────────────────────────────────────────────────
        {"ApplicationWindow", {"Your app's main window. It holds one child (usually a Box) and a title bar, and it "
                               "owns the app's win.* actions -- the ones its menus and keys run.",
                               "One per app is usual; other windows are Window or Dialog forms."}},
        {"Window", {"A secondary window -- a tool palette, an inspector, a preferences window. One child and a "
                    "title bar, like the main window, without the app's actions.",
                    "Your code makes one and calls present()."}},
        {"Dialog", {"A window that asks or tells something over another window, and blocks it while it's up. It "
                    "hides when closed, so you build it once and show it again.",
                    "Show it with present_for(*this) from the window it belongs to."}},
        // ── containers ─────────────────────────────────────────────────────
        {"Box", {"Lines its children up in a row or a column, in order. The workhorse of layout: most windows are "
                 "Boxes inside Boxes.",
                 "A child's hexpand / vexpand decides who takes the spare room."}},
        {"Grid", {"Places children on rows and columns, like a table; a child can span several cells. Good for "
                  "forms: labels in one column, fields in the next.",
                  "Each child says its column and row in its packing."}},
        {"Paned", {"Two panes with a divider the user drags to share the space -- a sidebar beside a document, a "
                   "list above its details.",
                   "Its window's remember keeps where the divider was left."}},
        {"Stack", {"Holds several pages and shows one at a time. Pair it with a StackSidebar or a StackSwitcher "
                   "to let the user choose, or switch pages from your code.",
                   "Each child is a page with a name and a title."}},
        {"StackSidebar", {"A list of a Stack's page titles; clicking one shows that page. The classic settings "
                          "window: categories on the left, the page on the right.",
                          "Point its stack property at the Stack it drives."}},
        {"ScrolledWindow", {"A viewport with scroll bars around one child that may be bigger than the room it "
                            "gets -- long lists, big text, a large drawing.",
                            "Lists and TextViews nearly always go inside one."}},
        {"Frame", {"A border, with an optional label, around one child -- groups related controls so they read "
                   "as one block.", ""}},
        {"Expander", {"A heading with a triangle that shows or hides one child -- for details most users don't "
                      "need to see all the time.", ""}},
        {"Notebook", {"Pages behind tabs along one edge; clicking a tab shows its page. The older cousin of "
                      "Stack + StackSwitcher, still handy when tabs are what you want.",
                      "Each child's title is its tab's text."}},
        {"Overlay", {"Stacks children on top of one another: one main child, others floating over it -- a badge "
                     "on a picture, a button over a map, a message over a view.", ""}},
        {"CenterBox", {"Three slots in a row: start, center, end. The center stays truly centered whatever the "
                       "sides hold -- a toolbar with a title in the middle.", ""}},
        {"HeaderBar", {"The modern GNOME title bar: buttons at the start and end, the title in the middle, the "
                       "window's own controls at the corner. Put it in a window's titlebar slot.",
                       "Fewer menus, more buttons in the header -- the GNOME style."}},
        {"ListBox", {"A vertical list of rows you design yourself; each row can hold any widgets. For short lists "
                     "of rich rows -- settings, accounts.",
                     "For long lists of data, ListView or ColumnView scale far better."}},
        {"ListView", {"A fast list over a model: one row you design, repeated for each item, built only for the "
                      "rows on screen. Thousands of items stay quick.",
                      "Its rows show a model's fields -- set the model and design the row."}},
        {"ColumnView", {"A table over a model: columns with headers the user can sort and resize, one row per "
                        "item. Spreadsheets lite -- files, records, tasks.",
                        "Columns show the model's fields; cells can be editable."}},
        {"MenuButton", {"A button that opens a menu or a popover when clicked -- the hamburger in a header bar, "
                        "an options menu beside a field.",
                        "Its menu rows run actions (win.something)."}},
        // ── controls ───────────────────────────────────────────────────────
        {"Label", {"Shows text the user reads but doesn't edit -- titles, captions, explanations. It can wrap, "
                   "ellipsize, and take simple markup for bold or italics.", ""}},
        {"Button", {"Does something when clicked. Shows a label, an icon, or both.",
                    "Its clicked signal is the hook your code fills in."}},
        {"ToggleButton", {"A button that stays pressed in or out -- on / off, shown as a button. Several can be "
                          "grouped so only one is in at a time.", ""}},
        {"CheckButton", {"A box with a tick and a label -- yes / no for one option. Grouped, they become radio "
                         "buttons: one choice of several.", ""}},
        {"Switch", {"An on / off slider for a setting that takes effect at once -- Wi-Fi on, dark style on.",
                    "Use a CheckButton when the choice is applied later (with an OK)."}},
        {"LinkButton", {"A web-style link: underlined text that opens a URL in the user's browser when clicked.",
                        ""}},
        {"Entry", {"One line of text the user types -- a name, a path, a search.",
                   "Its changed signal fires on every keystroke; activate fires on Enter."}},
        {"SearchEntry", {"An Entry made for searching: a magnifier icon, a clear button, and a search_changed "
                         "signal that waits for the user to pause before it fires.", ""}},
        {"SpinButton", {"A number field with - and + buttons, kept between a minimum and a maximum, stepping by a "
                        "set amount.", ""}},
        {"Scale", {"A slider over a range of numbers -- volume, zoom, opacity. Horizontal or vertical, with or "
                   "without the value shown.", ""}},
        {"DropDown", {"Shows the current choice; click it for the list of the others. One choice from a fixed set "
                      "-- units, sizes, modes.",
                      "Its strings property is the list."}},
        {"TextView", {"Many lines of text the user can edit -- notes, a message, a document.",
                      "Put it inside a ScrolledWindow, or it grows with its text."}},
        {"ProgressBar", {"Shows how far along a long job is -- a fraction from 0 to 1, or a pulse back and forth "
                         "when you can't tell.", ""}},
        {"Separator", {"A thin line between groups of widgets, across or down.", ""}},
        {"Image", {"Shows a picture or an icon -- from the icon theme, the app's own icons, or a file.",
                   "Symbolic icons (-symbolic) recolour with the theme."}},
        {"Calendar", {"A month of days to pick a date from, with arrows for the month and year.", ""}},
        {"ColorDialogButton", {"A button wearing the chosen colour; clicking it opens GTK's colour chooser.",
                               ""}},
        {"FontDialogButton", {"A button showing the chosen font; clicking it opens GTK's font chooser.", ""}},
    };
    const auto it = kAbout.find(type);
    return it == kAbout.end() ? WidgetAbout{} : it->second;
}

std::string widget_doc_url(const std::string& type) {
    const WidgetSpec* w = find_widget(type);
    if (!w) return {};
    std::string c = w->gtk_class;   // "Gtk::Button" (a Dialog is a Gtk::Window)
    if (c.rfind("Gtk::", 0) == 0) c = c.substr(5);
    return "https://docs.gtk.org/gtk4/class." + c + ".html";
}

}  // namespace mortimer::core
