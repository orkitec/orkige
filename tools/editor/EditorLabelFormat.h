// EditorLabelFormat - pure display-name prettifiers for the Inspector.
//
// The reflected property/type names are terse identifiers (camelCase, a
// "Component" suffix). These helpers turn one into a human display label PURELY
// FOR PRESENTATION - the schema keys, serialization and MCP names are the raw
// identifiers and never change. Kept UI-independent (no ImGui) in the editor_core
// library so it is headless-unit-testable.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <string>

namespace Orkige
{
	//! @brief a reflected property name as a spaced Title-Case label, for display
	//! only. Splits camelCase / acronym boundaries and capitalises each word:
	//! "position" -> "Position", "castShadows" -> "Cast Shadows", "x" -> "X",
	//! "designWidth" -> "Design Width", "zOrder" -> "Z Order". The input name is
	//! never mutated in the schema - this is a render-time transform.
	std::string prettifyPropertyLabel(std::string const& name);

	//! @brief a component TYPE name as a clean display title: drops a trailing
	//! "Component" and prettifies the rest - "ModelComponent" -> "Model",
	//! "RigidBodyComponent" -> "Rigid Body", "TransformComponent" -> "Transform".
	//! A name without the suffix is prettified as-is. The raw type name stays the
	//! reflection key (show it in a tooltip for honesty).
	std::string prettifyComponentTitle(std::string const& typeName);
}
