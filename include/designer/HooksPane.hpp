#pragma once
// designer::HooksPane -- the hooks report as a panel under the canvas, LIVE.
//
// mortimer-gen writes generated/<Form>.hooks.txt; this is the same report
// (core::live_report) computed from the form as it is in the window right now
// against your include/<Form>.hpp -- so ticking a signal shows "unhooked" at
// once, ticking "required" shows "MISSING", and a hook your header overrides
// but the form dropped shows "STALE", before you save. Copy puts the lines to
// paste (declarations for the header, empty bodies for the .cpp) on the
// clipboard. A row click selects the widget the hook belongs to.
#include "core/HookScan.hpp"
#include "widgets/Widgets.hpp"

#include <string>
#include <vector>

namespace mortimer::designer {

class HooksPane : public widgets::Box {
public:
    HooksPane();
    // app_dir "" = the form isn't in a project (nothing can be hooked yet).
    void show(const core::HookReport& r, const std::string& app_dir);
    sigc::signal<void(const std::string&)>& signal_select() { return m_select; }   // a widget NAME

private:
    widgets::Label          m_summary;
    widgets::Button         m_copy;
    widgets::Label          m_note;
    widgets::ScrolledWindow m_scroll;
    widgets::ListBox        m_list;
    std::vector<std::string> m_row_widgets;
    std::string             m_paste;
    sigc::signal<void(const std::string&)> m_select;
};

}  // namespace mortimer::designer
