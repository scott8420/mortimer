cmake_minimum_required(VERSION 3.21)
project(@BIN@ VERSION 0.1.0 LANGUAGES C CXX)

# @APP@ -- made by Mortimer. Two halves of every form build into one binary:
#   generated/*.cpp   Mortimer's (rewritten from forms/*.mort -- never edit)
#   src/*.cpp         yours (Mortimer wrote the first stubs, then let go)
# Both are GLOBbed with CONFIGURE_DEPENDS, so a new form needs no edit here.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Faster rebuilds when they're installed -- found, never required.
find_program(CCACHE ccache)
if(CCACHE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE}")
endif()
include(CheckLinkerFlag)
check_linker_flag(CXX "-fuse-ld=mold" HAVE_MOLD)
if(HAVE_MOLD)
    add_link_options("-fuse-ld=mold")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED gtkmm-4.0)
find_package(spdlog REQUIRED)

# ── forms/ -> generated/, by the build ───────────────────────────────────────
# Change a forms/*.mort or mortimer.json ANY way -- Mortimer, an editor, a git
# pull -- and the next build regenerates before it compiles, so it can't build
# a stale form. How: CMake re-runs itself when one of them is newer than its
# last run (CMAKE_CONFIGURE_DEPENDS), and that run calls mortimer-gen generate
# HERE, before the globs below -- so a new form's files are in this same build.
# generate writes only what changed; a form with errors stops the build, and
# the last good generated/ stays. Without mortimer-gen (someone building your
# published app) generated/ is used as it is -- that's why it's checked in.
# Off: cmake -B build -D@BIN_UPPER@_REGENERATE=OFF. Elsewhere: -DMORTIMER_GEN=/path.
option(@BIN_UPPER@_REGENERATE "Regenerate generated/ from forms/ when they change" ON)
find_program(MORTIMER_GEN mortimer-gen HINTS "$ENV{HOME}/.local/bin")
file(GLOB FORM_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/forms/*.mort")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FORM_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/mortimer.json")
if(@BIN_UPPER@_REGENERATE AND MORTIMER_GEN)
    execute_process(COMMAND "${MORTIMER_GEN}" generate "${CMAKE_CURRENT_SOURCE_DIR}"
                    RESULT_VARIABLE GEN_RESULT OUTPUT_VARIABLE GEN_OUT ERROR_VARIABLE GEN_OUT)
    if(NOT GEN_RESULT EQUAL 0)
        message(FATAL_ERROR "mortimer-gen generate: the forms have errors (the last good generated/ is kept):\n${GEN_OUT}")
    endif()
    string(REGEX MATCHALL "  (wrote|your half|removed|note|warn) [^\n]*" GEN_SAID "${GEN_OUT}")
    if(GEN_SAID)
        string(REPLACE ";" "\n" GEN_SAID "${GEN_SAID}")
        message(STATUS "forms regenerated (mortimer-gen):\n${GEN_SAID}")
    else()
        message(STATUS "forms: generated/ is up to date")
    endif()
elseif(@BIN_UPPER@_REGENERATE)
    message(STATUS "mortimer-gen not found: generated/ is used as it is (forms/ edits need a generate)")
endif()

# ── @BIN@_core -- the GTK-free half (docs/two-layer-seam.md) ─────────────────
# No gtkmm on this library's link line, BY DESIGN: a stray UI include under
# core/ fails to compile, so the seam is enforced by the build graph.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")
add_library(@BIN@_core STATIC ${CORE_SOURCES})
target_include_directories(@BIN@_core PUBLIC include)
target_compile_definitions(@BIN@_core PRIVATE APP_VERSION="${PROJECT_VERSION}")

add_executable(@BIN@_selftest src/selftest.cpp)
target_link_libraries(@BIN@_selftest PRIVATE @BIN@_core)

# ── resources, compiled in (docs/resources-and-dialog-lifetime.md) ──────────
# Add a resource from Mortimer's navigator (+ ▸ Icon / Image / Data file /
# Stylesheet) or `mortimer-gen add`: it copies the file and lists it in the .xml.
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(RES_XML "${CMAKE_CURRENT_SOURCE_DIR}/resources/@BIN@.gresource.xml")
set(RES_C   "${CMAKE_CURRENT_BINARY_DIR}/@BIN@-resources.c")
file(GLOB RES_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps/*.svg"
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.css"
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/*"
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/data/*")
add_custom_command(
    OUTPUT  "${RES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}" --target "${RES_C}" --generate-source
            --c-name @BIN@_resources @BIN@.gresource.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DEPENDS "${RES_XML}" ${RES_FILES}
    COMMENT "Compiling gresource bundle"
    VERBATIM)

# ── @BIN@ -- the app ─────────────────────────────────────────────────────────
# src/widgets/: your promoted widgets' classes (Mortimer writes each one once).
file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
                                        "${CMAKE_CURRENT_SOURCE_DIR}/src/widgets/*.cpp")
list(REMOVE_ITEM APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/selftest.cpp")
file(GLOB GENERATED_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.cpp")
add_executable(@BIN@ ${RES_C} ${APP_SOURCES} ${GENERATED_SOURCES})
# include/ for your headers; the app root so "generated/X.hpp" resolves.
target_include_directories(@BIN@ PRIVATE include "${CMAKE_CURRENT_SOURCE_DIR}" ${GTKMM_INCLUDE_DIRS})
target_link_libraries(@BIN@ PRIVATE ${GTKMM_LIBRARIES} @BIN@_core spdlog::spdlog)
target_link_directories(@BIN@ PRIVATE ${GTKMM_LIBRARY_DIRS})
target_compile_options(@BIN@ PRIVATE ${GTKMM_CFLAGS_OTHER})

# ── @BIN@_undo_gate -- the app's undo, checked on EVERY build ─────────────────
# generated/UndoGate.cpp walks each row type (your forms' models, the shared ones)
# through the undo journal: add, each field alone, remove; undo, redo. It needs
# no display, and the build RUNS it: a broken undo fails here, not for your user.
# (The app's own glob takes UndoGate.cpp too; without MORTIMER_UNDO_GATE it's empty.)
file(GLOB GATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generated/*Items.cpp")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/Models.cpp")
    list(APPEND GATE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/generated/Models.cpp")
endif()
add_executable(@BIN@_undo_gate generated/UndoGate.cpp generated/Undo.cpp src/Log.cpp ${GATE_SOURCES})
target_compile_definitions(@BIN@_undo_gate PRIVATE MORTIMER_UNDO_GATE)
target_include_directories(@BIN@_undo_gate PRIVATE include "${CMAKE_CURRENT_SOURCE_DIR}" ${GTKMM_INCLUDE_DIRS})
target_link_libraries(@BIN@_undo_gate PRIVATE ${GTKMM_LIBRARIES} @BIN@_core spdlog::spdlog)
target_link_directories(@BIN@_undo_gate PRIVATE ${GTKMM_LIBRARY_DIRS})
target_compile_options(@BIN@_undo_gate PRIVATE ${GTKMM_CFLAGS_OTHER})
# Run it: the stamp is written only when it passes, so a failed gate runs again
# on the next build (and keeps the build red) until the undo is fixed.
add_custom_command(
    OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/undo_gate.passed"
    COMMAND @BIN@_undo_gate
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_BINARY_DIR}/undo_gate.passed"
    DEPENDS @BIN@_undo_gate
    COMMENT "Undo gate: walking the app's undo"
    VERBATIM)
add_custom_target(undo_gate ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/undo_gate.passed")
