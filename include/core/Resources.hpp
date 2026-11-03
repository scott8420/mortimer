#pragma once
// core::Resources -- an app's bundled files, and its forms, as the navigator
// lists them (s009).
//
// The bundle is resources/<bin>.gresource.xml: compiled into the binary, so
// nothing lands on the user's disk. Each kind has its folder and its prefix:
//
//   icon   resources/icons/scalable/apps/x.svg  <id path>/icons/scalable/apps  -> icon_name "x"
//   image  resources/images/x.png               <id path>/images                -> a resource path
//   data   resources/data/x.json                <id path>/data                  -> a resource path
//   css    resources/<bin>.css                  <id path>                       -> loaded by the generated code
//
// Removing never deletes: the file moves to <app>/removed/ (outside every
// build glob), so the app still builds and nothing is lost. A removed form
// takes your include/<Form>.hpp and src/<Form>.cpp there too -- Mortimer
// never writes your half, and it doesn't throw it away either.
//
// GTK-FREE (the seam). Errors throw std::runtime_error with the reason in words.
#include <string>
#include <vector>

namespace mortimer::core {

struct Resource {
    std::string kind;   // icon | image | data | css
    std::string file;   // under resources/: "icons/scalable/apps/x-symbolic.svg"
    std::string path;   // in the bundle: "/io/github/scott8420/Notes/icons/scalable/apps/x-symbolic.svg"
    std::string use;    // what your code names: the icon name, or the resource path
};

// "io.github.scott8420.Notes" -> "/io/github/scott8420/Notes"
std::string id_path(const std::string& app_id);

// The bundle's files, in the xml's order. An app with no xml has none.
std::vector<Resource> list_resources(const std::string& app_dir);

// Copy `src` into resources/<kind's folder>/ and list it in the bundle.
// kind: icon (an .svg), image, data. Refused: an unknown kind, a name that
// isn't letters / digits / . _ -, a name already there, a missing file.
Resource add_resource(const std::string& app_dir, const std::string& kind, const std::string& src);

// resources/<bin>.css, made (with a comment saying how it's used) and listed.
// Refused if it's already listed.
Resource add_stylesheet(const std::string& app_dir);

// Drop `file` (as Resource::file) from the bundle; the file moves to removed/.
// Returns where it went. The app's logo icon is refused (the .desktop file and
// the About window use it).
std::string remove_resource(const std::string& app_dir, const std::string& file);

// Drop a form from the project: forms/<stem>.mort and your include/src halves
// move to removed/<stem>/; regenerate prunes its generated files. The main form
// is refused. Returns the folder they went to.
std::string remove_form(const std::string& app_dir, const std::string& stem);

// s021 -- rename a project's form `old` -> `now` (the class follows its file):
//   forms/<old>.mort -> forms/<now>.mort (its name inside too); the project's
//   list keeps its place, main_form follows; other forms' actions that open it
//   follow; generated/<old>.hooks.{json,txt} move (stale hooks stay tracked).
//   YOUR half: include/<old>.hpp + src/<old>.cpp become <now>.hpp / <now>.cpp
//   with the class renamed inside (code and comments; never a string); every
//   other file of yours under include/ and src/ has its #include "<old>.hpp"
//   and its code mentions renamed (comments and strings left alone).
//   The original of every file of yours that changed is kept in
//   removed/<old>.renamed/ -- nothing is lost.
// Refused, in words: not a class name, no such form, the name is taken (a form,
// or include/<now>.hpp / src/<now>.cpp already there). Doesn't generate.
struct RenamedForm {
    std::vector<std::string> edited;    // files of yours rewritten (relative, new names)
    std::vector<std::string> forms;     // other forms whose actions open it
    std::string kept;                   // "removed/<old>.renamed" ("" = nothing of yours changed)
};
RenamedForm rename_project_form(const std::string& app_dir, const std::string& old, const std::string& now);

// The rewrite behind it, alone (the selftest's): C++ text with identifier `old`
// -> `now` and `old`Ui -> `now`Ui; an #include of "<old>.hpp" / "<old>Ui.hpp"
// (any folder) follows. Strings and char literals never change; comments do
// only when `comments` (your form's own two files).
std::string rename_class_in(const std::string& text, const std::string& old, const std::string& now, bool comments);

// s021 m2 -- rename a bundled file: `file` (as Resource::file) -> `name` (a
// file name; no extension = the old one's). The file moves in its folder, the
// xml follows; an ICON's name is what forms say (icon_name, a Stack page's
// icon), so every form that names it follows too. Code names a resource in a
// STRING, and strings are yours: the files of yours (and the stylesheet) that
// still name the old one are listed, not changed. Refused, in words: the
// stylesheet (it's <bin>.css by name), the logo, a name that isn't letters /
// digits / . _ -, an icon that isn't .svg, a name already there.
struct RenamedResource {
    Resource now;                       // as the bundle lists it after
    std::string was;                    // what code named before (icon name / path)
    std::vector<std::string> forms;     // forms whose icon_name followed
    std::vector<std::string> mentions;  // your files still naming `was` in a string
};
RenamedResource rename_resource(const std::string& app_dir, const std::string& file, const std::string& name);

// The root type of a project form ("Window", "Dialog", ...) for the list; "" if unreadable.
std::string form_kind(const std::string& app_dir, const std::string& stem);

}  // namespace mortimer::core
