cmake_minimum_required(VERSION 3.21)
project(mortimer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
find_package(spdlog REQUIRED)

# ── the new-project template, compiled in (cmake/embed.cmake) ──────────────
# templates/project/** -> one .cpp of raw strings, so mortimer-gen carries its
# template with it. Re-embedded whenever a template file changes.
file(GLOB_RECURSE MORTIMER_TEMPLATE_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/templates/project/*")
set(MORTIMER_TEMPLATES_CPP "${CMAKE_CURRENT_BINARY_DIR}/mortimer-templates.cpp")
add_custom_command(
    OUTPUT  "${MORTIMER_TEMPLATES_CPP}"
    COMMAND "${CMAKE_COMMAND}" -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/templates/project
            -DOUT=${MORTIMER_TEMPLATES_CPP} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${MORTIMER_TEMPLATE_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake"
    COMMENT "Embedding the new-project template"
    VERBATIM)

# ── mortimer_core -- the GTK-free core (the model side of the one real seam) ──────
# No gtkmm on this library's link line, BY DESIGN: a stray UI include under
# core/ fails to compile, so the seam is enforced by the build graph and the
# core stays headless-testable. (docs/two-layer-seam.md.)
add_library(mortimer_core STATIC
    src/core/Recents.cpp
    src/core/TextMap.cpp
    # The SHORTCUT-REGISTRY paradigm: one list, two consumers (App wires accels
    # from it, ShortcutsDialog renders from it) so a key and its advertised row
    # can't drift. GTK-free, so it lives in core and the selftest guards it
    # headless -- a pure test proves no two chords collide.
    src/core/Shortcuts.cpp
    # The UNDO paradigm (s046): core/Undo.hpp is the journal (header-only, a
    # template over the app's store); Stones is the demo model + its verbs +
    # the verb table the selftest gate walks. docs/undo.md.
    src/core/Stones.cpp
    # ── s001: the generator (Mortimer's reason to exist) ─────────────────────
    # Catalog = what can be placed; Form = a window's design (+ JSON, validate);
    # Codegen = the Qt split (generated <Form>Ui base + your <Form> once);
    # HookScan = hooked / unhooked / missing / stale; Project = the app Mortimer
    # owns (mortimer.json, generate-all, a new workspace from the template).
    src/core/Catalog.cpp
    src/core/Form.cpp
    src/core/Codegen.cpp
    src/core/HookScan.cpp
    src/core/Project.cpp
    src/core/Resources.cpp   # s009: the bundle + forms, as the navigator lists them
    # ── s002: the design the window edits -- a Form flattened by stable key so
    # the journal holds it; every designer verb + the verb table the gate walks.
    src/core/Design.cpp
    ${MORTIMER_TEMPLATES_CPP}
)
target_include_directories(mortimer_core PUBLIC include)
# Vendored nlohmann/json (header-only, MIT), PRIVATE -- an implementation detail
# of the pump, no consumer inherits the include (CANON: surprise-free build).
target_include_directories(mortimer_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ── mortimer-gen -- the generator from a terminal (GTK-free, like the core) ───
add_executable(mortimer-gen src/gen_main.cpp)
target_link_libraries(mortimer-gen PRIVATE mortimer_core)

# ── Headless selftest -- exercises the core with no display ────────────────────
add_executable(mortimer_selftest src/selftest.cpp src/selftest_generator.cpp src/selftest_design.cpp)
# The gate reads tests/KitchenSink.mort from the source tree.
target_compile_definitions(mortimer_selftest PRIVATE MORTIMER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mortimer_selftest PRIVATE mortimer_core)
# The selftest reads json.hpp too (manifests), and finds the source tree for the proof.
target_include_directories(mortimer_selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ── App resources (gresource) -- the RESOURCE-PIPELINE paradigm ────────────────
# glib-compile-resources embeds resources/mortimer.gresource.xml into a C source;
# App::on_activate registers it and points the icon theme at the bundle so
# set_icon_name("mortimer-logo-symbolic") resolves out of the binary. The generated
# .c compiles as C (project enables C above) so mortimer_resources_get_resource()
# has C linkage, matching the extern "C" prototype in App.cpp. Adding an icon:
# drop the .svg in resources/icons/scalable/apps/, list it in the .xml, rebuild.
# (docs/resources-and-dialog-lifetime.md)
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(CAIRN_GRESOURCE_XML "${CMAKE_CURRENT_SOURCE_DIR}/resources/mortimer.gresource.xml")
set(CAIRN_RESOURCES_C   "${CMAKE_CURRENT_BINARY_DIR}/mortimer-resources.c")
file(GLOB CAIRN_RESOURCE_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps/*.svg"
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.css")
add_custom_command(
    OUTPUT  "${CAIRN_RESOURCES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            --target "${CAIRN_RESOURCES_C}"
            --generate-source
            --c-name mortimer_resources
            mortimer.gresource.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DEPENDS "${CAIRN_GRESOURCE_XML}" ${CAIRN_RESOURCE_FILES}
    COMMENT "Compiling gresource bundle (mortimer-resources.c)"
    VERBATIM)

# ── mortimer -- the GTK app ───────────────────────────────────────────────────────
# The Shell is a SPLIT CLASS across TUs by category (see the index in
# include/Shell.hpp); all parts compile into the one mortimer::Shell.
add_executable(mortimer
    ${CAIRN_RESOURCES_C}
    src/main.cpp
    src/App.cpp
    # Follows the desktop's light/dark preference (GTK4 will not do it for us
    # and we do not link libadwaita). Needs no new dependency: the D-Bus and
    # GSettings calls are GIO, which gtkmm-4.0 already brings in.
    src/Appearance.cpp
    src/Shell.cpp
    src/Shell_zones.cpp
    src/Shell_bindings.cpp
    src/Shell_handlers.cpp
    src/Shell_helpers.cpp
    src/AboutWindow.cpp
    src/ShortcutsDialog.cpp
    # s002 -- the designer's panes: the live canvas (real widgets, hooks inert),
    # palette + tree, the inspector, and the catalog<->GObject property bridge.
    src/designer/Canvas.cpp
    src/designer/Panes.cpp
    src/designer/Inspector.cpp
    src/designer/GProps.cpp
    src/designer/HooksPane.cpp
    src/designer/Dialogs.cpp
    src/designer/ProjectPane.cpp   # s009: the navigator's forms + resources, + and trash
    src/Registry.cpp
    src/Log.cpp
)
target_include_directories(mortimer PRIVATE include ${GTKMM_INCLUDE_DIRS})
target_link_libraries(mortimer     PRIVATE ${GTKMM_LIBRARIES} mortimer_core spdlog::spdlog)
target_link_directories(mortimer   PRIVATE ${GTKMM_LIBRARY_DIRS})
target_compile_options(mortimer    PRIVATE ${GTKMM_CFLAGS_OTHER})
