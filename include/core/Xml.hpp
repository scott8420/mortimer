#pragma once
// core::Xml -- just enough XML to read a GtkBuilder .ui (s024 m4).
//
// Elements, attributes, text, comments, <?...?>, CDATA and the five named
// entities plus &#NN; / &#xNN;. No DTDs, no namespaces -- a .ui has neither.
// Small on purpose: the core stays GTK-free and dependency-free (the seam), and
// what a .ui needs is a few dozen lines, not libxml2.
#include <map>
#include <string>
#include <vector>

namespace mortimer::core {

struct XmlNode {
    std::string name;                          // "object"
    std::map<std::string, std::string> attrs;  // class="GtkButton" id="go"
    std::vector<XmlNode> children;             // elements only, in order
    std::string text;                          // this element's own text, joined (entities decoded)
    int line = 0;                              // where it starts, for messages

    const std::string& attr(const std::string& k) const;   // "" when absent
};

// The document element. Throws std::runtime_error("line N: ...") on bad XML.
XmlNode parse_xml(const std::string& text);

}  // namespace mortimer::core
