// EditorInspectorPanel.cpp - the Inspector panel: the per-component editors
// (Transform/Model/Script/RigidBody/Camera/Sprite), the Add Component popup
// and the remote play-mode object_state view.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "AnimationPreviewStage.h"
#include "EditorApp.h"
#include "EditorLabelFormat.h"
#include "EditorPropertyWidgets.h"
#include "EditorTheme.h"
#include "SyntaxHighlight.h"
#include "ImGuiFacadeRenderer.h"
#include "MeshPreviewStage.h"

#include <core_base/PropertySchema.h>
#include <core_base/TypeManager.h>
#include <core_game/GameObjectManager.h>
#include <core_project/AssetDatabase.h>
#include <core_util/MaterialAsset.h>
#include <core_util/StringUtil.h>
#include <core_util/VectorShapeAsset.h>
#include <core_util/VectorShapeRaster.h>
#include <core_util/VectorTessellator.h>

#include <engine_render/RenderSystem.h>
#include <engine_render/RenderTexture.h>
#include <engine_render/RenderMaterial.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <fstream>
#include <sstream>

using Orkige::optr;
using Orkige::woptr;

namespace
{

//! @brief the LOCAL inspector's reference-picker provider: AssetRef candidates
//! come from the open project's AssetDatabase (filtered by asset-kind hint),
//! ObjectRef candidates are the scene's GameObject ids. The remote inspector
//! passes NO provider (keeps the text field) - this is the local-only half.
std::vector<PropertyRefOption> collectReferenceOptions(EditorState& state,
	Orkige::EditorCore& core, PropertyWidgetDesc const& widget)
{
	std::vector<PropertyRefOption> options;
	if (widget.kind == Orkige::PropertyKind::AssetRef)
	{
		if (optr<Orkige::AssetDatabase> const& database =
			state.project.getAssetDatabase())
		{
			for (Orkige::AssetEntry const& asset : database->listAssets())
			{
				if (assetMatchesKind(asset.fileName, widget.hint))
				{
					options.push_back({ asset.fileName, asset.fileName });
				}
			}
		}
	}
	else if (widget.kind == Orkige::PropertyKind::ObjectRef)
	{
		for (auto const& entry : core.getGameObjectManager().getGameObjects())
		{
			options.push_back({ entry.first, entry.first });
		}
	}
	return options;
}

//! @brief the widget hint a reflected property needs beyond its kind: Enum ->
//! the "label=value,label=value,..." option table (from the EnumInfo registry),
//! AssetRef/ObjectRef -> the asset-kind / object-type hint, everything else ->
//! "". The LOCAL cousin of PlayerRuntime::propertyHint (the remote path builds
//! the same string on the player side and streams it).
std::string propertyWidgetHint(Orkige::PropertyDesc const& desc)
{
	if (desc.kind == Orkige::PropertyKind::Enum)
	{
		std::string options;
		if (Orkige::EnumInfo const* enumInfo =
			Orkige::TypeManager::getSingleton().findEnum(desc.enumTypeName))
		{
			for (auto const& labelled : enumInfo->values())
			{
				if (!options.empty())
				{
					options += ",";
				}
				options += labelled.first + "=" +
					std::to_string(labelled.second);
			}
		}
		return options;
	}
	if (desc.kind == Orkige::PropertyKind::AssetRef ||
		desc.kind == Orkige::PropertyKind::ObjectRef)
	{
		return desc.referenceHint;
	}
	return std::string();
}

// The AUTO Inspector: render ONE component's editable properties
// GENERICALLY off its reflection schema, retiring the six hand-written per-
// component ImGui editors. For every declared PropertyDesc we read the live
// value (desc.get -> canonical string), draw the typed widget by its
// PropertyKind (the SAME drawPropertyWidget the remote play-mode inspector uses),
// and route an edit through EditorCore's generic undoable
// PropertyChangeCommand - the reflected setter makes the change take effect live
// in the viewport (a texture/mesh reloads, a transform moves the node). New
// components and (later) script export properties appear here with ZERO editor
// code. Edit-mode filtering: PROP_HIDDEN and PROP_TRANSIENT (runtime telemetry
// like velocities / has_body / script started) are hidden while editing;
// PROP_READONLY and getter-less properties render disabled. Drags collapse to
// one undo step via the merge session (IsItemActivated opens it, matching the
// gizmo/old-inspector drag bracketing).
//! the label/value property grid for ONE component, rendered off `schema`
//! (fetched once by the caller so the header can also read it). A two-column
//! table lays out a left-aligned label column (~40%) and a value column that
//! fills with the typed widget - so labels read LEFT, values RIGHT. `skipName`
//! is a property the header already hosts (the enable toggle) and must not
//! repeat in the body; pass "" to show every property.
void drawComponentProperties(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId, std::string const& componentName,
	Orkige::PropertySchema const& schema, std::string const& skipName)
{
	if (schema.empty())
	{
		ImGui::TextDisabled("(no editable properties yet)");
		return;
	}
	// the two-column grid: a proportional 40/60 split that follows the panel
	// width (labels left, value widgets right). A recessed 1px field border
	// makes each input read as a well; the table itself draws no borders.
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, Orkige::editorFieldBorderColor());
	bool any = false;
	// no PadOuterX: the value column runs to the panel's right edge (no
	// artificial outer margin), only the panel's own window padding remains
	if (ImGui::BeginTable("##props", 2, ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch,
			0.40f);
		ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch,
			0.60f);
		for (Orkige::PropertyDesc const& desc : schema.properties())
		{
			// edit-mode filtering: never show a hidden property or transient
			// runtime telemetry (velocities, has_body, script started/error);
			// a property with no getter has nothing to read/display; the header
			// already hosts skipName (the enable toggle)
			if (desc.hasFlag(Orkige::PROP_HIDDEN) ||
				desc.hasFlag(Orkige::PROP_TRANSIENT) || !desc.get ||
				desc.name == skipName)
			{
				continue;
			}
			std::string value;
			if (!core.getObjectProperty(objectId, componentName, desc.name,
				value))
			{
				continue;
			}
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			// display-only prettified label; the schema key (desc.name) is
			// untouched and drives the edit/serialization path below
			ImGui::TextUnformatted(
				Orkige::prettifyPropertyLabel(desc.name).c_str());
			const bool isRotation =
				desc.kind == Orkige::PropertyKind::Quat && gViewSettings;
			if (!desc.meta.tooltip.empty())
			{
				ImGui::SetItemTooltip("%s", desc.meta.tooltip.c_str());
			}
			else if (isRotation)
			{
				ImGui::SetItemTooltip(
					"right-click to change rotation display (Euler / Quaternion)");
			}
			// a rotation row opens the Euler/Quaternion display chooser on
			// right-click (of the label OR the fields); the View menu carries the
			// same choice. The setting is global + persisted.
			const std::string rotPopupId = "rotdisp##" + desc.name;
			if (isRotation)
			{
				ImGui::OpenPopupOnItemClick(rotPopupId.c_str(),
					ImGuiPopupFlags_MouseButtonRight);
			}
			ImGui::TableSetColumnIndex(1);
			PropertyWidgetDesc widget;
			// hide the widget's own label - the label column carries the name;
			// the "##name" seed keeps the ImGui id unique within this component
			widget.label = "##" + desc.name;
			widget.kind = desc.kind;
			widget.hint = propertyWidgetHint(desc);
			widget.readOnly = desc.isReadOnly();
			widget.quatAsEuler = gViewSettings
				? gViewSettings->rotationAsEuler : true;
			std::string edited;
			ImGui::SetNextItemWidth(-FLT_MIN);	// value widget fills the column
			// the reference-picker provider (AssetRef/ObjectRef): backed by the
			// project's AssetDatabase and the scene's object ids. Scalar/math
			// widgets ignore it; the remote inspector passes none (text field).
			PropertyRefProvider refProvider =
				[&state, &core](PropertyWidgetDesc const& w)
				{
					return collectReferenceOptions(state, core, w);
				};
			bool activated = false;
			const bool committed = drawPropertyWidget(widget, value, edited,
				refProvider, &activated);
			// bracket a drag/interaction into ONE undo step: a fresh merge
			// session opens when the widget becomes active (drag start / combo
			// open / click) and every edited frame of that interaction shares
			// it (mergeWith collapses them); the next interaction opens a new
			// session
			if (activated)
			{
				state.inspectorMergeSession = core.beginMergeSession();
			}
			if (committed)
			{
				core.applyPropertyChange(objectId, componentName, desc.name,
					value, edited, state.inspectorMergeSession);
			}
			if (!desc.meta.tooltip.empty())
			{
				ImGui::SetItemTooltip("%s", desc.meta.tooltip.c_str());
			}
			// rotation display chooser: right-click on the fields opens the same
			// popup as the label (above); the shared body is drawn once here
			if (isRotation)
			{
				ImGui::OpenPopupOnItemClick(rotPopupId.c_str(),
					ImGuiPopupFlags_MouseButtonRight);
				if (ImGui::BeginPopup(rotPopupId.c_str()))
				{
					const bool euler = gViewSettings->rotationAsEuler;
					if (ImGui::MenuItem("Euler Angles", nullptr, euler))
					{
						gViewSettings->rotationAsEuler = true;
						gViewSettings->save();
					}
					if (ImGui::MenuItem("Quaternion", nullptr, !euler))
					{
						gViewSettings->rotationAsEuler = false;
						gViewSettings->save();
					}
					ImGui::EndPopup();
				}
			}
			// a text-editable asset reference (a ScriptComponent's script path,
			// a .oui/.omat ref) offers "Open in External Editor" on right-click
			// - the context action that jumps to the file in the code editor
			if (desc.kind == Orkige::PropertyKind::AssetRef && !value.empty() &&
				isTextEditableAsset(value))
			{
				const std::string popupId =
					componentName + "." + desc.name + "##openext";
				if (ImGui::BeginPopupContextItem(popupId.c_str()))
				{
					if (ImGui::MenuItem("Open in External Editor") &&
						gViewSettings)
					{
						openInExternalEditor(
							resolveProjectFilePath(state.project, value), 0,
							*gViewSettings);
					}
					ImGui::EndPopup();
				}
			}
			any = true;
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleColor();		// field border
	ImGui::PopStyleVar();		// FrameBorderSize
	if (!any)
	{
		ImGui::TextDisabled("(no editable properties yet)");
	}
}

//! @brief the reflected property a component's header hosts as an enable toggle:
//! a live, editable Bool named "enabled". Returns its name, or "" when the
//! component has no such property (most components - only a header checkbox is
//! offered where one exists). A rendering-visibility flag ("visible") is a
//! DIFFERENT concept and stays a normal body row, so this looks for "enabled"
//! only.
std::string componentEnableProperty(Orkige::PropertySchema const& schema)
{
	for (Orkige::PropertyDesc const& desc : schema.properties())
	{
		if (desc.name == "enabled" &&
			desc.kind == Orkige::PropertyKind::Bool &&
			desc.get && desc.set && !desc.isReadOnly() &&
			!desc.hasFlag(Orkige::PROP_HIDDEN) &&
			!desc.hasFlag(Orkige::PROP_TRANSIENT))
		{
			return desc.name;
		}
	}
	return std::string();
}

//! case-insensitive substring match for the Add Component search box
bool containsIgnoreCase(std::string const& haystack, std::string const& needle)
{
	if (needle.empty())
	{
		return true;
	}
	return Orkige::StringUtil::to_lower_copy(haystack).find(
		Orkige::StringUtil::to_lower_copy(needle)) != std::string::npos;
}

// The "Add Component" button + searchable popup at the bottom of the
// Inspector: lists every registered component type (already-attached ones
// are disabled), the text box filters, a click adds through the undoable
// AddComponentCommand (dependencies come along automatically).
void drawAddComponentButton(EditorState& state, Orkige::EditorCore& core,
	optr<Orkige::GameObject> const& gameObject)
{
	ImGui::Spacing();
	// a comfortable, inviting primary action: a generously wide (~70% of the
	// panel, at least the label) and TALLER-than-default button, centred. Width
	// stays clamped so a very narrow Inspector still fits and the scaled font at
	// 2x/3x never clips "Add Component".
	const char* addLabel = "Add Component";
	const float labelWidth = ImGui::CalcTextSize(addLabel).x +
		ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetTextLineHeight();
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	const float buttonWidth = std::min(availableWidth,
		std::max(labelWidth, availableWidth * 0.70f));
	const float buttonHeight = ImGui::GetFrameHeight() * 1.6f;
	if (availableWidth > buttonWidth)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
			(availableWidth - buttonWidth) * 0.5f);
	}
	if (ImGui::Button(addLabel, ImVec2(buttonWidth, buttonHeight)))
	{
		state.addComponentSearch[0] = '\0';
		state.addComponentFocusPending = true;
		ImGui::OpenPopup("##addcomponent");
	}
	if (ImGui::BeginPopup("##addcomponent"))
	{
		if (state.addComponentFocusPending)
		{
			ImGui::SetKeyboardFocusHere();
			state.addComponentFocusPending = false;
		}
		ImGui::SetNextItemWidth(240.0f);
		ImGui::InputTextWithHint("##componentsearch", "search components...",
			state.addComponentSearch, sizeof(state.addComponentSearch));
		ImGui::Separator();
		for (std::string const& typeName : core.getAddableComponentTypes())
		{
			if (!containsIgnoreCase(typeName, state.addComponentSearch))
			{
				continue;
			}
			const bool attached =
				gameObject->hasComponent(Orkige::TypeInfo(typeName));
			ImGui::BeginDisabled(attached);
			if (ImGui::MenuItem(typeName.c_str(), attached ? "added" : nullptr))
			{
				if (!core.addComponentToObject(gameObject->getObjectID(),
					typeName))
				{
					oDebugError("editor.inspector", 0, "adding " << typeName <<
						" to '" << gameObject->getObjectID() << "' failed");
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
		}
		ImGui::EndPopup();
	}
}

// remote inspector helper: render ONE streamed property with a typed widget
// (drawPropertyWidget) and send set_property when the user edits
// it. `key` is the "<Component>.<property>" schema key; `component`/`property`
// are its split halves (the wire fields set_property needs). Read-only and
// getter-less properties render disabled - the reflection metadata says which.
void drawRemoteProperty(PlaySession& session, std::string const& key,
	std::string const& component, std::string const& property)
{
	std::map<std::string, std::string>::const_iterator value =
		session.stateProperties.find(key);
	if (value == session.stateProperties.end())
	{
		return;
	}
	PropertyWidgetDesc desc;
	// the label column carries the name; hide the widget's own label (a unique
	// "##name" id keeps ImGui state stable per property)
	desc.label = "##" + property;
	std::map<std::string, int>::const_iterator kind =
		session.statePropKind.find(key);
	desc.kind = (kind != session.statePropKind.end())
		? static_cast<Orkige::PropertyKind>(kind->second)
		: Orkige::PropertyKind::String;
	std::map<std::string, std::string>::const_iterator hint =
		session.statePropHint.find(key);
	if (hint != session.statePropHint.end())
	{
		desc.hint = hint->second;
	}
	desc.readOnly = session.statePropReadonly.count(key) != 0;
	desc.quatAsEuler = gViewSettings ? gViewSettings->rotationAsEuler : true;
	std::string edited;
	if (drawPropertyWidget(desc, value->second, edited))
	{
		Orkige::DebugMessage set(Protocol::MSG_SET_PROPERTY);
		set.set(Protocol::FIELD_ID, session.stateObjectId);
		set.set(Protocol::FIELD_COMPONENT, component);
		set.set(Protocol::FIELD_PROPERTY, property);
		set.set(Protocol::FIELD_VALUE, edited);
		session.client.send(set);
	}
}

// Inspector content during play: the streamed object_state of the selected
// remote object, rendered GENERICALLY off the reflection metadata.
// Every reflected property gets a typed widget by its PropertyKind
// (float->drag, vec3->3 drags, bool->checkbox, enum->combo, ...); an edit
// sends set_property. No per-component code - new components / script exports
// appear here automatically.
void drawRemoteInspector(PlaySession& session)
{
	if (session.remoteSelectedId.empty())
	{
		ImGui::TextDisabled("nothing selected (remote)");
		return;
	}
	if (session.stateObjectId != session.remoteSelectedId)
	{
		ImGui::TextDisabled("waiting for '%s' state...",
			session.remoteSelectedId.c_str());
		return;
	}
	// the live active checkbox (set_active is additive protocol v1 - an old
	// player answers with an error line instead of applying it); the flag
	// comes from the hierarchy stream, which the player refreshes on change
	if (session.remoteActive.size() == session.remoteHierarchy.size())
	{
		for (std::size_t index = 0; index < session.remoteHierarchy.size();
			++index)
		{
			if (session.remoteHierarchy[index] != session.stateObjectId)
			{
				continue;
			}
			bool activeSelf = session.remoteActive[index] != "0";
			if (Orkige::compactCheckbox("##remoteActive", &activeSelf))
			{
				setRemoteObjectActive(session, session.stateObjectId,
					activeSelf);
			}
			ImGui::SetItemTooltip("Active");
			ImGui::SameLine();
			break;
		}
	}
	ImGui::Text("%s", session.stateObjectId.c_str());
	ImGui::TextDisabled("remote object (live)");
	ImGui::Separator();
	// component header bars, matching the edit-mode inspector's look
	ImGui::PushStyleColor(ImGuiCol_Header,
		Orkige::editorComponentHeaderColor());
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
		Orkige::editorComponentHeaderHoverColor());
	ImGui::PushStyleColor(ImGuiCol_HeaderActive,
		Orkige::editorComponentHeaderHoverColor());
	for (std::string const& component : session.stateComponents)
	{
		const std::string headerLabel =
			Orkige::prettifyComponentTitle(component) + "###" + component;
		const bool headerOpen = ImGui::CollapsingHeader(headerLabel.c_str(),
			ImGuiTreeNodeFlags_DefaultOpen);
		ImGui::SetItemTooltip("%s", component.c_str());
		if (!headerOpen)
		{
			continue;
		}
		ImGui::PushID(component.c_str());
		const std::string prefix = component + ".";
		bool any = false;
		if (!session.statePropKeys.empty())
		{
			// typed widgets in a label-left / value-right grid, same as the
			// edit-mode inspector (declaration order preserved)
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Border,
				Orkige::editorFieldBorderColor());
			if (ImGui::BeginTable("##rprops", 2,
				ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("label",
					ImGuiTableColumnFlags_WidthStretch, 0.40f);
				ImGui::TableSetupColumn("value",
					ImGuiTableColumnFlags_WidthStretch, 0.60f);
				for (std::string const& key : session.statePropKeys)
				{
					if (key.rfind(prefix, 0) != 0)
					{
						continue;
					}
					const std::string prop = key.substr(prefix.size());
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(
						Orkige::prettifyPropertyLabel(prop).c_str());
					// a streamed Quat row gets the same right-click Euler/
					// Quaternion chooser as the edit-mode inspector (label OR
					// fields open it; the View menu carries it too)
					std::map<std::string, int>::const_iterator kindIt =
						session.statePropKind.find(key);
					const bool remoteRotation = gViewSettings &&
						kindIt != session.statePropKind.end() &&
						static_cast<Orkige::PropertyKind>(kindIt->second) ==
							Orkige::PropertyKind::Quat;
					const std::string rotPopupId = "rotdisp##" + key;
					if (remoteRotation)
					{
						ImGui::SetItemTooltip("right-click to change rotation "
							"display (Euler / Quaternion)");
						ImGui::OpenPopupOnItemClick(rotPopupId.c_str(),
							ImGuiPopupFlags_MouseButtonRight);
					}
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-FLT_MIN);
					drawRemoteProperty(session, key, component, prop);
					if (remoteRotation)
					{
						ImGui::OpenPopupOnItemClick(rotPopupId.c_str(),
							ImGuiPopupFlags_MouseButtonRight);
						if (ImGui::BeginPopup(rotPopupId.c_str()))
						{
							const bool euler = gViewSettings->rotationAsEuler;
							if (ImGui::MenuItem("Euler Angles", nullptr, euler))
							{
								gViewSettings->rotationAsEuler = true;
								gViewSettings->save();
							}
							if (ImGui::MenuItem("Quaternion", nullptr, !euler))
							{
								gViewSettings->rotationAsEuler = false;
								gViewSettings->save();
							}
							ImGui::EndPopup();
						}
					}
					any = true;
				}
				ImGui::EndTable();
			}
			ImGui::PopStyleColor();	// field border
			ImGui::PopStyleVar();	// FrameBorderSize
		}
		else
		{
			// fallback: a player predating the reflection metadata - untyped
			// read-only dump of whatever values it streamed
			for (auto const& [key, value] : session.stateProperties)
			{
				if (key.rfind(prefix, 0) == 0)
				{
					ImGui::Text("%s: %s", key.c_str() + prefix.size(),
						value.c_str());
					any = true;
				}
			}
		}
		if (!any)
		{
			ImGui::TextDisabled("(no properties streamed)");
		}
		ImGui::PopID();
	}
	ImGui::PopStyleColor(3); // component header bars
}

