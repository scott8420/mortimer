#pragma once
// core::Help -- the user guide, compiled in (s024 m6).
//
// ONE source: the repo's help/*.md pages (public -- the product's help, unlike
// the workspace's private docs/). cmake/embed_help.cmake compiles them into the
// core, so the designer's Help window and `mortimer-gen help` show the same
// pages, and GitHub shows them too. GTK-free: the pages are parsed here into
// blocks and spans; the window only paints them.
//
// The Markdown is the plain subset the pages use: # / ## / ### headings,
// paragraphs, "- " bullets, ``` code blocks, | tables |, **bold**, *italic*, `code`.
#include <string>
#include <vector>

namespace mortimer::core {

struct HelpPage {
    const char* file;   // "02-the-designer.md"
    const char* text;
};
const std::vector<HelpPage>& help_pages();   // in file order

std::string help_title(const HelpPage& p);   // its "# Title" line, else the file

// A page as blocks to paint.
struct HelpBlock {
    enum class Kind { H1, H2, H3, Para, Bullet, Number, Code, Table };
    Kind kind;
    std::string text;                        // Para / headings / list items: inline marks kept
    std::vector<std::vector<std::string>> rows;   // Table: cells (no separator row)
};
std::vector<HelpBlock> help_blocks(const std::string& text);

// Inline marks of a line: **bold**, *italic*, `code`; everything else plain.
struct HelpSpan {
    std::string text;
    bool bold = false;
    bool code = false;
    bool italic = false;
};
std::vector<HelpSpan> help_spans(const std::string& line);

// Pages that mention every word (case-insensitive), best first (most hits);
// `hits` counts them. A query naming a page's file or title picks that page.
struct HelpHit {
    std::size_t page;   // index in help_pages()
    int hits = 0;
};
std::vector<HelpHit> help_search(const std::string& query);

}  // namespace mortimer::core
