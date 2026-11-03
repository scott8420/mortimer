#pragma once
// designer::WidgetMenu -- the right-click menu of a widget, on the canvas and
// in the widget tree (s025 m5; the project list got its menu in m1, the
// palette its card in m4).
//
// The rows are the window's own edit actions (win.cut ... win.move-down) --
// the same verbs as the main menu and the keys, so the menu can't drift from
// them, each showing its key ("accel" drawn, not bound: the window hears the
// keys, Shell_bindings.cpp). Then "What is a <Type>?" -> the type's card
// (WidgetCard), as Alt+click in the palette shows it.
//
// Right-clicking a widget selects it first (unless it's already one of
// several selected -- then the menu acts on all of them), so the highlight
// says what the verbs will take.
#include <giomm/menu.h>

#include <string>

namespace mortimer::designer {

// The model; `card_action` is the pane's own ("canvas.card" / "tree.card").
Glib::RefPtr<Gio::Menu> widget_menu(const std::string& type, const std::string& card_action);

}  // namespace mortimer::designer