//! draw the editable fields of one texture import block (filter/wrap combos,
//! max-size cap, premultiply/generate-mips toggles); id-scoped by the caller so
//! the default and per-platform blocks never collide on the widget ids
void drawTextureSettingsFields(Orkige::TextureImportSettings& settings)
{
	if (ImGui::BeginCombo("Filter", settings.filter.c_str()))
	{
		for (const char* option : { "point", "bilinear" })
		{
			if (ImGui::Selectable(option, settings.filter == option))
			{
				settings.filter = option;
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::BeginCombo("Wrap", settings.wrap.c_str()))
	{
		for (const char* option : { "clamp", "wrap" })
		{
			if (ImGui::Selectable(option, settings.wrap == option))
			{
				settings.wrap = option;
			}
		}
		ImGui::EndCombo();
	}
	int maxSize = settings.maxSize;
	if (ImGui::InputInt("Max Size", &maxSize))
	{
		settings.maxSize = maxSize < 0 ? 0 : maxSize;	// 0 = uncapped
	}
	ImGui::SetItemTooltip("longest-side texel cap the export cook downscales "
		"to; 0 = uncapped");
	Orkige::compactCheckbox("Premultiply Alpha", &settings.premultiply);
	Orkige::compactCheckbox("Generate Mips", &settings.generateMips);
	ImGui::SetItemTooltip("bake an offline mip chain into the compressed "
		"container at export");
	if (ImGui::BeginCombo("Format", settings.format.c_str()))
	{
		for (const char* option : { "auto", "none", "astc-4x4", "astc-6x6",
			"astc-8x8", "etc2", "bc1", "bc3", "bc7" })
		{
			if (ImGui::Selectable(option, settings.format == option))
			{
				settings.format = option;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("GPU block compression the export cook applies: "
		"auto = the platform's best format, none = ship the PNG; an explicit "
		"format is validated against the export platform (the dev loop always "
		"renders the raw source)");
	if (ImGui::BeginCombo("Quality", settings.quality.c_str()))
	{
		for (const char* option : { "low", "normal", "high" })
		{
			if (ImGui::Selectable(option, settings.quality == option))
			{
				settings.quality = option;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("compression effort/quality; under auto it also "
		"picks the ASTC block size (high 4x4, normal 6x6, low 8x8)");
}

//! the texture-import PREVIEW block appended to the settings section: a
//! Base/Android/iOS platform selector (the two overrides appear only when
//! enabled), the source image drawn as it would import (scaled to the platform's
//! resolved maxSize downscale so the shrink is visible) and an "as imported:
//! WxH (from WxH source)" status line with the filter/wrap the runtime samples
//! with. The honest limits: ImGui draws the FULL-RES bindable texture scaled
//! down (there is no CPU image decode or texture readback in the tree to
//! re-upload a point-sampled downscaled copy), so the shrink is shown by draw
//! scale, not a re-cook - the DIMENSIONS are exact (the pure appliedSize the
//! export cook shares), the pixels are a linear-sampled approximation.
void drawTexturePreviewBlock(EditorState& state, std::string const& relativePath)
{
	AssetBrowserState& browser = state.assetBrowser;
	Orkige::RenderSystem* render = Orkige::RenderSystem::get();
	if (!render || !gImGuiRenderer)
	{
		return;
	}
	const std::string bareName =
		std::filesystem::path(relativePath).filename().string();
	unsigned int srcW = 0;
	unsigned int srcH = 0;
	// getTextureSize LOADS + measures the real image; a miss means it is not a
	// bindable texture (a broken/non-image file) - say so honestly
	if (!render->getTextureSize(bareName, srcW, srcH) || srcW == 0 || srcH == 0)
	{
		ImGui::Separator();
		ImGui::TextDisabled("preview unavailable (image did not load)");
		return;
	}
	ImGui::Separator();
	ImGui::TextUnformatted("Preview");
	// the platform selector: Base always, Android/iOS only when an override is
	// enabled (otherwise they resolve to Base - nothing to choose). A dropped
	// override snaps the selection back to Base.
	std::vector<std::pair<const char*, const char*>> platforms;
	platforms.push_back({ "Base", "" });
	if (browser.editImport.hasAndroid)
	{
		platforms.push_back({ "Android", "android" });
	}
	if (browser.editImport.hasIos)
	{
		platforms.push_back({ "iOS", "ios" });
	}
	if (browser.editImport.hasWeb)
	{
		platforms.push_back({ "Web", "web" });
	}
	bool valid = false;
	for (auto const& option : platforms)
	{
		if (browser.previewPlatform == option.second)
		{
			valid = true;
			break;
		}
	}
	if (!valid)
	{
		browser.previewPlatform.clear();
	}
	const char* currentLabel = "Base";
	for (auto const& option : platforms)
	{
		if (browser.previewPlatform == option.second)
		{
			currentLabel = option.first;
		}
	}
	if (ImGui::BeginCombo("Platform", currentLabel))
	{
		for (auto const& option : platforms)
		{
			if (ImGui::Selectable(option.first,
				browser.previewPlatform == option.second))
			{
				browser.previewPlatform = option.second;
			}
		}
		ImGui::EndCombo();
	}
	// the resolved settings drive the imported dimensions (the live edit cache,
	// so the preview follows an in-flight maxSize change before Apply)
	Orkige::TextureImportSettings const& resolved =
		browser.editImport.resolvedFor(browser.previewPlatform);
	int impW = 0;
	int impH = 0;
	resolved.appliedSize(static_cast<int>(srcW), static_cast<int>(srcH),
		impW, impH);
	// draw the image at the imported pixel size, capped to the inspector width
	// (so a big texture is not clipped) - the downscale reads visually
	const float avail = ImGui::GetContentRegionAvail().x;
	float drawW = static_cast<float>(impW);
	float drawH = static_cast<float>(impH);
	if (drawW > avail && drawW > 0.0f)
	{
		const float scale = avail / drawW;
		drawW *= scale;
		drawH *= scale;
	}
	const ImTextureID id = gImGuiRenderer->textureIdForResource(bareName);
	if (id != 0 && drawW > 0.0f && drawH > 0.0f)
	{
		ImGui::Image(id, ImVec2(drawW, drawH));
	}
	ImGui::Text("as imported: %d x %d", impW, impH);
	ImGui::SameLine();
	ImGui::TextDisabled("(from %u x %u source)", srcW, srcH);
	ImGui::TextDisabled("%s filter, %s wrap%s, format %s (%s)",
		resolved.filter.c_str(), resolved.wrap.c_str(),
		resolved.premultiply ? ", premultiplied" : "",
		resolved.format.c_str(), resolved.quality.c_str());
}

//! the "Texture Import Settings" Inspector section: shown when the SCENE
//! selection is empty AND exactly one id-tracked TEXTURE is selected in the
//! Asset browser. Reads the sidecar's import block on a selection change, edits
//! it live in an AssetBrowserState cache, and writes it back (id preserved) on
//! Apply through applyTextureImportEdit. Returns true when it drew the section.
bool drawTextureImportSection(EditorState& state)
{
	std::string metaFilePath;
	std::string assetId;
	if (!selectedBrowserTextureMeta(state, metaFilePath, assetId))
	{
		// honesty for the near-miss: a single selected texture WITHOUT a
		// sidecar has no settings to edit - say so instead of falling back
		// to "nothing selected" (which reads as the feature being gone)
		AssetBrowserState const& browser = state.assetBrowser;
		if (browser.selection.size() == 1 && state.project.isLoaded() &&
			classifyAsset(*browser.selection.begin()) == AssetKind::Texture)
		{
			ImGui::TextUnformatted("Texture Import Settings");
			ImGui::Separator();
			ImGui::TextDisabled(
				"not an imported asset (no .orkmeta sidecar) -\n"
				"import it through the Asset browser to edit settings");
			return true;
		}
		return false;
	}
	AssetBrowserState& browser = state.assetBrowser;
	// (re)read from disk on a selection change OR when the sidecar file
	// changed underneath (an MCP write_project_file rewriting it must show up
	// without a re-select); an unset/id-only sidecar leaves the defaults
	long long fileTime = 0;
	{
		std::error_code ec;
		const std::filesystem::file_time_type mtime =
			std::filesystem::last_write_time(metaFilePath, ec);
		fileTime = ec ? 0
			: static_cast<long long>(mtime.time_since_epoch().count());
	}
	if (browser.editImportPath != metaFilePath ||
		browser.editImportFileTime != fileTime)
	{
		Orkige::TextureImport loaded;
		Orkige::AssetDatabase::readImportSettings(metaFilePath, loaded);
		browser.editImport = loaded;
		browser.editImportPath = metaFilePath;
		browser.editImportFileTime = fileTime;
	}
	const std::string relativePath = *browser.selection.begin();
	ImGui::TextUnformatted("Texture Import Settings");
	ImGui::TextDisabled("%s",
		std::filesystem::path(relativePath).filename().string().c_str());
	ImGui::Separator();
	drawTextureSettingsFields(browser.editImport.base);
	// per-platform overrides: enabling one seeds it from the resolved default
	ImGui::PushID("android");
	if (Orkige::compactCheckbox("Android override", &browser.editImport.hasAndroid) &&
		browser.editImport.hasAndroid)
	{
		browser.editImport.android = browser.editImport.base;
	}
	if (browser.editImport.hasAndroid)
	{
		drawTextureSettingsFields(browser.editImport.android);
	}
	ImGui::PopID();
	ImGui::PushID("ios");
	if (Orkige::compactCheckbox("iOS override", &browser.editImport.hasIos) &&
		browser.editImport.hasIos)
	{
		browser.editImport.ios = browser.editImport.base;
	}
	if (browser.editImport.hasIos)
	{
		drawTextureSettingsFields(browser.editImport.ios);
	}
	ImGui::PopID();
	ImGui::PushID("web");
	if (Orkige::compactCheckbox("Web override", &browser.editImport.hasWeb) &&
		browser.editImport.hasWeb)
	{
		browser.editImport.web = browser.editImport.base;
	}
	if (browser.editImport.hasWeb)
	{
		drawTextureSettingsFields(browser.editImport.web);
	}
	ImGui::PopID();
	ImGui::Separator();
	if (ImGui::Button("Apply"))
	{
		applyTextureImportEdit(state, browser.editImport);
	}
	drawTexturePreviewBlock(state, relativePath);
	return true;
}

//! the lowercase extension (with dot) of the single browser selection, or ""
//! when the selection is not exactly one asset in a loaded project
std::string singleSelectionExtension(EditorState& state, std::string& outRel)
{
	AssetBrowserState const& browser = state.assetBrowser;
	if (browser.selection.size() != 1 || !state.project.isLoaded())
	{
		return std::string();
	}
	outRel = *browser.selection.begin();
	return Orkige::StringUtil::to_lower_copy(
		std::filesystem::path(outRel).extension().string());
}

//! the animation the single selection targets: a `.oanim` directly, or a
//! Lottie `.json` whose sibling `.oanim` exists (the import keeps the pair).
//! Returns the project-relative `.oanim` path, or "".
std::string selectedBrowserAnimation(EditorState& state)
{
	std::string rel;
	const std::string ext = singleSelectionExtension(state, rel);
	if (ext == ".oanim")
	{
		return rel;
	}
	if (ext == ".json")
	{
		const std::string sibling =
			std::filesystem::path(rel).replace_extension(".oanim").generic_string();
		const std::string absolute = state.project.resolvePath(sibling);
		std::error_code ec;
		if (std::filesystem::is_regular_file(absolute, ec))
		{
			return sibling;
		}
	}
	return std::string();
}

//! the KEPT Lottie source of the animation the single selection targets: the
//! selected `.json` itself, or the selected `.oanim`'s sibling `.json` when
//! the import kept it. Returns the project-relative source path, or "" (an
//! orphan rig authored directly as `.oanim` text has no source).
std::string selectedBrowserAnimationSource(EditorState& state)
{
	std::string rel;
	const std::string ext = singleSelectionExtension(state, rel);
	if (ext == ".json")
	{
		return rel;
	}
	if (ext == ".oanim")
	{
		const std::string sibling = std::filesystem::path(rel)
			.replace_extension(".json").generic_string();
		std::error_code ec;
		if (std::filesystem::is_regular_file(
			state.project.resolvePath(sibling), ec))
		{
			return sibling;
		}
	}
	return std::string();
}

//! the animation preview's freshness key for a rig: the RECORDED sourceHash of
//! its kept source's sidecar (a re-cook rewrites the record, so the key moves
//! exactly when the artifact did), falling back to the artifact's own write
//! time for a record-less pair or an orphan `.oanim`.
std::string animationFreshnessKey(EditorState& state,
	std::string const& animRel)
{
	const std::string sourceRel = selectedBrowserAnimationSource(state);
	if (!sourceRel.empty())
	{
		Orkige::CookRecord record;
		if (Orkige::AssetDatabase::readCookRecord(
			state.project.resolvePath(sourceRel) +
			Orkige::AssetDatabase::META_FILE_EXTENSION, record) &&
			!record.sourceHash.empty())
		{
			return record.sourceHash;
		}
	}
	std::error_code ec;
	const std::filesystem::file_time_type mtime =
		std::filesystem::last_write_time(
			state.project.resolvePath(animRel), ec);
	return ec ? std::string() : std::to_string(
		static_cast<long long>(mtime.time_since_epoch().count()));
}

//! the "Animation Import Settings" block heading the preview section when the
//! selection is a cooked PAIR (a kept source exists): shows the sidecar's
//! recorded cook settings (--clips/--extent/--tolerance, verbatim CLI text)
//! with edit fields, and Apply re-cooks the pair with the edited settings
//! (which become the recorded intent) - the texture-import-settings pattern
//! for cooked pairs. An orphan `.oanim` has no source and no settings.
void drawAnimationImportSettings(EditorState& state,
	std::string const& sourceRel)
{
	AssetBrowserState& browser = state.assetBrowser;
	const std::string metaPath = state.project.resolvePath(sourceRel) +
		Orkige::AssetDatabase::META_FILE_EXTENSION;
	// (re)read from disk on a selection change OR after any cook (the revision
	// moves - a watcher/scan/MCP re-cook may have rewritten the record) so the
	// fields always reflect the recorded state
	if (browser.editCookMetaPath != metaPath ||
		browser.editCookSeenRevision != browser.animImportsRevision)
	{
		Orkige::CookRecord record;
		browser.editCookHasRecord =
			Orkige::AssetDatabase::readCookRecord(metaPath, record);
		browser.editCook = record.settings;
		browser.editCookMetaPath = metaPath;
		browser.editCookSeenRevision = browser.animImportsRevision;
	}
	ImGui::TextUnformatted("Animation Import Settings");
	ImGui::TextDisabled("%s",
		std::filesystem::path(sourceRel).filename().string().c_str());
	if (!browser.editCookHasRecord)
	{
		ImGui::TextDisabled("no recorded import yet - Apply cooks and records");
	}
	auto textField = [](char const* label, char const* hint,
		std::string& value)
	{
		char buffer[256];
		SDL_strlcpy(buffer, value.c_str(), sizeof(buffer));
		if (ImGui::InputTextWithHint(label, hint, buffer, sizeof(buffer)))
		{
			value = buffer;
		}
	};
	textField("clips", "document markers", browser.editCook.clips);
	ImGui::SetItemTooltip("clip ranges overriding the document markers:\n"
		"name:start:end[:loop|once],...  (frames)");
	textField("extent", "2", browser.editCook.extent);
	ImGui::SetItemTooltip(
		"world-unit size the composition's larger side spans");
	textField("tolerance", "auto", browser.editCook.tolerance);
	ImGui::SetItemTooltip("flatten chord tolerance in composition units");
	if (ImGui::Button("Apply (re-cook)"))
	{
		std::string cookError;
		if (recookAnimationPair(state, sourceRel, &browser.editCook,
			&cookError).empty())
		{
			// keep the edited fields for a fix-up; the record is unchanged
			browser.statusMessage = "re-cook failed: " + cookError;
			browser.statusMessageExpiry = ImGui::GetTime() + 6.0;
		}
		else
		{
			// the record now carries the edits - re-read on the next draw
			browser.editCookMetaPath.clear();
		}
	}
	ImGui::Separator();
}

//! the inline animation-preview section: shown when the SCENE selection is
//! empty AND the single browser selection is a `.oanim` (or a `.json` with a
//! cooked sibling). A cooked pair heads it with the Animation Import Settings
//! block. Loads the rig into the SHARED preview stage on a selection change
//! OR when a re-cook made the loaded rig stale - the guard keys on the
//! sidecar's RECORDED source hash (via the animation-import revision counter,
//! so an unchanged frame does no sidecar IO) - and draws the shared animation
//! preview widget (clip dropdown, Play/Pause/Reset, scrub, blend, status +
//! pose image). Returns true when it drew the section.
bool drawAnimationPreviewSection(EditorState& state,
	OrkigeEditor::AnimationPreviewStage& stage)
{
	const std::string animRel = selectedBrowserAnimation(state);
	if (animRel.empty())
	{
		return false;
	}
	AssetBrowserState& browser = state.assetBrowser;
	// stale-rig guard: check freshness on a selection change or whenever a
	// cook bumped the revision; reload only when the key actually moved
	if (stage.getLoadedFile() != animRel ||
		browser.animPreviewSeenRevision != browser.animImportsRevision)
	{
		browser.animPreviewSeenRevision = browser.animImportsRevision;
		const std::string key = animationFreshnessKey(state, animRel);
		if (stage.getLoadedFile() != animRel ||
			key != browser.animPreviewFreshnessKey)
		{
			std::string err;
			stage.load(state.project.getRootDirectory(), animRel, err);
			stage.clearBlend();
			browser.animPreviewFreshnessKey = key;
		}
	}
	const std::string sourceRel = selectedBrowserAnimationSource(state);
	if (!sourceRel.empty())
	{
		drawAnimationImportSettings(state, sourceRel);
	}
	ImGui::TextUnformatted("Animation Preview");
	ImGui::TextDisabled("%s",
		std::filesystem::path(animRel).filename().string().c_str());
	ImGui::Separator();
	if (!stage.isLoaded())
	{
		ImGui::TextWrapped("Could not preview '%s': %s", animRel.c_str(),
			stage.getLastError().c_str());
		return true;
	}
	drawAnimationPreviewBody(stage);
	return true;
}

//! is this extension one the Inspector shows highlighted source for? The
//! engine's text-authored formats plus the common plain-text kinds. A `.oanim`
//! shows the animation preview instead, and a texture its import settings, so
//! neither reaches this list.
bool isTextPreviewExtension(std::string const& ext)
{
	static const char* const kinds[] = {
		".lua", ".oui", ".ogui", ".omat", ".oshape", ".xlf", ".json",
		".oactions", ".olayers", ".olevels", ".orkproj", ".md", ".txt",
	};
	for (const char* kind : kinds)
	{
		if (ext == kind)
		{
			return true;
		}
	}
	return false;
}

//! (re)read the selected text file into the browser preview cache when the
//! selection changed. Reads at most TEXT_PREVIEW_CAP_BYTES and records the true
//! total so the footer can say how much was elided.
void refreshTextPreview(EditorState& state, std::string const& rel)
{
	AssetBrowserState& browser = state.assetBrowser;
	if (browser.textPreviewPath == rel)
	{
		return;
	}
	browser.textPreviewPath = rel;
	browser.textPreviewLines.clear();
	browser.textPreviewTruncated = false;
	browser.textPreviewTotalBytes = 0;
	browser.textPreviewFormat = Orkige::syntaxFormatForExtension(
		std::filesystem::path(rel).extension().string());
	const std::string absolute = state.project.resolvePath(rel);
	std::ifstream in(absolute, std::ios::binary);
	if (!in)
	{
		return;
	}
	in.seekg(0, std::ios::end);
	const std::streamoff size = in.tellg();
	browser.textPreviewTotalBytes = size > 0
		? static_cast<std::size_t>(size) : 0;
	in.seekg(0, std::ios::beg);
	const std::size_t cap = AssetBrowserState::TEXT_PREVIEW_CAP_BYTES;
	browser.textPreviewTruncated = browser.textPreviewTotalBytes > cap;
	std::vector<char> buffer(std::min(browser.textPreviewTotalBytes, cap));
	if (!buffer.empty())
	{
		in.read(buffer.data(),
			static_cast<std::streamsize>(buffer.size()));
	}
	const std::size_t got = buffer.empty() ? 0
		: static_cast<std::size_t>(in.gcount());
	// split into lines, stripping a trailing CR so a CRLF file reads clean
	std::string line;
	for (std::size_t i = 0; i < got; ++i)
	{
		const char c = buffer[i];
		if (c == '\n')
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			browser.textPreviewLines.push_back(line);
			line.clear();
		}
		else
		{
			line.push_back(c);
		}
	}
	if (!line.empty())
	{
		if (line.back() == '\r')
		{
			line.pop_back();
		}
		browser.textPreviewLines.push_back(line);
	}
}

//! the read-only, syntax-highlighted content section: shown when the single
//! browser selection is one of the text-preview formats. An "Open in External
//! Editor" button heads it; the content scrolls in a bordered child region with
//! per-token muted colours; a truncated file gets a footer line. Returns true
//! when it drew the section.
//! the .oui section header: a small static GUI-screen thumbnail (baked by the
//! main loop through the shared GuiPreviewStage - a .oui is a GPU render, not a
//! CPU raster, so it cannot bake mid-frame; the Inspector sets ouiPreviewRequest
//! and the loop bakes it post-render) plus an "Open Preview" button that opens
//! the full GUI Preview panel on this screen. The .oui syntax-text view follows
//! below (drawTextPreviewSection). A no-op unless a single .oui is selected.
void drawGuiPreviewInspectorHeader(EditorState& state)
{
	std::string rel;
	if (singleSelectionExtension(state, rel) != ".oui")
	{
		return;
	}
	AssetBrowserState& browser = state.assetBrowser;
	const std::string absolutePath = resolveProjectFilePath(state.project, rel);
	std::error_code ec;
	const long long mtime = static_cast<long long>(
		std::filesystem::last_write_time(absolutePath, ec)
			.time_since_epoch().count());
	const std::string key = absolutePath + "|" + std::to_string(mtime);

	ImGui::TextUnformatted("GUI Screen");
	ImGui::TextDisabled("%s",
		std::filesystem::path(rel).filename().string().c_str());

	// (re)bake when the file/mtime changed - the loop picks up the request
	if (browser.ouiPreviewKey != key)
	{
		browser.ouiPreviewRequest = absolutePath;
	}
	Orkige::RenderSystem* render = Orkige::RenderSystem::get();
	unsigned int tw = 0;
	unsigned int th = 0;
	const bool haveThumb = browser.ouiPreviewKey == key &&
		!browser.ouiPreviewUpload.empty() && render &&
		render->getTextureSize(browser.ouiPreviewUpload, tw, th) &&
		tw > 0 && th > 0;
	const float maxH = 190.0f;
	if (haveThumb && gImGuiRenderer)
	{
		const float aspect = static_cast<float>(tw) / static_cast<float>(th);
		ImGui::Image(gImGuiRenderer->textureIdForResource(
			browser.ouiPreviewUpload), ImVec2(maxH * aspect, maxH));
	}
	else
	{
		ImGui::Dummy(ImVec2(maxH * 0.5f, maxH * 0.5f));
		ImGui::SameLine();
		ImGui::TextDisabled("generating preview...");
	}
	if (ImGui::Button("Open Preview"))
	{
		// the full panel: a phone-portrait canvas + device / notch / language
		// controls that need more room than the Inspector column offers
		state.requestedGuiPreviewAsset = rel;
		if (gViewSettings)
		{
			gViewSettings->showGuiPreviewPanel = true;
			gViewSettings->save();
		}
	}
	ImGui::SetItemTooltip("open the full GUI Preview panel on this screen");
	ImGui::Separator();
}

bool drawTextPreviewSection(EditorState& state)
{
	std::string rel;
	const std::string ext = singleSelectionExtension(state, rel);
	if (ext.empty() || !isTextPreviewExtension(ext))
	{
		return false;
	}
	refreshTextPreview(state, rel);
	AssetBrowserState& browser = state.assetBrowser;

	if (gViewSettings && ImGui::Button("Open in External Editor"))
	{
		openInExternalEditor(resolveProjectFilePath(state.project, rel), 0,
			*gViewSettings);
	}
	ImGui::TextDisabled("%s",
		std::filesystem::path(rel).filename().string().c_str());
	ImGui::Separator();

	const bool dark = Orkige::currentEditorThemeVariant() ==
		Orkige::EditorThemeVariant::Dark;
	// leave room for the truncation footer below the scroll region
	const float footer = browser.textPreviewTruncated
		? ImGui::GetTextLineHeightWithSpacing() : 0.0f;
	ImGui::BeginChild("##textpreview", ImVec2(0.0f, -footer),
		ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
	// tight vertical spacing so a code listing reads as lines, not paragraphs
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
		ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y * 0.25f));
	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(browser.textPreviewLines.size()));
	while (clipper.Step())
	{
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
		{
			std::string const& line = browser.textPreviewLines[row];
			if (line.empty())
			{
				ImGui::NewLine();
				continue;
			}
			const std::vector<Orkige::SyntaxSpan> spans =
				Orkige::highlightLine(line, browser.textPreviewFormat);
			bool first = true;
			for (Orkige::SyntaxSpan const& span : spans)
			{
				if (!first)
				{
					ImGui::SameLine(0.0f, 0.0f);
				}
				first = false;
				const unsigned int rgba =
					Orkige::syntaxTokenColor(span.token, dark);
				ImGui::PushStyleColor(ImGuiCol_Text,
					IM_COL32((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF,
						(rgba >> 8) & 0xFF, rgba & 0xFF));
				ImGui::TextUnformatted(line.c_str() + span.begin,
					line.c_str() + span.end);
				ImGui::PopStyleColor();
			}
		}
	}
	clipper.End();
	ImGui::PopStyleVar();
	ImGui::EndChild();

	if (browser.textPreviewTruncated)
	{
		ImGui::TextDisabled("truncated - %zu KiB total",
			browser.textPreviewTotalBytes / 1024);
	}
	return true;
}

//! a human-friendly byte count for the no-preview line (B / KiB / MiB)
std::string humanFileSize(std::uintmax_t bytes)
{
	char buffer[64];
	if (bytes < 1024)
	{
		std::snprintf(buffer, sizeof(buffer), "%llu B",
			static_cast<unsigned long long>(bytes));
	}
	else if (bytes < 1024 * 1024)
	{
		std::snprintf(buffer, sizeof(buffer), "%.1f KiB", bytes / 1024.0);
	}
	else
	{
		std::snprintf(buffer, sizeof(buffer), "%.1f MiB",
			bytes / (1024.0 * 1024.0));
	}
	return buffer;
}

//! the fallback for a single selected asset that has no richer view (a mesh,
//! audio, an unknown/binary kind): a one-line size + "no preview" - never a
//! binary dump. Returns true when it drew (a folder selection returns false so
//! the caller shows "nothing selected"). Order-wise the LAST asset section.
bool drawUnknownAssetSection(EditorState& state)
{
	std::string rel;
	const std::string ext = singleSelectionExtension(state, rel);
	if (ext.empty() && rel.empty())
	{
		return false;
	}
	const std::string absolute = state.project.resolvePath(rel);
	std::error_code ec;
	if (!std::filesystem::is_regular_file(absolute, ec))
	{
		return false;	// a folder (or a vanished file): no asset section
	}
	ImGui::TextDisabled("%s",
		std::filesystem::path(rel).filename().string().c_str());
	ImGui::Separator();
	const std::uintmax_t size = std::filesystem::file_size(absolute, ec);
	ImGui::TextDisabled("%s - no preview",
		humanFileSize(ec ? 0 : size).c_str());
	return true;
}


//! as its project-relative path, or "" when the selection is not exactly one
std::string singleSelectedAsset(EditorState& state)
{
	AssetBrowserState const& browser = state.assetBrowser;
	if (!state.project.isLoaded() || browser.selection.size() != 1)
	{
		return std::string();
	}
	return *browser.selection.begin();
}

//! the ".oshape" Inspector section: the tessellated flat-colour fill rendered
//! at Inspector width (the thumbnail raster at a larger size, cached per file),
//! a "View source" toggle switching to the raw text, and a vertex/triangle
//! status line. Returns true when it drew the section.
bool drawShapeInspectorSection(EditorState& state,
	std::string const& relativePath)
{
	AssetBrowserState& browser = state.assetBrowser;
	const std::string absolutePath =
		resolveProjectFilePath(state.project, relativePath);
	std::error_code ec;
	const long long mtime = static_cast<long long>(
		std::filesystem::last_write_time(absolutePath, ec)
			.time_since_epoch().count());
	const std::string key = absolutePath + "|" + std::to_string(mtime);

	ImGui::TextUnformatted("Vector Shape");
	ImGui::TextDisabled("%s",
		std::filesystem::path(relativePath).filename().string().c_str());
	ImGui::Separator();
	Orkige::compactCheckbox("View source", &browser.shapeShowSource);

	// (re)build the raster + read the counts (and cache the source text) when
	// the selected file or its mtime changes
	Orkige::RenderSystem* render = Orkige::RenderSystem::get();
	if (browser.shapePreviewKey != key)
	{
		if (!browser.shapePreviewUpload.empty() && render)
		{
			render->destroyTexture2D(browser.shapePreviewUpload);
		}
		browser.shapePreviewUpload.clear();
		browser.shapePreviewVertices = 0;
		browser.shapePreviewTriangles = 0;
		browser.shapePreviewKey = key;
		browser.shapeSourceText.clear();
		std::ifstream in(absolutePath, std::ios::binary);
		if (in)
		{
			std::stringstream buffer;
			buffer << in.rdbuf();
			browser.shapeSourceText = buffer.str();
			std::vector<Orkige::VectorTessellator::Region> regions;
			if (Orkige::VectorShapeAsset::parse(browser.shapeSourceText, regions))
			{
				const Orkige::VectorTessellator::Bounds bounds =
					Orkige::VectorTessellator::computeBounds(regions);
				Orkige::VectorTessellator::Mesh mesh;
				Orkige::VectorTessellator::build(regions,
					Orkige::VectorTessellator::defaultFeatherWidth(bounds), mesh);
				browser.shapePreviewVertices =
					static_cast<int>(mesh.positions.size());
				browser.shapePreviewTriangles =
					static_cast<int>(mesh.indices.size() / 3);
				if (!mesh.indices.empty() && render)
				{
					const int side = 256;
					std::vector<unsigned char> pixels(
						static_cast<std::size_t>(side) * side * 4, 0);
					Orkige::VectorShapeRaster::rasterize(mesh, side, side,
						pixels.data());
					const std::string uploadName = "__oshapeinspector";
					if (render->createTexture2D(uploadName, pixels.data(),
						side, side))
					{
						browser.shapePreviewUpload = uploadName;
					}
				}
			}
		}
	}

	if (browser.shapeShowSource)
	{
		// the raw text view (the "existing highlighted-text" toggle target -
		// a read-only monospaced dump of the .oshape source)
		ImGui::InputTextMultiline("##shapesource",
			browser.shapeSourceText.data(), browser.shapeSourceText.size() + 1,
			ImVec2(ImGui::GetContentRegionAvail().x,
				ImGui::GetTextLineHeight() * 12.0f),
			ImGuiInputTextFlags_ReadOnly);
	}
	else if (!browser.shapePreviewUpload.empty() && gImGuiRenderer)
	{
		const float side = std::min(ImGui::GetContentRegionAvail().x, 256.0f);
		ImGui::Image(gImGuiRenderer->textureIdForResource(
			browser.shapePreviewUpload), ImVec2(side, side));
	}
	else
	{
		ImGui::TextDisabled("(shape did not tessellate - see the source)");
	}
	ImGui::Text("%d vertices, %d triangles", browser.shapePreviewVertices,
		browser.shapePreviewTriangles);
	return true;
}

//! shared mouse-drag orbit on a just-drawn preview image: a left-drag over the
//! image rotates the mesh stage (cheap - per drag, not per frame)
void applyPreviewOrbitDrag(OrkigeEditor::MeshPreviewStage& meshPreview)
{
	if (ImGui::IsItemHovered() &&
		ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
		meshPreview.addOrbit(delta.x * 0.4f, -delta.y * 0.4f);
		ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
	}
}

//! the ".glb"/mesh Inspector section: a static-snapshot 3D preview from
//! MeshPreviewStage (a framed 3/4 view, orbit by dragging the image). Degrades
//! honestly to bounds + submesh-count text when the RTT is unavailable (a
//! backend without offscreen 3D targets, or a load failure). Returns true.
bool drawMeshInspectorSection(EditorState& state,
	OrkigeEditor::MeshPreviewStage& meshPreview, std::string const& relativePath)
{
	ImGui::TextUnformatted("Mesh");
	ImGui::TextDisabled("%s",
		std::filesystem::path(relativePath).filename().string().c_str());
	ImGui::Separator();
	// (re)stage the mesh on a selection change
	if (meshPreview.getLoadedFile() != relativePath)
	{
		std::string error;
		if (!meshPreview.load(state.project.getRootDirectory(), relativePath,
			error))
		{
			ImGui::TextDisabled("preview unavailable: %s", error.c_str());
		}
	}
	const OrkigeEditor::MeshPreviewInfo info = meshPreview.getInfo();
	Orkige::optr<Orkige::RenderTexture> target = meshPreview.getTarget();
	if (target && gImGuiRenderer)
	{
		const float side = std::min(ImGui::GetContentRegionAvail().x, 320.0f);
		ImGui::Image(gImGuiRenderer->textureIdFor(target), ImVec2(side, side));
		applyPreviewOrbitDrag(meshPreview);
		ImGui::SetItemTooltip("drag to orbit");
	}
	else if (info.loaded)
	{
		// honest degrade: no offscreen 3D target on this backend - show the
		// facts the stage still measured
		ImGui::TextDisabled("3D preview unavailable on this render backend");
	}
	if (info.loaded)
	{
		ImGui::Text("%d sub-mesh%s", info.subMeshCount,
			info.subMeshCount == 1 ? "" : "es");
		ImGui::TextDisabled("bounds %.2f x %.2f x %.2f  (radius %.2f)",
			info.sizeX, info.sizeY, info.sizeZ, info.boundingRadius);
	}
	return true;
}

//! a texture-reference field for the .omat editor: an InputText + a "pick"
//! popup listing the project's texture assets (reusing the AssetDatabase the
//! component ref-picker uses). Returns true when the reference changed.
bool drawMaterialTextureRef(EditorState& state, const char* label,
	Orkige::String& ref)
{
	bool changed = false;
	char buffer[256];
	SDL_strlcpy(buffer, ref.c_str(), sizeof(buffer));
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
	if (ImGui::InputText(label, buffer, sizeof(buffer)))
	{
		ref = buffer;
		changed = true;
	}
	ImGui::SameLine();
	ImGui::PushID(label);
	if (ImGui::Button("pick"))
	{
		ImGui::OpenPopup("##pickTex");
	}
	if (ImGui::BeginPopup("##pickTex"))
	{
		if (ImGui::Selectable("(none)"))
		{
			ref.clear();
			changed = true;
		}
		if (optr<Orkige::AssetDatabase> const& database =
			state.project.getAssetDatabase())
		{
			for (Orkige::AssetEntry const& asset : database->listAssets())
			{
				if (classifyAsset(asset.fileName) != AssetKind::Texture)
				{
					continue;
				}
				if (ImGui::Selectable(asset.fileName.c_str()))
				{
					ref = asset.fileName;
					changed = true;
				}
			}
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
	return changed;
}

//! build a live renderer material from the edited .omat and point BOTH the
//! preview mesh AND any scene ModelComponent at it: createMaterial is
//! create-or-update keyed by "Omat/<bare>" - the SAME name ModelComponent
//! builds - so editing updates every mesh using this material live
void refreshMaterialPreview(EditorState& state,
	OrkigeEditor::MeshPreviewStage& meshPreview)
{
	AssetBrowserState& browser = state.assetBrowser;
	Orkige::RenderSystem* render = Orkige::RenderSystem::get();
	if (!render)
	{
		return;
	}
	Orkige::RenderMaterialDesc desc;
	Orkige::MaterialAsset::ParsedMaterial const& m = browser.editMaterial;
	desc.albedo = Orkige::Color(m.albedo.r, m.albedo.g, m.albedo.b, m.albedo.a);
	desc.albedoTexture = m.albedoTexture;
	desc.metalness = m.metalness;
	desc.roughness = m.roughness;
	desc.normalTexture = m.normalTexture;
	desc.emissive = Orkige::Color(m.emissive.r, m.emissive.g, m.emissive.b, 1.0f);
	desc.emissiveTexture = m.emissiveTexture;
	const std::string materialName = "Omat/" + browser.editMaterialRef;
	render->createMaterial(materialName, desc);
	std::string error;
	meshPreview.setMaterial(materialName, error);
}

//! the ".omat" Inspector section: editable PBS fields (albedo/metalness/
//! roughness/normal/emissive) with Apply/Revert writing the file back, and a
//! LIVE 3D preview (the shared preview mesh with the edited material) that also
//! drives any scene mesh using the material. Returns true.
bool drawMaterialInspectorSection(EditorState& state,
	OrkigeEditor::MeshPreviewStage& meshPreview, std::string const& relativePath)
{
	AssetBrowserState& browser = state.assetBrowser;
	const std::string absolutePath =
		resolveProjectFilePath(state.project, relativePath);
	const std::string bareName =
		std::filesystem::path(relativePath).filename().string();

	ImGui::TextUnformatted("Material");
	ImGui::TextDisabled("%s", bareName.c_str());
	ImGui::Separator();

	// (re)read + parse on a selection change; stage the shared preview mesh
	if (browser.editMaterialPath != absolutePath)
	{
		browser.editMaterialPath = absolutePath;
		browser.editMaterialRef = bareName;
		browser.editMaterialStatus.clear();
		browser.editMaterial = Orkige::MaterialAsset::ParsedMaterial();
		std::ifstream in(absolutePath, std::ios::binary);
		std::stringstream buffer;
		buffer << in.rdbuf();
		browser.editMaterialOriginal = buffer.str();
		Orkige::String parseError;
		if (!Orkige::MaterialAsset::parse(browser.editMaterialOriginal,
			browser.editMaterial, &parseError))
		{
			browser.editMaterialStatus = "parse error: " + parseError;
		}
		std::string meshError;
		// the shared lit preview surface: a unit cube WITH normals + UV0 (the
		// engine's material-demo cube, registered by the editor) so HlmsPbs
		// accepts the datablock; a miss degrades to the swatch below
		meshPreview.loadNamedMesh("demo_material_cube.glb", "", meshError);
		refreshMaterialPreview(state, meshPreview);
	}

	// the live preview (or an albedo swatch degrade)
	Orkige::optr<Orkige::RenderTexture> target = meshPreview.getTarget();
	if (target && gImGuiRenderer)
	{
		const float side = std::min(ImGui::GetContentRegionAvail().x, 320.0f);
		ImGui::Image(gImGuiRenderer->textureIdFor(target), ImVec2(side, side));
		applyPreviewOrbitDrag(meshPreview);
		ImGui::SetItemTooltip("drag to orbit");
	}
	else
	{
		ImGui::TextDisabled("3D preview unavailable - showing albedo swatch");
		ImGui::ColorButton("##albedoswatch",
			ImVec4(browser.editMaterial.albedo.r, browser.editMaterial.albedo.g,
				browser.editMaterial.albedo.b, 1.0f),
			ImGuiColorEditFlags_NoTooltip, ImVec2(120.0f, 60.0f));
	}
	ImGui::Separator();

	// the editable PBS fields; any change re-cooks the live material
	bool edited = false;
	Orkige::MaterialAsset::ParsedMaterial& m = browser.editMaterial;
	float albedo[4] = { m.albedo.r, m.albedo.g, m.albedo.b, m.albedo.a };
	if (ImGui::ColorEdit4("Albedo", albedo))
	{
		m.albedo = Orkige::MaterialAsset::Colour(albedo[0], albedo[1],
			albedo[2], albedo[3]);
		edited = true;
	}
	edited |= drawMaterialTextureRef(state, "Albedo Map", m.albedoTexture);
	if (ImGui::SliderFloat("Metalness", &m.metalness, 0.0f, 1.0f))
	{
		edited = true;
	}
	if (ImGui::SliderFloat("Roughness", &m.roughness, 0.0f, 1.0f))
	{
		edited = true;
	}
	edited |= drawMaterialTextureRef(state, "Normal Map", m.normalTexture);
	float emissive[3] = { m.emissive.r, m.emissive.g, m.emissive.b };
	if (ImGui::ColorEdit3("Emissive", emissive))
	{
		m.emissive = Orkige::MaterialAsset::Colour(emissive[0], emissive[1],
			emissive[2], 1.0f);
		edited = true;
	}
	edited |= drawMaterialTextureRef(state, "Emissive Map", m.emissiveTexture);
	if (edited)
	{
		refreshMaterialPreview(state, meshPreview);
	}

	ImGui::Separator();
	const bool dirty =
		Orkige::MaterialAsset::serialize(m) != browser.editMaterialOriginal;
	ImGui::BeginDisabled(!dirty);
	if (ImGui::Button("Apply"))
	{
		// regenerate clean text and write it back (the .omat is a
		// generated-style asset - a rewrite regenerates, not preserves)
		const Orkige::String text = Orkige::MaterialAsset::serialize(m);
		std::ofstream out(absolutePath, std::ios::binary | std::ios::trunc);
		if (out)
		{
			out << text;
			out.close();
			browser.editMaterialOriginal = text;
			// re-run createMaterial so every scene mesh using this .omat picks
			// the change up live (create-or-update, the ModelComponent path)
			refreshMaterialPreview(state, meshPreview);
			browser.editMaterialStatus = "written to " + bareName;
		}
		else
		{
			browser.editMaterialStatus = "could not write " + bareName;
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!dirty);
	if (ImGui::Button("Revert"))
	{
		Orkige::MaterialAsset::parse(browser.editMaterialOriginal, m, nullptr);
		refreshMaterialPreview(state, meshPreview);
		browser.editMaterialStatus.clear();
	}
	ImGui::EndDisabled();
	if (!browser.editMaterialStatus.empty())
	{
		ImGui::TextDisabled("%s", browser.editMaterialStatus.c_str());
	}
	return true;
}

//! the ONE asset-inspector section shown when nothing in the scene is selected
//! but exactly one asset is selected in the browser: dispatch by asset kind to
//! the single matching section (texture import + preview / vector-shape preview
//! / mesh 3D preview / material editor). Exactly one section ever draws - other
//! kinds fall through to the caller's "nothing selected". Returns true when it
//! drew a section.

bool drawAssetInspectorSection(EditorState& state,
	OrkigeEditor::AnimationPreviewStage& animStage,
	OrkigeEditor::MeshPreviewStage& meshPreview)
{
	const std::string selected = singleSelectedAsset(state);
	if (selected.empty())
	{
		return false;
	}
	const AssetKind kind = classifyAsset(selected);
	// the mesh preview stage is shared by the mesh + material sections only;
	// any OTHER section frees it (its far-staged content renders every frame
	// while alive). Leaving the material section also clears its edit cache so
	// re-selecting the same .omat re-stages the preview mesh from scratch.
	if (kind != AssetKind::Mesh && kind != AssetKind::Material)
	{
		meshPreview.clear();
		state.assetBrowser.editMaterialPath.clear();
	}
	else if (kind == AssetKind::Mesh)
	{
		state.assetBrowser.editMaterialPath.clear();
	}
	// EXACTLY ONE section draws, by asset kind. The kinds the AssetDatabase
	// classifies get their editor/preview; everything else falls through to
	// the extension-driven sections (an animation rig, a text asset), else a
	// bare size line.
	switch (kind)
	{
	case AssetKind::Texture:
		return drawTextureImportSection(state);
	case AssetKind::VectorShape:
		return drawShapeInspectorSection(state, selected);
	case AssetKind::Mesh:
		return drawMeshInspectorSection(state, meshPreview, selected);
	case AssetKind::Material:
		return drawMaterialInspectorSection(state, meshPreview, selected);
	default:
		break;
	}
	// a .oui prepends its GUI-screen thumbnail + Open Preview button, then its
	// syntax-text view follows via drawTextPreviewSection (a no-op for others)
	drawGuiPreviewInspectorHeader(state);
	return drawAnimationPreviewSection(state, animStage) ||
		drawTextPreviewSection(state) ||
		drawUnknownAssetSection(state);
}

} // namespace

void drawInspectorPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, OrkigeEditor::AnimationPreviewStage& animStage,
	OrkigeEditor::MeshPreviewStage& meshPreview,
	bool* visible)
{
	const bool remote = session.isActive();
	if (ImGui::Begin(remote ? INSPECTOR_WINDOW_REMOTE : INSPECTOR_WINDOW_EDIT,
		visible))
	{
		// buttons in the Inspector (Add Component, Apply/Revert, pickers, ...)
		// take the darker header-bar shade so they stand off the panel body
		// (controlBg equals the panel here, so they'd otherwise vanish); hover
		// darkens, matching the component headers. Scoped to THIS panel so the
		// toolbar transport keeps its own control look.
		ImGui::PushStyleColor(ImGuiCol_Button,
			Orkige::editorComponentHeaderColor());
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
			Orkige::editorComponentHeaderHoverColor());
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,
			Orkige::editorComponentHeaderHoverColor());
		if (remote)
		{
			drawRemoteInspector(session);
			ImGui::PopStyleColor(3); // inspector button shade
			ImGui::End();
			return;
		}
		optr<Orkige::GameObject> gameObject;
		if (core.hasSelection())
		{
			gameObject = core.getGameObjectManager()
				.getGameObject(core.getSelectedObjectId()).lock();
		}
		if (!gameObject)
		{
			// with no scene object selected, a single asset selected in the
			// the Asset browser's selection shows its own view instead:
			// EXACTLY ONE section draws (texture import, vector shape, mesh
			// preview, material editor, animation preview, highlighted text,
			// else a bare size line)
			if (!drawAssetInspectorSection(state, animStage, meshPreview))
			{
				meshPreview.clear();	// nothing to preview: free the stage
				ImGui::TextDisabled("nothing selected");
			}
		}
		else
		{
			const std::string objectId = gameObject->getObjectID();
			// active checkbox in the header: it toggles the OWN
			// flag (activeSelf) - a checked box on an object annotated
			// "inactive via parent" means an ancestor is deactivated
			bool activeSelf = gameObject->isActiveSelf();
			if (Orkige::compactCheckbox("##activeSelf", &activeSelf))
			{
				core.setObjectActive(objectId, activeSelf);
			}
			ImGui::SetItemTooltip("Active");
			ImGui::SameLine();
			// the object name is an inline RENAME field (a recessed well), on the
			// SAME rename command path as the hierarchy's F2 - one undo step,
			// identical validation. The buffer resyncs when the selection changes
			// (and after a rename, when objectId becomes the new name); commit is
			// on Enter/defocus (IsItemDeactivatedAfterEdit), Escape cancels.
			{
				static std::string nameEditId;
				static char nameBuffer[256];
				if (nameEditId != objectId)
				{
					SDL_strlcpy(nameBuffer, objectId.c_str(), sizeof(nameBuffer));
					nameEditId = objectId;
				}
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
				ImGui::PushStyleColor(ImGuiCol_Border,
					Orkige::editorFieldBorderColor());
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::InputText("##objname", nameBuffer, sizeof(nameBuffer),
					ImGuiInputTextFlags_AutoSelectAll);
				ImGui::PopStyleColor();
				ImGui::PopStyleVar();
				if (ImGui::IsItemDeactivatedAfterEdit())
				{
					const Orkige::EditorCore::NameValidation validation =
						core.validateRename(objectId, nameBuffer);
					if (validation == Orkige::EditorCore::NameValidation::Ok)
					{
						core.renameObject(objectId, nameBuffer);
					}
					else if (validation !=
						Orkige::EditorCore::NameValidation::Unchanged)
					{
						// empty/duplicate rejected: restore the current name
						SDL_strlcpy(nameBuffer, objectId.c_str(),
							sizeof(nameBuffer));
					}
				}
			}
			if (!gameObject->isActiveInHierarchy())
			{
				ImGui::TextDisabled("%s", gameObject->isActiveSelf()
					? "(inactive via parent)" : "(inactive)");
			}
			if (core.getSelectionCount() > 1)
			{
				// multi-select groundwork: the Inspector edits the PRIMARY
				ImGui::TextDisabled("%zu selected - showing primary",
					core.getSelectionCount());
			}
			ImGui::TextDisabled("type: %s",
				gameObject->getTypeInfo().getName().c_str());
			// tags: comma-separated multi-tag labels (the manager tag index /
			// world.findByTag). Applied on Enter as one undoable SetTagsCommand;
			// the buffer rebuilds when the selection or the cleaned tag set
			// changes.
			{
				Orkige::StringVector currentTags;
				core.getObjectTags(objectId, currentTags);
				std::string joined;
				for (Orkige::String const& tag : currentTags)
				{
					if (!joined.empty())
					{
						joined += ", ";
					}
					joined += tag;
				}
				if (state.tagsEditObjectId != objectId ||
					state.tagsEditCurrent != joined)
				{
					state.tagsEditObjectId = objectId;
					state.tagsEditCurrent = joined;
					SDL_strlcpy(state.tagsEditBuffer, joined.c_str(),
						sizeof(state.tagsEditBuffer));
				}
				if (ImGui::InputTextWithHint("Tags", "tag1, tag2, ...",
					state.tagsEditBuffer, sizeof(state.tagsEditBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue))
				{
					Orkige::StringVector newTags;
					const std::string buffer = state.tagsEditBuffer;
					std::size_t pos = 0;
					while (pos <= buffer.size())
					{
						const std::size_t comma = buffer.find(',', pos);
						const std::string token = buffer.substr(pos,
							comma == std::string::npos ? std::string::npos
								: comma - pos);
						const std::size_t begin = token.find_first_not_of(" \t");
						const std::size_t end = token.find_last_not_of(" \t");
						if (begin != std::string::npos)
						{
							newTags.push_back(
								token.substr(begin, end - begin + 1));
						}
						if (comma == std::string::npos)
						{
							break;
						}
						pos = comma + 1;
					}
					core.setObjectTags(objectId, newTags);
					state.tagsEditCurrent.clear();	// force a rebuild from the cleaned set
				}
				ImGui::SetItemTooltip("comma-separated tags "
					"(world.findByTag); Enter to apply");
			}
			ImGui::Separator();
			// component header bars: a slightly distinct surface from the panel
			// body so each component reads as a titled bar (a grey disclosure
			// group, not the accent list-selection blue)
			ImGui::PushStyleColor(ImGuiCol_Header,
				Orkige::editorComponentHeaderColor());
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
				Orkige::editorComponentHeaderHoverColor());
			ImGui::PushStyleColor(ImGuiCol_HeaderActive,
				Orkige::editorComponentHeaderHoverColor());
			// iterate a snapshot of the attached types: the remove button
			// mutates the component map mid-loop
			std::vector<Orkige::TypeInfo> componentTypes;
			for (auto const& [componentType, component] :
				gameObject->getComponents())
			{
				componentTypes.push_back(componentType);
			}
			for (Orkige::TypeInfo const& componentType : componentTypes)
			{
				const std::string typeName = componentType.getName();
				ImGui::PushID(typeName.c_str());
				// fetch the schema ONCE: the header hosts an enable toggle when
				// the component exposes one, and the body renders the rest
				const Orkige::PropertySchema schema =
					core.getComponentPropertySchema(objectId, typeName);
				const std::string enableProp = componentEnableProperty(schema);
				// remember where the header's line ends so the small remove
				// button (and the enable toggle) can overlap its right edge
				const float headerRight = ImGui::GetCursorPosX() +
					ImGui::GetContentRegionAvail().x;
				// clean display title (no "Component" suffix); the "###" keeps
				// the header ID = raw type name, so its persisted collapse state
				// is stable regardless of the display text
				const std::string headerLabel =
					Orkige::prettifyComponentTitle(typeName) + "###" + typeName;
				const bool headerOpen = ImGui::CollapsingHeader(
					headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
					ImGuiTreeNodeFlags_AllowOverlap);
				ImGui::SetItemTooltip("%s", typeName.c_str()); // raw name, honest
				// remove affordances: a small x on the header + context menu.
				// Removal is blocked while another attached component depends
				// on this one (honest check against the addDependency info).
				std::string blockedBy;
				const bool removable = core.canRemoveComponent(objectId,
					typeName, &blockedBy);
				if (ImGui::BeginPopupContextItem("##componentmenu"))
				{
					// Copy this component's reflected values, or paste the
					// clipboard component onto THIS object (one undo step -
					// add + set when the object lacks the kind)
					if (ImGui::MenuItem("Copy Component"))
					{
						core.copyComponent(objectId, typeName);
					}
					const bool canPaste = core.hasComponentClipboard();
					std::string pasteLabel = canPaste
						? ("Paste " + core.getClipboardComponentTypeName())
						: std::string("Paste Component");
					ImGui::BeginDisabled(!canPaste);
					if (ImGui::MenuItem(pasteLabel.c_str()))
					{
						core.pasteComponent(objectId);
					}
					ImGui::EndDisabled();
					ImGui::Separator();
					ImGui::BeginDisabled(!removable);
					if (ImGui::MenuItem("Remove Component"))
					{
						core.removeComponentFromObject(objectId, typeName);
					}
					ImGui::EndDisabled();
					if (!removable && !blockedBy.empty())
					{
						ImGui::TextDisabled("required by %s",
							blockedBy.c_str());
					}
					ImGui::EndPopup();
				}
				bool removedNow = false;
				const float removeButtonWidth = ImGui::GetFrameHeight();
				ImGui::SameLine(headerRight - removeButtonWidth);
				ImGui::BeginDisabled(!removable);
				if (ImGui::SmallButton("x"))
				{
					removedNow =
						core.removeComponentFromObject(objectId, typeName);
				}
				ImGui::EndDisabled();
				if (!removable && !blockedBy.empty())
				{
					ImGui::SetItemTooltip("required by %s", blockedBy.c_str());
				}
				else
				{
					ImGui::SetItemTooltip("Remove Component");
				}
				// the enable toggle (where the component exposes one): a small
				// recessed checkbox on the header, left of the remove control,
				// bound to the reflected "enabled" property through the same
				// undoable edit path as the body fields
				if (!enableProp.empty())
				{
					std::string enabledValue;
					if (core.getObjectProperty(objectId, typeName, enableProp,
						enabledValue))
					{
						bool enabled = (enabledValue == "1");
						const float boxWidth = ImGui::GetFrameHeight();
						ImGui::SameLine(headerRight - removeButtonWidth -
							ImGui::GetStyle().ItemInnerSpacing.x - boxWidth);
						if (Orkige::compactCheckbox("##enabled", &enabled))
						{
							core.applyPropertyChange(objectId, typeName,
								enableProp, enabledValue, enabled ? "1" : "0",
								core.beginMergeSession());
						}
						ImGui::SetItemTooltip("Enabled");
					}
				}
				if (removedNow || !gameObject->hasComponent(componentType))
				{
					ImGui::PopID();
					continue;
				}
				if (!headerOpen)
				{
					ImGui::PopID();
					continue;
				}
				// AUTO Inspector: render this component's editable properties
				// off its reflection schema (label left / value right), skipping
				// the enable toggle the header already hosts - no per-component
				// code, no dynamic_cast dispatch
				drawComponentProperties(state, core, objectId, typeName, schema,
					enableProp);
				ImGui::PopID();
			}
			ImGui::PopStyleColor(3); // component header bars
			ImGui::Separator();
			drawAddComponentButton(state, core, gameObject);
		}
		ImGui::PopStyleColor(3); // inspector button shade
	}
	ImGui::End();
}
