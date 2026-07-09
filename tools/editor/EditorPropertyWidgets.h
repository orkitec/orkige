// EditorPropertyWidgets.h - the GENERIC property-widget layer (task #94 P3).
//
// One ImGui widget renderer keyed off Orkige::PropertyKind, so the SAME code
// draws a C++ component's reflected property and (later) a Lua script's dynamic
// export property. P3 uses it for the remote play-mode Inspector, which has no
// local schema - it consumes the stringly-typed reflection metadata streamed in
// the object_state message. P4 reuses drawPropertyWidget() for the LOCAL
// edit-mode Inspector (feeding it PropertyDesc.kind + PropertyValue::toString()
// and writing back through the reflected setter), retiring the per-component
// ImGui editors. Values cross as PropertyValue's canonical string form both
// ways, so this file needs no PropertyValue instance - only the kind enum.
#ifndef __EditorPropertyWidgets_h__9_7_2026__16_00_00__
#define __EditorPropertyWidgets_h__9_7_2026__16_00_00__

#include <core_base/PropertyValue.h> // Orkige::PropertyKind

#include <string>

//! @brief the minimal per-property description a typed widget needs. Both the
//! remote path (parsed from the object_state metadata lists) and the future
//! local path (derived from a PropertyDesc) fill this the same way.
struct PropertyWidgetDesc
{
	std::string label;					//!< display label AND ImGui id seed
	Orkige::PropertyKind kind =
		Orkige::PropertyKind::String;	//!< the widget shape
	//! Enum: the "label=value,label=value,..." option table; AssetRef/ObjectRef:
	//! the asset-kind / object-type hint; unused for scalar/math kinds
	std::string hint;
	bool readOnly = false;				//!< render disabled, never report an edit
};

//! @brief render the ImGui widget for a property whose current value is the
//! canonical string `value` (PropertyValue::toString()). Returns true and fills
//! `outValue` (the new canonical string) exactly when the user committed an
//! edit; a read-only descriptor renders disabled and always returns false. The
//! caller supplies id-scope uniqueness (e.g. ImGui::PushID(component)) when two
//! components could carry the same property name.
bool drawPropertyWidget(PropertyWidgetDesc const& desc,
	std::string const& value, std::string& outValue);

#endif // __EditorPropertyWidgets_h__9_7_2026__16_00_00__
