#include "core/DiskForm.hpp"
#include "core/Form.hpp"

#include <exception>
#include <filesystem>
#include <system_error>

namespace mortimer::core {

DiskLook look_at_disk(const std::string& path, const std::string& known, bool dirty) {
    DiskLook l;
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
        l.what = DiskChange::Gone;
        return l;
    }
    try {
        // Compared as JSON the designer would write, not as bytes: a file
        // re-indented by hand, or with its keys reordered, is the same form.
        l.json = form_to_json(load_form(path));
    } catch (const std::exception& e) {
        l.what = DiskChange::Broken;
        l.error = e.what();
        return l;
    }
    if (l.json == known) l.what = DiskChange::Same;
    else l.what = dirty ? DiskChange::Ask : DiskChange::Reload;
    return l;
}

}  // namespace mortimer::core
