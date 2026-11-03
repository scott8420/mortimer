#pragma once
// core::ProjectUndo -- the PROJECT's verbs as undo steps (s025 m2; Scott: "is
// there undo/redo throughout the app?" -- inside a form yes, the project's
// verbs no).
//
// ── the same journal, a different store ───────────────────────────────────
// A form's undo is core::Journal over the design: a step is how the records
// it touched looked before and after. The project's verbs -- add / remove /
// rename a form or a resource, share / unshare / edit a shared model, import a
// .ui -- change FILES. So the files are the store:
//
//     Id    = a path, relative to the app folder   ("forms/Prefs.mort")
//     Value = the file's bytes                      (absent = no such file)
//
// and a step is the files the verb changed, before and after. Removing a form
// MOVES forms/X.mort to removed/X/X.mort: two entries (one gone, one made).
// Undo puts each one back as it was. No verb needs an inverse written for it:
// a verb added later is undoable the day it runs through project_run().
//
// Which files a verb will touch isn't known ahead, so project_run() names
// them ALL (every file in the app folder, build/ and dot-folders aside) and
// the journal keeps only the ones that changed (Journal::prune_unchanged).
// A project is a few hundred small files; a verb is a click, not a loop.
//
// ── never overwrite your work ─────────────────────────────────────────────
// Between the verb and the Ctrl+Z, a file can change under us -- you edit
// src/Prefs.cpp, git pulls. Putting the step's picture back would lose that.
// project_conflict() names the first file that isn't as the step left it;
// the window refuses the undo and says which. (generated/ is exempt: it's
// Mortimer's own output, made again from the forms on every build.)
#include "core/Undo.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mortimer::core {

// The store contract (core/Undo.hpp): get / put by id.
struct ProjectFiles {
    using Id    = std::string;   // relative to root, '/'-separated
    using Value = std::string;   // the bytes

    std::string root;            // the app folder ("" = no project: nothing to read or write)

    std::optional<Value> get(const Id& rel) const;
    // Writes the file (making its folders), or removes it -- and then any
    // folders it leaves empty, up to the app's own top-level ones.
    void put(const Id& rel, const std::optional<Value>& v);
};

using ProjectJournal = Journal<ProjectFiles>;

// Every file under root a step watches: sorted, relative. Skipped: build* and
// dot-named folders at any depth (the build, .git, an editor's cache).
std::vector<std::string> project_files(const std::string& root);

// THE ONE DOOR for a project verb: `op` runs as one step labelled `label`.
// An exception from op still keeps what it changed (undoable), then goes on
// up to the caller. True if a step was kept.
bool project_run(ProjectJournal& j, const std::string& label, const std::function<void()>& op);

// The first file (not under generated/) whose bytes on disk aren't what the
// step left -- "" when undo (or redo) can go ahead.
std::string project_conflict(const ProjectJournal& j, bool undo);

// The files the next undo / redo puts back.
std::vector<std::string> project_touched(const ProjectJournal& j, bool undo);

}  // namespace mortimer::core
