// EditorInspectorPanel.cpp - the Inspector panel: the per-component editors
// (Transform/Model/Script/RigidBody/Camera/Sprite), the Add Component popup
// and the remote play-mode object_state view.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "EditorPropertyWidgets.h"

#include <core_util/StringUtil.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_gocomponent/ScriptComponent.h>
#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/TransformComponent.h>

#include <cstring>

namespace
{

// Inspector transform editors: every edit goes through the command stack
// (undoable); while a drag widget stays active the per-frame edits merge
// into ONE undo step, exactly like a gizmo drag.
void drawTransformComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId)
{
	Orkige::EditorTransform before;
	if (!core.getObjectTransform(objectId, before))
	{
		return;
	}
	Orkige::EditorTransform after = before;
	bool edited = false;

	float position[3] = { before.position.x, before.position.y,
		before.position.z };
	bool changed = ImGui::DragFloat3("Position", position, 0.05f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.position = Orkige::Vec3(position[0], position[1], position[2]);
		edited = true;
	}

	float yawPitchRoll[3] = {
		before.orientation.getYaw().valueDegrees(),
		before.orientation.getPitch().valueDegrees(),
		before.orientation.getRoll().valueDegrees(),
	};
	changed = ImGui::DragFloat3("Yaw/Pitch/Roll", yawPitchRoll, 0.5f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		Orkige::Mat3 rotation;
		rotation.FromEulerAnglesYXZ(
			Orkige::Degree(yawPitchRoll[0]),
			Orkige::Degree(yawPitchRoll[1]),
			Orkige::Degree(yawPitchRoll[2]));
		after.orientation = Orkige::Quat(rotation);
		edited = true;
	}

	float scale[3] = { before.scale.x, before.scale.y, before.scale.z };
	changed = ImGui::DragFloat3("Scale", scale, 0.02f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.scale = Orkige::Vec3(scale[0], scale[1], scale[2]);
		edited = true;
	}

	if (edited)
	{
		core.applyTransformChange(objectId, before, after,
			state.inspectorMergeSession);
	}
}

