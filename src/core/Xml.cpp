#include "core/Xml.hpp"

#include <cctype>
#include <stdexcept>

namespace mortimer::core {

const std::string& XmlNode::attr(const std::string& k) const {
    static const std::string none;
    const auto it = attrs.find(k);
    return it == attrs.end() ? none : it->second;
}

namespace {

struct Reader {
    const std::string& s;
    std::size_t at = 0;
    int line = 1;

    [[noreturn]] void fail(const std::string& what) const {
        throw std::runtime_error("line " + std::to_string(line) + ": " + what);
    }
    bool starts(const char* t) const { return s.compare(at, std::char_traits<char>::length(t), t) == 0; }
    void step(std::size_t n = 1) {
        for (std::size_t i = 0; i < n && at < s.size(); ++i, ++at)
            if (s[at] == '\n') ++line;
    }
    void skip_space() { while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) step(); }
    void skip_past(const char* end, const char* what) {
        const auto e = s.find(end, at);
        if (e == std::string::npos) fail(std::string("an unclosed ") + what);
        step(e + std::char_traits<char>::length(end) - at);
    }
    std::string name() {
        const std::size_t b = at;
        while (at < s.size() && (std::isalnum(static_cast<unsigned char>(s[at])) || s[at] == '_' || s[at] == '-' ||
                                 s[at] == ':' || s[at] == '.'))
            step();
        if (b == at) fail("a name was expected");
        return s.substr(b, at - b);
    }

    // &amp; &#233; &#xE9; -> the character (UTF-8).
    std::string decode(const std::string& raw) const {
        std::string o;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '&') { o += raw[i]; continue; }
            const auto semi = raw.find(';', i);
            if (semi == std::string::npos) { o += raw[i]; continue; }
            const std::string e = raw.substr(i + 1, semi - i - 1);
            unsigned long cp = 0;
            bool num = false;
            if (e == "amp") o += '&';
            else if (e == "lt") o += '<';
            else if (e == "gt") o += '>';
            else if (e == "quot") o += '"';
            else if (e == "apos") o += '\'';
            else if (!e.empty() && e[0] == '#') {
                num = true;
                try { cp = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::stoul(e.substr(2), nullptr, 16) : std::stoul(e.substr(1)); }
                catch (...) { o += raw.substr(i, semi - i + 1); i = semi; continue; }
            } else { o += raw.substr(i, semi - i + 1); i = semi; continue; }
            if (num) {
                if (cp < 0x80) o += static_cast<char>(cp);
                else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
                else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
            }
            i = semi;
        }
        return o;
    }

    // Comments, <?...?>, <!DOCTYPE ...> -- skipped wherever they are.
    bool skip_misc() {
        if (starts("<!--")) { skip_past("-->", "comment"); return true; }
        if (starts("<?")) { skip_past("?>", "<?...?>"); return true; }
        if (starts("<!DOCTYPE")) { skip_past(">", "DOCTYPE"); return true; }
        return false;
    }

    XmlNode element() {
        XmlNode n;
        n.line = line;
        step();   // '<'
        n.name = name();
        for (;;) {
            skip_space();
            if (at >= s.size()) fail("<" + n.name + "> isn't closed");
            if (starts("/>")) { step(2); return n; }
            if (s[at] == '>') { step(); break; }
            const std::string k = name();
            skip_space();
            if (at >= s.size() || s[at] != '=') fail("attribute " + k + " has no value");
            step();
            skip_space();
            if (at >= s.size() || (s[at] != '"' && s[at] != '\'')) fail("attribute " + k + "'s value isn't quoted");
            const char q = s[at];
            step();
            const auto e = s.find(q, at);
            if (e == std::string::npos) fail("attribute " + k + "'s value isn't closed");
            n.attrs[k] = decode(s.substr(at, e - at));
            step(e + 1 - at);
        }
        for (;;) {
            if (at >= s.size()) fail("<" + n.name + "> (line " + std::to_string(n.line) + ") isn't closed");
            if (skip_misc()) continue;
            if (starts("<![CDATA[")) {
                step(9);
                const auto e = s.find("]]>", at);
                if (e == std::string::npos) fail("an unclosed CDATA");
                n.text += s.substr(at, e - at);
                step(e + 3 - at);
                continue;
            }
            if (starts("</")) {
                step(2);
                const std::string end = name();
                if (end != n.name) fail("</" + end + "> closes <" + n.name + "> (line " + std::to_string(n.line) + ")");
                skip_space();
                if (at >= s.size() || s[at] != '>') fail("</" + end + " isn't closed");
                step();
                return n;
            }
            if (s[at] == '<') { n.children.push_back(element()); continue; }
            const auto e = s.find('<', at);
            const std::size_t stop = e == std::string::npos ? s.size() : e;
            n.text += decode(s.substr(at, stop - at));
            step(stop - at);
        }
    }
};

}  // namespace

XmlNode parse_xml(const std::string& text) {
    Reader r{text};
    for (;;) {
        r.skip_space();
        if (r.at >= text.size()) r.fail("no element in it");
        if (r.skip_misc()) continue;
        if (text[r.at] != '<') r.fail("text before the first element");
        return r.element();
    }
}

}  // namespace mortimer::core
