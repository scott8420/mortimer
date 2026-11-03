#include "core/ProjectUndo.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace mortimer::core {

namespace fs = std::filesystem;

namespace {

// A folder a step never walks into: the build (build/, build-debug/...), and
// anything dot-named (.git, .cache, an editor's swap folder).
bool skipped_dir(const fs::path& name) {
    const std::string n = name.string();
    return n.empty() || n[0] == '.' || n.rfind("build", 0) == 0;
}

// The app's own folders stay even when a removal empties them -- the template
// made them, CMake globs them.
bool kept_dir(const std::string& top) {
    static const std::set<std::string> keep = {"forms", "src", "include", "resources", "generated", "data"};
    return keep.count(top) > 0;
}

}  // namespace

std::optional<ProjectFiles::Value> ProjectFiles::get(const Id& rel) const {
    if (root.empty()) return std::nullopt;
    const fs::path p = fs::path(root) / rel;
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return std::nullopt;
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

void ProjectFiles::put(const Id& rel, const std::optional<Value>& v) {
    if (root.empty()) return;
    const fs::path p = fs::path(root) / rel;
    std::error_code ec;
    if (v) {
        fs::create_directories(p.parent_path(), ec);
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out << *v;
        return;
    }
    fs::remove(p, ec);
    // Folders the file leaves empty go too (removed/Prefs/ after an undone
    // remove) -- but never the app folder or its own top-level folders.
    const fs::path top = fs::path(rel).begin() != fs::path(rel).end() ? *fs::path(rel).begin() : fs::path{};
    for (fs::path d = p.parent_path(); d != fs::path(root) && d.has_relative_path(); d = d.parent_path()) {
        const fs::path drel = d.lexically_relative(root);
        if (drel.empty() || drel == "." || (drel == top && kept_dir(top.string()))) break;
        if (!fs::is_empty(d, ec) || ec) break;
        fs::remove(d, ec);
    }
}

std::vector<std::string> project_files(const std::string& root) {
    std::vector<std::string> out;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec)) {
            if (skipped_dir(it->path().filename())) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec)) out.push_back(it->path().lexically_relative(root).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool project_run(ProjectJournal& j, const std::string& label, const std::function<void()>& op) {
    // The journal counts its depth around op. An exception
    // thrown THROUGH run() would leave it counting -- every later verb would
    // then write without a step. So op's exception is caught inside and thrown
    // again after the step is kept.
    std::exception_ptr failed;
    const std::vector<std::string> before = project_files(j.store().root);
    const bool kept = j.run(label, before, [&]() -> std::vector<std::string> {
        try { op(); } catch (...) { failed = std::current_exception(); }
        std::vector<std::string> made;
        const std::vector<std::string> after = project_files(j.store().root);
        std::set_difference(after.begin(), after.end(), before.begin(), before.end(), std::back_inserter(made));
        return made;
    });
    if (failed) std::rethrow_exception(failed);
    return kept;
}

std::string project_conflict(const ProjectJournal& j, bool undo) {
    const auto* st = undo ? j.undo_step() : j.redo_step();
    if (!st) return {};
    for (const auto& e : st->entries) {
        if (e.id.rfind("generated/", 0) == 0) continue;
        const auto& expect = undo ? e.after : e.before;   // what the disk should say now
        if (!(j.store().get(e.id) == expect)) return e.id;
    }
    return {};
}

std::vector<std::string> project_touched(const ProjectJournal& j, bool undo) {
    std::vector<std::string> out;
    if (const auto* st = undo ? j.undo_step() : j.redo_step())
        for (const auto& e : st->entries) out.push_back(e.id);
    return out;
}

}  // namespace mortimer::core
