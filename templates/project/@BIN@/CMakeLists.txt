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
# Add an icon: drop the .svg in resources/icons/scalable/apps/, list it in the
# .xml, rebuild.
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
set(RES_XML "${CMAKE_CURRENT_SOURCE_DIR}/resources/@BIN@.gresource.xml")
set(RES_C   "${CMAKE_CURRENT_BINARY_DIR}/@BIN@-resources.c")
file(GLOB RES_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/scalable/apps/*.svg"
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.css")
add_custom_command(
    OUTPUT  "${RES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}" --target "${RES_C}" --generate-source
            --c-name @BIN@_resources @BIN@.gresource.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    DEPENDS "${RES_XML}" ${RES_FILES}
    COMMENT "Compiling gresource bundle"
    VERBATIM)

# ── @BIN@ -- the app ─────────────────────────────────────────────────────────
file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/selftest.cpp")
file(GLOB GENERATED_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.cpp")
add_executable(@BIN@ ${RES_C} ${APP_SOURCES} ${GENERATED_SOURCES})
# include/ for your headers; the app root so "generated/X.hpp" resolves.
target_include_directories(@BIN@ PRIVATE include "${CMAKE_CURRENT_SOURCE_DIR}" ${GTKMM_INCLUDE_DIRS})
target_link_libraries(@BIN@ PRIVATE ${GTKMM_LIBRARIES} @BIN@_core spdlog::spdlog)
target_link_directories(@BIN@ PRIVATE ${GTKMM_LIBRARY_DIRS})
target_compile_options(@BIN@ PRIVATE ${GTKMM_CFLAGS_OTHER})