// ModelComponent editor: the mesh name is an editable text field; Enter
// applies through EditorCore::changeObjectMesh (undoable, reloads the
// entity). A failed load logs to the Console and keeps the old mesh.
void drawModelComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId, Orkige::ModelComponent* model)
{
	const std::string currentMesh = model->getCurrentModelFileName();
	// rebuild the edit buffer when the selection or the mesh changed behind
	// the field (undo/redo, another panel)
	if (state.meshEditObjectId != objectId ||
		state.meshEditCurrentMesh != currentMesh)
	{
		state.meshEditObjectId = objectId;
		state.meshEditCurrentMesh = currentMesh;
		SDL_strlcpy(state.meshEditBuffer, currentMesh.c_str(),
			sizeof(state.meshEditBuffer));
	}
	if (ImGui::InputText("Mesh", state.meshEditBuffer,
		sizeof(state.meshEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		if (!core.changeObjectMesh(objectId, state.meshEditBuffer))
		{
			SDL_Log("orkige_editor: mesh change to '%s' refused/failed",
				state.meshEditBuffer);
			// snap the field back to reality
			SDL_strlcpy(state.meshEditBuffer, currentMesh.c_str(),
				sizeof(state.meshEditBuffer));
		}
		// on success the next frame re-syncs via meshEditCurrentMesh
	}
	ImGui::SetItemTooltip("mesh resource name (Enter reloads the entity)");
}

// ScriptComponent editor: project-relative script path (Enter applies) +
// enabled checkbox - both undoable through ONE ChangeScriptCommand - plus a
// "(script error)" indicator fed from the component. In the editor scripts
// never run (edit mode does not tick components), so errors show up here
// only for state carried in from elsewhere; the live play-mode error state
// arrives through the remote Inspector (ScriptComponent.error).
void drawScriptComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId, Orkige::ScriptComponent* script)
{
	const std::string currentScript = script->getScriptFile();
	// rebuild the edit buffer when the selection or the path changed behind
	// the field (undo/redo, another panel)
	if (state.scriptEditObjectId != objectId ||
		state.scriptEditCurrentScript != currentScript)
	{
		state.scriptEditObjectId = objectId;
		state.scriptEditCurrentScript = currentScript;
		SDL_strlcpy(state.scriptEditBuffer, currentScript.c_str(),
			sizeof(state.scriptEditBuffer));
	}
	if (ImGui::InputText("Script", state.scriptEditBuffer,
		sizeof(state.scriptEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		if (!core.changeObjectScript(objectId, state.scriptEditBuffer,
			script->isScriptEnabled()))
		{
			// refused = no-op (same path); snap the field back to reality
			SDL_strlcpy(state.scriptEditBuffer, currentScript.c_str(),
				sizeof(state.scriptEditBuffer));
		}
		// on success the next frame re-syncs via scriptEditCurrentScript
	}
	ImGui::SetItemTooltip("project-relative Lua script path, e.g. "
		"scripts/player.lua (Enter applies; runs in Play mode only)");
	bool enabled = script->isScriptEnabled();
	if (ImGui::Checkbox("Enabled", &enabled))
	{
		core.changeObjectScript(objectId, currentScript, enabled);
	}
	ImGui::SetItemTooltip("disabled scripts load but never update");
	if (script->hasScriptError())
	{
		ImGui::TextColored(ImVec4(0.94f, 0.35f, 0.35f, 1.0f),
			"(script error)");
		ImGui::SetItemTooltip("%s", script->getScriptError().c_str());
	}
}

// RigidBodyComponent editor: creation parameters (BodyDesc) only - every
// edit is one undoable RigidBodyChangeCommand; drag widgets merge their
// per-frame edits into ONE undo step exactly like the transform drags.
void drawRigidBodyComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId)
{
	Orkige::PhysicsWorld::BodyDesc before;
	if (!core.getRigidBodyDesc(objectId, before))
	{
		return;
	}
	Orkige::PhysicsWorld::BodyDesc after = before;
	bool edited = false;

	static const char* const bodyTypeNames[] =
		{ "Static", "Kinematic", "Dynamic" };
	int bodyType = static_cast<int>(before.bodyType);
	if (ImGui::Combo("Body Type", &bodyType, bodyTypeNames, 3))
	{
		after.bodyType =
			static_cast<Orkige::PhysicsWorld::BodyType>(bodyType);
		// click widgets get a fresh session - they never merge
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}

	static const char* const shapeTypeNames[] =
		{ "Box", "Sphere", "Capsule" };
	int shapeType = static_cast<int>(before.shapeType);
	if (ImGui::Combo("Shape", &shapeType, shapeTypeNames, 3))
	{
		after.shapeType =
			static_cast<Orkige::PhysicsWorld::ShapeType>(shapeType);
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}

	// shape dimensions (which ones apply follows the CURRENT shape)
	if (before.shapeType == Orkige::PhysicsWorld::ST_BOX)
	{
		float halfExtents[3] = { before.halfExtents.x, before.halfExtents.y,
			before.halfExtents.z };
		const bool changed =
			ImGui::DragFloat3("Half Extents", halfExtents, 0.05f, 0.01f,
				1000.0f);
		if (ImGui::IsItemActivated())
		{
			state.inspectorMergeSession = core.beginMergeSession();
		}
		if (changed)
		{
			after.halfExtents = Orkige::Vec3(halfExtents[0], halfExtents[1],
				halfExtents[2]);
			edited = true;
		}
	}
	else
	{
		float radius = before.radius;
		bool changed = ImGui::DragFloat("Radius", &radius, 0.05f, 0.01f,
			1000.0f);
		if (ImGui::IsItemActivated())
		{
			state.inspectorMergeSession = core.beginMergeSession();
		}
		if (changed)
		{
			after.radius = radius;
			edited = true;
		}
		if (before.shapeType == Orkige::PhysicsWorld::ST_CAPSULE)
		{
			float halfHeight = before.halfHeight;
			changed = ImGui::DragFloat("Half Height", &halfHeight, 0.05f,
				0.01f, 1000.0f);
			if (ImGui::IsItemActivated())
			{
				state.inspectorMergeSession = core.beginMergeSession();
			}
			if (changed)
			{
				after.halfHeight = halfHeight;
				edited = true;
			}
		}
	}

	float mass = before.mass;
	bool changed = ImGui::DragFloat("Mass", &mass, 0.1f, 0.0f, 100000.0f,
		"%.2f kg");
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.mass = mass;
		edited = true;
	}

	float friction = before.friction;
	changed = ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, 5.0f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.friction = friction;
		edited = true;
	}

	float restitution = before.restitution;
	changed = ImGui::DragFloat("Restitution", &restitution, 0.01f, 0.0f, 1.0f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.restitution = restitution;
		edited = true;
	}

	bool planar = before.planar;
	if (ImGui::Checkbox("Planar (2D: X/Y plane)", &planar))
	{
		after.planar = planar;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}

	// sensor / trigger volume (WP #88): detects overlaps (fires contact events
	// + the Lua onContactBegin/onContactEnd hooks) with NO collision response,
	// composing with the layer above (only detects what its layer collides with)
	bool isSensor = before.isSensor;
	if (ImGui::Checkbox("Sensor (trigger - no collision response)", &isSensor))
	{
		after.isSensor = isSensor;
		edited = true;
	}
	ImGui::SetItemTooltip("a trigger volume: it detects overlaps (firing "
		"ContactBegan/EndedEvent and the onContactBegin/onContactEnd script "
		"hooks) but never pushes bodies apart");

	// collision layer: a dropdown over the project's physics.olayers names
	// (a single "Default" without an asset). An unknown current name (a
	// hand-edited scene, or a project whose asset dropped the layer) shows
	// as a first entry so it is never silently lost.
	{
		const Orkige::String currentLayer =
			before.layer.empty() ? Orkige::String("Default") : before.layer;
		std::vector<Orkige::String> names = core.getPhysicsLayerNames();
		if (std::find(names.begin(), names.end(), currentLayer) == names.end())
		{
			names.insert(names.begin(), currentLayer);
		}
		if (ImGui::BeginCombo("Layer", currentLayer.c_str()))
		{
			for (Orkige::String const& name : names)
			{
				const bool selected = (name == currentLayer);
				if (ImGui::Selectable(name.c_str(), selected) &&
					name != currentLayer)
				{
					after.layer = name;
					state.inspectorMergeSession = core.beginMergeSession();
					edited = true;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("collision layer (physics.olayers) - the NxN "
			"matrix decides which layers collide");
	}

	if (edited)
	{
		core.applyRigidBodyChange(objectId, before, after,
			state.inspectorMergeSession);
	}
}

// CameraComponent editor: projection mode combo + ortho size drag - one
// undoable CameraChangeCommand per edit, drags merge like the transforms
void drawCameraComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId)
{
	Orkige::EditorCameraSettings before;
	if (!core.getCameraSettings(objectId, before))
	{
		return;
	}
	Orkige::EditorCameraSettings after = before;
	bool edited = false;

	static const char* const projectionNames[] =
		{ "Perspective", "Orthographic" };
	int projection = before.projectionMode;
	if (ImGui::Combo("Projection", &projection, projectionNames, 2))
	{
		after.projectionMode = projection;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}
	ImGui::SetItemTooltip("orthographic = 2D projection (applies to the "
		"engine camera in Play mode; the editor viewport stays perspective)");

	if (before.projectionMode ==
		static_cast<int>(Orkige::CameraComponent::PM_ORTHOGRAPHIC))
	{
		float orthoSize = before.orthoSize;
		const bool changed = ImGui::DragFloat("Ortho Size", &orthoSize,
			0.1f, 0.01f, 10000.0f, "%.2f wu");
		if (ImGui::IsItemActivated())
		{
			state.inspectorMergeSession = core.beginMergeSession();
		}
		if (changed)
		{
			after.orthoSize = orthoSize;
			edited = true;
		}
		ImGui::SetItemTooltip("world units from the view center to the top "
			"edge (the camera sees 2x this height)");
	}

	if (edited)
	{
		core.applyCameraChange(objectId, before, after,
			state.inspectorMergeSession);
	}
}

// SpriteComponent editor: texture name (Enter applies), size/tint/flip/
// z-order/visibility - one undoable SpriteChangeCommand per edit
void drawSpriteComponentUI(EditorState& state, Orkige::EditorCore& core,
	std::string const& objectId)
{
	Orkige::EditorSpriteSettings before;
	if (!core.getSpriteSettings(objectId, before))
	{
		return;
	}
	Orkige::EditorSpriteSettings after = before;
	bool edited = false;

	// texture field: rebuild the buffer when the selection or the texture
	// changed behind the field (undo/redo, another panel)
	if (state.spriteEditObjectId != objectId ||
		state.spriteEditCurrentTexture != before.textureName)
	{
		state.spriteEditObjectId = objectId;
		state.spriteEditCurrentTexture = before.textureName;
		SDL_strlcpy(state.spriteEditBuffer, before.textureName.c_str(),
			sizeof(state.spriteEditBuffer));
	}
	if (ImGui::InputText("Texture", state.spriteEditBuffer,
		sizeof(state.spriteEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		after.textureName = state.spriteEditBuffer;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}
	ImGui::SetItemTooltip("texture resource name, e.g. ball.png from the "
		"project's assets/ (Enter reloads the sprite)");

	float size[2] = { before.width, before.height };
	bool changed = ImGui::DragFloat2("Size", size, 0.05f, 0.0f, 10000.0f);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.width = size[0];
		after.height = size[1];
		edited = true;
	}
	ImGui::SetItemTooltip("world units; 0 derives the dimension from the "
		"texture aspect ratio");

	float tint[4] = { before.tint[0], before.tint[1], before.tint[2],
		before.tint[3] };
	changed = ImGui::ColorEdit4("Tint", tint);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		for (int each = 0; each < 4; ++each)
		{
			after.tint[each] = tint[each];
		}
		edited = true;
	}

	bool flipX = before.flipX;
	if (ImGui::Checkbox("Flip X", &flipX))
	{
		after.flipX = flipX;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}
	ImGui::SameLine();
	bool flipY = before.flipY;
	if (ImGui::Checkbox("Flip Y", &flipY))
	{
		after.flipY = flipY;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}

	int zOrder = before.zOrder;
	changed = ImGui::DragInt("Z-Order", &zOrder, 0.1f,
		Orkige::SpriteComponent::ZORDER_MIN,
		Orkige::SpriteComponent::ZORDER_MAX);
	if (ImGui::IsItemActivated())
	{
		state.inspectorMergeSession = core.beginMergeSession();
	}
	if (changed)
	{
		after.zOrder = zOrder;
		edited = true;
	}
	ImGui::SetItemTooltip("higher renders on top; overlapping sprites should "
		"use distinct values (alpha sorting within one value is by camera "
		"distance)");

	bool visible = before.visible;
	if (ImGui::Checkbox("Visible", &visible))
	{
		after.visible = visible;
		state.inspectorMergeSession = core.beginMergeSession();
		edited = true;
	}

	if (edited)
	{
		core.applySpriteChange(objectId, before, after,
			state.inspectorMergeSession);
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
// (drawPropertyWidget, task #94 P3) and send set_property when the user edits
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
// remote object, rendered GENERICALLY off the reflection metadata (task #94
// P3). Every reflected property gets a typed widget by its PropertyKind
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
			// Unity-style active checkbox in the header: it toggles the OWN
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
				Orkige::GameObjectComponent* component =
					gameObject->getComponentPtr(componentType);
				if (dynamic_cast<Orkige::TransformComponent*>(component))
				{
					drawTransformComponentUI(state, core, objectId);
				}
				else if (auto* model =
					dynamic_cast<Orkige::ModelComponent*>(component))
				{
					drawModelComponentUI(state, core, objectId, model);
				}
				else if (dynamic_cast<Orkige::RigidBodyComponent*>(component))
				{
					drawRigidBodyComponentUI(state, core, objectId);
				}
				else if (dynamic_cast<Orkige::SpriteComponent*>(component))
				{
					drawSpriteComponentUI(state, core, objectId);
				}
				else if (dynamic_cast<Orkige::CameraComponent*>(component))
				{
					drawCameraComponentUI(state, core, objectId);
				}
				else if (auto* script =
					dynamic_cast<Orkige::ScriptComponent*>(component))
				{
					drawScriptComponentUI(state, core, objectId, script);
				}
				else
				{
					ImGui::TextDisabled("(no editable properties yet)");
				}
				ImGui::PopID();
			}
			ImGui::PopStyleColor(3); // neutral component headers
			ImGui::Separator();
			drawAddComponentButton(state, core, gameObject);
		}
	}
	ImGui::End();
}
