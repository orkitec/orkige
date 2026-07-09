// EditorInspectorPanel.cpp - the Inspector panel: the per-component editors
// (Transform/Model/Script/RigidBody/Camera/Sprite), the Add Component popup
// and the remote play-mode object_state view.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "EditorPropertyWidgets.h"

#include <core_base/PropertySchema.h>
#include <core_base/TypeManager.h>
#include <core_game/GameObjectManager.h>
#include <core_project/AssetDatabase.h>
#include <core_util/StringUtil.h>

#include <algorithm>
#include <cstring>

namespace
{

//! @brief does an asset file name belong to the asset-kind a reference property
//! hints at? The AssetDatabase enumerates files, not kinds, so we match on the
//! extension set the engine's resource pipeline associates with each kind
//! (texture/mesh/sound/script/prefab). An unknown/empty hint matches every
//! asset (a permissive picker beats an empty one).
bool assetMatchesKind(std::string const& fileName, std::string const& kind)
{
	const std::string::size_type dot = fileName.find_last_of('.');
	if (dot == std::string::npos)
	{
		return kind.empty();
	}
	std::string ext = fileName.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (kind == "texture")
	{
		return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
			ext == "tga" || ext == "dds";
	}
	if (kind == "mesh")
	{
		return ext == "mesh" || ext == "gltf" || ext == "glb" || ext == "obj";
	}
	if (kind == "sound")
	{
		return ext == "wav" || ext == "ogg" || ext == "mp3" || ext == "flac";
	}
	if (kind == "script")
	{
		return ext == "lua";
	}
	if (kind == "prefab")
	{
		return ext == "oprefab";
	}
	return true; // an unknown hint: offer everything rather than nothing
}

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
void drawComponentProperties(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId, Orkige::TypeInfo const& componentType)
{
	const std::string componentName = componentType.getName();
	// the union schema (static per-type + dynamic per-instance) so a
	// ScriptComponent's exported script properties render in the Inspector too
	// - discovered per instance since a script's exports are
	// known only once a specific .lua is attached
	const Orkige::PropertySchema schema =
		core.getComponentPropertySchema(objectId, componentName);
	if (schema.empty())
	{
		ImGui::TextDisabled("(no editable properties yet)");
		return;
	}
	bool any = false;
	for (Orkige::PropertyDesc const& desc : schema.properties())
	{
		// edit-mode filtering: never show a hidden property or transient
		// runtime telemetry (velocities, has_body, script started/error);
		// a property with no getter has nothing to read/display
		if (desc.hasFlag(Orkige::PROP_HIDDEN) ||
			desc.hasFlag(Orkige::PROP_TRANSIENT) || !desc.get)
		{
			continue;
		}
		std::string value;
		if (!core.getObjectProperty(objectId, componentName, desc.name, value))
		{
			continue;
		}
		PropertyWidgetDesc widget;
		widget.label = desc.name;
		widget.kind = desc.kind;
		widget.hint = propertyWidgetHint(desc);
		widget.readOnly = desc.isReadOnly();
		std::string edited;
		// the reference-picker provider (AssetRef/ObjectRef): backed by the
		// project's AssetDatabase and the scene's object ids. Scalar/math
		// widgets ignore it; the remote inspector passes none (text field).
		PropertyRefProvider refProvider =
			[&state, &core](PropertyWidgetDesc const& w)
			{
				return collectReferenceOptions(state, core, w);
			};
		const bool committed =
			drawPropertyWidget(widget, value, edited, refProvider);
		// bracket a drag/interaction into ONE undo step: a fresh merge session
		// opens when the widget becomes active (drag start / combo open / click)
		// and every edited frame of that interaction shares it (mergeWith
		// collapses them); the next interaction opens a new session
		if (ImGui::IsItemActivated())
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
		any = true;
	}
	if (!any)
	{
		ImGui::TextDisabled("(no editable properties yet)");
	}
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
	const float buttonWidth = 180.0f;
	const float availableWidth = ImGui::GetContentRegionAvail().x;
	if (availableWidth > buttonWidth)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
			(availableWidth - buttonWidth) * 0.5f);
	}
	if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0.0f)))
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
					SDL_Log("orkige_editor: adding %s to '%s' failed",
						typeName.c_str(),
						gameObject->getObjectID().c_str());
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
	desc.label = property;
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
			if (ImGui::Checkbox("##remoteActive", &activeSelf))
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
	// neutral component headers, same reasoning as the edit-mode inspector
	ImGui::PushStyleColor(ImGuiCol_Header,
		ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
		ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive,
		ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
	for (std::string const& component : session.stateComponents)
	{
		if (!ImGui::CollapsingHeader(component.c_str(),
			ImGuiTreeNodeFlags_DefaultOpen))
		{
			continue;
		}
		ImGui::PushID(component.c_str());
		const std::string prefix = component + ".";
		bool any = false;
		if (!session.statePropKeys.empty())
		{
			// typed widgets, in the schema's declaration order
			for (std::string const& key : session.statePropKeys)
			{
				if (key.rfind(prefix, 0) != 0)
				{
					continue;
				}
				drawRemoteProperty(session, key, component,
					key.substr(prefix.size()));
				any = true;
			}
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
	ImGui::PopStyleColor(3); // neutral component headers
}

} // namespace

void drawInspectorPanel(EditorState& state, PlaySession& session,
	Orkige::EditorCore& core, bool* visible)
{
	const bool remote = session.isActive();
	if (ImGui::Begin(remote ? INSPECTOR_WINDOW_REMOTE : INSPECTOR_WINDOW_EDIT,
		visible))
	{
		if (remote)
		{
			drawRemoteInspector(session);
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
			ImGui::TextDisabled("nothing selected");
		}
		else
		{
			const std::string objectId = gameObject->getObjectID();
			// active checkbox in the header: it toggles the OWN
			// flag (activeSelf) - a checked box on an object annotated
			// "inactive via parent" means an ancestor is deactivated
			bool activeSelf = gameObject->isActiveSelf();
			if (ImGui::Checkbox("##activeSelf", &activeSelf))
			{
				core.setObjectActive(objectId, activeSelf);
			}
			ImGui::SetItemTooltip("Active");
			ImGui::SameLine();
			ImGui::Text("%s", objectId.c_str());
			if (!gameObject->isActiveInHierarchy())
			{
				ImGui::SameLine();
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
			// the theme's accent Header colour is for list selection; the
			// component CollapsingHeaders read better neutral (macOS
			// disclosure groups are grey, not blue)
			ImGui::PushStyleColor(ImGuiCol_Header,
				ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
				ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive,
				ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
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
				// remember where the header's line ends so the small remove
				// button can overlap its right edge
				const float headerRight = ImGui::GetCursorPosX() +
					ImGui::GetContentRegionAvail().x;
				const bool headerOpen = ImGui::CollapsingHeader(
					typeName.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
					ImGuiTreeNodeFlags_AllowOverlap);
				// remove affordances: a small x on the header + context menu.
				// Removal is blocked while another attached component depends
				// on this one (honest check against the addDependency info).
				std::string blockedBy;
				const bool removable = core.canRemoveComponent(objectId,
					typeName, &blockedBy);
				if (ImGui::BeginPopupContextItem("##componentmenu"))
				{
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
				// off its reflection schema - no per-component
				// code, no dynamic_cast dispatch
				drawComponentProperties(state, core, objectId, componentType);
				ImGui::PopID();
			}
			ImGui::PopStyleColor(3); // neutral component headers
			ImGui::Separator();
			drawAddComponentButton(state, core, gameObject);
		}
	}
	ImGui::End();
}
