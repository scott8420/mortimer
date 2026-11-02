#pragma once
// core::CssScope -- s012: the app's stylesheet, made to style ONLY the canvas.
//
// GTK 4 has no per-subtree stylesheet: a CSS provider is display-wide. Load the
// app's resources/<bin>.css into the designer as it is, and `label { color:
// red }` turns every label in Mortimer red too. So the canvas loads a REWRITTEN
// copy, every rule scoped to the form's card (".mortimer-window"):
//
//   label, .title  { ... }    ->  .mortimer-window label, .mortimer-window .title { ... }
//   window.big > box { ... }  ->  .mortimer-window.big > box { ... }   (the card IS the window)
//   @define-color, @import, @keyframes { ... }   -> unchanged
//
// Only selectors are touched; declarations, comments and newlines are copied
// as they are, so a parse error GTK reports in the copy is on the SAME line of
// your file. GTK-FREE: text in, text out -- the selftest proves it headless.
#include <string>
#include <vector>

namespace mortimer::core {

// `scope` is a class selector (".mortimer-window"). A rule's leading `window`
// type selector becomes the scope itself; any other selector gets it as an
// ancestor. Malformed input is copied as far as it goes (GTK reports it).
std::string scope_css(const std::string& css, const std::string& scope);

// s012 m3 -- the class names a stylesheet's SELECTORS use (`.card`, `button.big`
// -> card, big), sorted, once each: what the inspector offers for css_classes.
// Declarations, comments, strings and @-rules aren't looked at, so `0.5em` or a
// `.png` in a url() never count.
std::vector<std::string> css_class_names(const std::string& css);

}  // namespace mortimer::core
