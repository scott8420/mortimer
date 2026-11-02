#pragma once
// core::Templates -- the new-project template, compiled into the binary.
//
// templates/project/ in the source tree is the workspace a new app gets, with
// @TOKEN@s for its names (@APP@, @BIN@, @BIN_UPPER@, @APP_ID@, @APP_ID_PATH@,
// @NS@; @RULES@ in a HANDOFF pulls in the rendered RULES.md). cmake/embed.cmake
// turns the folder into a generated .cpp at build time, so mortimer-gen carries
// its template with it: no install path to find, nothing to go missing.
// (@BINDIR@ is NOT ours -- install.sh fills it in the .desktop file.)
#include <vector>

namespace mortimer::core {

struct TemplateFile {
    const char* path;      // relative to the workspace root; may hold tokens
    const char* content;
};

const std::vector<TemplateFile>& project_templates();

}  // namespace mortimer::core
