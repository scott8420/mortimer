#pragma once
// designer::gprops -- a catalog property <-> the GObject property behind it.
//
// Most catalog properties are the widget's GObject property under the same
// name (underscores for dashes): "use_markup" is "use-markup". A few have a
// different GObject name; gobject_name() knows them. The canvas SETS through
// this; the inspector reads the property's DEFAULT through it, so an unset
// row shows what GTK will actually do rather than a guess.
#include "core/Form.hpp"

#include <gtkmm/widget.h>
#include <optional>
#include <string>

namespace mortimer::designer::gprops {

// "" when the property has no single GObject property (margin, a range's
// bounds, items, css_classes, a Ref).
std::string gobject_name(const std::string& catalog_prop);

// Set it from the JSON value, converted by the property's own type (enums by
// nick). false: no such property, or a value of the wrong kind.
bool set(Gtk::Widget& w, const std::string& catalog_prop, const core::Value& v);

// GTK's default for it on a catalog type ("Button"), as a JSON value. nullopt
// when unknown (no such GObject property, or the type isn't registered yet --
// it is once the canvas has built one).
std::optional<core::Value> default_of(const std::string& type, const std::string& catalog_prop);

}  // namespace mortimer::designer::gprops
