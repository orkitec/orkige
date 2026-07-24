// EditorPropertyWidgets.h - the GENERIC property-widget layer.
//
// One ImGui widget renderer keyed off Orkige::PropertyKind, so the SAME code
// draws a C++ component's reflected property and (later) a Lua script's dynamic
// export property. It serves the remote play-mode Inspector, which has no
// local schema - it consumes the stringly-typed reflection metadata streamed in
// the object_state message. drawPropertyWidget() also drives the LOCAL
// edit-mode Inspector (feeding it PropertyDesc.kind + PropertyValue::toString()
// and writing back through the reflected setter), retiring the per-component
// ImGui editors. Values cross as PropertyValue's canonical string form both
// ways, so this file needs no PropertyValue instance - only the kind enum.
#ifndef __EditorPropertyWidgets_h__9_7_2026__16_00_00__
#define __EditorPropertyWidgets_h__9_7_2026__16_00_00__

#include <core_base/PropertyValue.h> // Orkige::PropertyKind

#include <functional>
#include <string>
#include <vector>

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
	//! Quat only: show the rotation as human-readable Euler X/Y/Z degrees
	//! (default) vs the raw quaternion x/y/z/w. Display-only - the committed
	//! value is always the quaternion string either way.
	bool quatAsEuler = true;
};

//! @brief one candidate for a Reference-kind property picker: the value the
//! setter receives (an asset file name / an object id) and its display label.
struct PropertyRefOption
{
	std::string value;	//!< what the property is set to when picked
	std::string label;	//!< the combo display text (usually == value)
};

//! @brief OPTIONAL supplier of picker candidates for a Reference-kind property.
//! Given the widget descriptor (kind + asset-kind/object-type hint) it returns
//! the candidate list. The LOCAL inspector passes a provider backed by the
//! project's AssetDatabase (AssetRef) and the scene's object ids (ObjectRef);
//! the REMOTE inspector passes an EMPTY provider (it has no local schema/asset
//! database), so the reference field stays a free-text box - the widget layer
//! is renderer-shared and must stay remote-safe. An empty return (no matching
//! candidates) also falls back to the text field.
typedef std::function<std::vector<PropertyRefOption>(
	PropertyWidgetDesc const& desc)> PropertyRefProvider;

//! @brief does an asset file name belong to the asset-kind a reference property
//! hints at? Matches on the extension set the engine's resource pipeline
//! associates with each kind ("texture"/"mesh"/"sound"/"script"/"prefab"); an
//! unknown/empty hint matches every asset (a permissive picker beats an empty
//! one). Shared by the local inspector's reference picker AND this layer's
//! asset-ref drop target, so a dropped asset is kind-validated the same way the
//! combo filters candidates.
bool assetMatchesKind(std::string const& fileName, std::string const& kind);

//! @brief render the ImGui widget for a property whose current value is the
//! canonical string `value` (PropertyValue::toString()). Returns true and fills
//! `outValue` (the new canonical string) exactly when the user committed an
//! edit; a read-only descriptor renders disabled and always returns false. The
//! caller supplies id-scope uniqueness (e.g. ImGui::PushID(component)) when two
//! components could carry the same property name.
//! @param refProvider optional candidate supplier for the Reference kinds; when
//! present AND it yields candidates, AssetRef/ObjectRef render a searchable
//! combo, else they render the free-text field (the default remote-safe path).
//! @param outActivated optional: set true when the widget became active this
//! frame (a drag/combo/field grab). Composite kinds (Vec3/Quat, drawn as
//! several sub-items) OR the grab across their axes, so a per-axis drag still
//! opens the caller's undo-merge session exactly once - use this instead of
//! ImGui::IsItemActivated(), which would only see the last axis.
bool drawPropertyWidget(PropertyWidgetDesc const& desc,
	std::string const& value, std::string& outValue,
	PropertyRefProvider const& refProvider = PropertyRefProvider(),
	bool* outActivated = nullptr);

#endif // __EditorPropertyWidgets_h__9_7_2026__16_00_00__
