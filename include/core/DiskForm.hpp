#pragma once
// core::DiskForm -- the open form vs its file on disk (s025 m3).
//
// s024 m2 made hand edits a first-class path: the app's build regenerates
// from forms/*.mort, so a form edited in a text editor or changed by a git
// pull is real work. Before m3 the designer kept its own copy; a Save wrote
// over the newer file without a word.
//
// The designer remembers the form's JSON as it last READ or WROTE the file
// (`known`). When the file changes -- or before a Save writes it -- it looks:
//
//   Same    the file says what we know (our own Save, a touch, a project
//           verb that already reloaded us)                   -> nothing
//   Reload  it changed; no unsaved edits here                -> reload it
//   Ask     it changed AND there are unsaved edits here      -> ask: reload
//           (lose mine) or keep mine (the next Save overwrites it)
//   Gone    deleted or moved away                            -> say so; Save
//           writes it back
//   Broken  it doesn't parse (an editor mid-save, a merge conflict) -> say
//           so, keep ours; a Save asks first
//
// GTK-free, so the selftest walks every case headless; the window does the
// watching (a Gio::FileMonitor) and the asking.
#include <string>

namespace mortimer::core {

enum class DiskChange { Same, Reload, Ask, Gone, Broken };

struct DiskLook {
    DiskChange  what = DiskChange::Same;
    std::string json;    // the file's form, as form_to_json says it (Reload / Ask / Same)
    std::string error;   // Broken: why it didn't parse
};

// known: the form's JSON as last read from / written to `path`.
// dirty: the designer has edits not yet saved.
DiskLook look_at_disk(const std::string& path, const std::string& known, bool dirty);

}  // namespace mortimer::core
