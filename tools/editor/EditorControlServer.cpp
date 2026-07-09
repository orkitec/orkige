// EditorControlServer.cpp - the MCP control-port command handler (WP #80).
// A thin adapter over EditorCore + the EditorDocument free functions + the two
// screenshot facade calls; see EditorControlServer.h for the design. Every
// verb maps onto an existing editor operation (the command->function table in
// the WP #80 API spec) - the only genuinely new engine work behind it is
// AssetDatabase::listAssets() (a clean enumeration accessor over the private
// lookup maps).
#include "EditorControlServer.h"
#include "EditorApp.h"

#include <core_game/GameObject.h>
#include <core_game/GameObjectManager.h>
#include <core_project/AssetDatabase.h>
#include <core_project/Project.h>
#include <core_util/optr.h>

#include <engine_gocomponent/ModelComponent.h>
#include <engine_render/RenderSystem.h>
#include <engine_render/RenderTexture.h>
#include <engine_render/RenderWorld.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>

namespace Orkige
{
	const String EditorControlServer::MSG_OK = "ok";
	const String EditorControlServer::MSG_ERR = "err";

	namespace
	{
		//! space-separated float bundle (the set_property wire convention the
		//! player already speaks - reuses formatPlayFloats from EditorApp.h)
		String floats(const float* values, int count)
		{
			return formatPlayFloats(values, count);
		}
		//! read an optional space-separated float bundle; false when the field
		//! is absent or does not parse to exactly count floats (out untouched)
		bool readFloats(DebugMessage const& message, String const& key,
			float* out, int count)
		{
			if (!message.has(key))
			{
				return false;
			}
			return parsePlayFloats(message.get(key), out, count);
		}
		//! is this verb a pure read (allowed before authentication)?
		bool isReadVerb(String const& type)
		{
			return type == "hello" || type == "ping" || type == "get_state" ||
				type == "list_hierarchy" || type == "get_object" ||
				type == "get_component" || type == "list_assets" ||
				type == "console_tail";
		}
	}
	//---------------------------------------------------------
	EditorControlServer::EditorControlServer()
	{
	}
	//---------------------------------------------------------
	EditorControlServer::~EditorControlServer()
	{
		this->stop();
	}
	//---------------------------------------------------------
	bool EditorControlServer::start(unsigned short port,
		std::string const& tokenFilePath)
	{
		if (!this->mServer.start(port))
		{
			return false;
		}
		// auth policy: a token is only meaningful when we can PUBLISH it for
		// the host to read. With a token-file path we mint a fresh secret (the
		// same 128-bit hex generator the asset database uses) and enforce it on
		// mutations; without one, auth is off (a hand-started dev port - a
		// loopback reader is harmless, and there is no secret to present).
		this->mTokenFilePath = tokenFilePath;
		if (!tokenFilePath.empty())
		{
			this->mToken = AssetDatabase::generateId();
			std::error_code ignored;
			std::filesystem::create_directories(
				std::filesystem::path(tokenFilePath).parent_path(), ignored);
			FILE* file = std::fopen(tokenFilePath.c_str(), "wb");
			if (!file)
			{
				this->mServer.stop();
				return false;
			}
			// the port too, so a host that started the editor on an ephemeral
			// port (0) can discover it: "<port>\n<token>\n"
			std::fprintf(file, "%u\n%s\n",
				static_cast<unsigned>(this->mServer.getPort()),
				this->mToken.c_str());
			std::fclose(file);
		}
		return true;
	}
	//---------------------------------------------------------
	void EditorControlServer::stop()
	{
		this->mServer.stop();
		if (!this->mTokenFilePath.empty())
		{
			std::error_code ignored;
			std::filesystem::remove(this->mTokenFilePath, ignored);
			this->mTokenFilePath.clear();
		}
		this->mToken.clear();
		this->mAuthenticated = false;
	}
	//---------------------------------------------------------
	void EditorControlServer::update(EditorControlContext const& context)
	{
		if (!this->mServer.isListening())
		{
			return;
		}
		this->mServer.update();
		// a fresh client starts unauthenticated (a dropped+reconnected host
		// must hello again)
		if (this->mServer.consumeClientConnected())
		{
			this->mAuthenticated = false;
		}
		if (this->mServer.consumeClientDisconnected())
		{
			this->mAuthenticated = false;
		}
		DebugMessage request;
		while (this->mServer.receive(request))
		{
			this->handleMessage(request, context);
		}
	}
	//---------------------------------------------------------
	void EditorControlServer::sendOk(String const& req, DebugMessage& payload)
	{
		payload.type = MSG_OK;
		if (!req.empty())
		{
			payload.set(DebugProtocol::FIELD_REQ, req);
		}
		this->mServer.send(payload);
	}
	//---------------------------------------------------------
	void EditorControlServer::sendOk(String const& req)
	{
		DebugMessage ok(MSG_OK);
		this->sendOk(req, ok);
	}
	//---------------------------------------------------------
	void EditorControlServer::sendErr(String const& req, String const& message)
	{
		DebugMessage err(MSG_ERR);
		err.set(DebugProtocol::FIELD_MESSAGE, message);
		if (!req.empty())
		{
			err.set(DebugProtocol::FIELD_REQ, req);
		}
		this->mServer.send(err);
	}
	//---------------------------------------------------------
	bool EditorControlServer::requireAuth(String const& req)
	{
		// no token configured => auth disabled (developer convenience for a
		// hand-started control port); with a token, a valid hello is required
		if (this->mToken.empty() || this->mAuthenticated)
		{
			return true;
		}
		this->sendErr(req,
			"unauthenticated: send a hello with the auth token first");
		return false;
	}
	//---------------------------------------------------------
	void EditorControlServer::handleMessage(DebugMessage const& request,
		EditorControlContext const& context)
	{
		const String type = request.type;
		const String req = request.get(DebugProtocol::FIELD_REQ);
		EditorState& state = *context.state;
		EditorCore& core = *context.core;
		GameObjectManager& manager = *context.gameObjectManager;

		// auth gate: everything but the pure reads needs a prior valid hello
		if (!isReadVerb(type) && !this->requireAuth(req))
		{
			return;
		}

		// destructive open/new/close verbs clobber the current world; honor the
		// dirty-state policy (refuse unless force=1 or the scene is clean)
		auto clobberRefused = [&](void) -> bool
		{
			if (core.isSceneDirty() && request.get("force") != "1")
			{
				this->sendErr(req, "scene has unsaved changes - save_scene "
					"first or pass force=1");
				return true;
			}
			return false;
		};

		//--- handshake / status ------------------------------
		if (type == "hello")
		{
			if (!this->mToken.empty() &&
				request.get(DebugProtocol::FIELD_TOKEN) != this->mToken)
			{
				this->sendErr(req, "auth failed: wrong or missing token");
				return;
			}
			this->mAuthenticated = true;
			DebugMessage ok(MSG_OK);
			ok.set("editor_version", ORKIGE_EDITOR_VERSION);
			ok.set("protocol_version",
				std::to_string(DebugProtocol::VERSION));
			ok.set("authenticated", "1");
			this->sendOk(req, ok);
			return;
		}
		if (type == "ping")
		{
			this->sendOk(req);
			return;
		}
		if (type == "get_state")
		{
			DebugMessage ok(MSG_OK);
			ok.set("project_loaded", state.project.isLoaded() ? "1" : "0");
			ok.set("project_name", state.project.getName());
			ok.set("project_root", state.project.getRootDirectory());
			ok.set("scene_path", state.currentScenePath);
			ok.set("scene_dirty", core.isSceneDirty() ? "1" : "0");
			ok.set("selected", core.getSelectedObjectId());
			ok.set("object_count",
				std::to_string(manager.getGameObjects().size()));
			ok.set("can_undo", core.canUndo() ? "1" : "0");
			ok.set("can_redo", core.canRedo() ? "1" : "0");
			ok.set("play_mode", playSessionModeName(*context.play));
			this->sendOk(req, ok);
			return;
		}

		//--- hierarchy read ----------------------------------
		if (type == "list_hierarchy")
		{
			StringVector ids;
			StringVector parents;
			StringVector activeSelf;
			StringVector activeHierarchy;
			for (auto const& [id, gameObject] : manager.getGameObjects())
			{
				ids.push_back(id);
				parents.push_back(gameObject->getParentId());
				activeSelf.push_back(gameObject->isActiveSelf() ? "1" : "0");
				activeHierarchy.push_back(
					gameObject->isActiveInHierarchy() ? "1" : "0");
			}
			DebugMessage ok(MSG_OK);
			ok.setList(DebugProtocol::LIST_IDS, ids);
			ok.setList(DebugProtocol::LIST_PARENTS, parents);
			ok.setList(DebugProtocol::LIST_ACTIVE, activeSelf);
			ok.setList("active_hierarchy", activeHierarchy);
			ok.set("selected", core.getSelectedObjectId());
			this->sendOk(req, ok);
			return;
		}
		if (type == "get_object")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			optr<GameObject> gameObject = manager.getGameObject(id).lock();
			if (!gameObject)
			{
				this->sendErr(req, "no such object '" + id + "'");
				return;
			}
			StringVector components;
			for (auto const& [componentType, component] :
				gameObject->getComponents())
			{
				components.push_back(componentType.getName());
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, id);
			ok.set("parent", gameObject->getParentId());
			ok.set("active_self", gameObject->isActiveSelf() ? "1" : "0");
			ok.set("active_hierarchy",
				gameObject->isActiveInHierarchy() ? "1" : "0");
			ok.setList(DebugProtocol::LIST_COMPONENTS, components);
			this->sendOk(req, ok);
			return;
		}

		//--- typed property bundles (read) -------------------
		if (type == "get_component")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String component = request.get(DebugProtocol::FIELD_COMPONENT);
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, id);
			ok.set(DebugProtocol::FIELD_COMPONENT, component);
			if (component == "TransformComponent")
			{
				EditorTransform t;
				if (!core.getObjectTransform(id, t))
				{
					this->sendErr(req, "no TransformComponent on '" + id + "'");
					return;
				}
				const float pos[3] = { t.position.x, t.position.y, t.position.z };
				const float rot[4] = { t.orientation.w, t.orientation.x,
					t.orientation.y, t.orientation.z };
				const float scl[3] = { t.scale.x, t.scale.y, t.scale.z };
				ok.set("position", floats(pos, 3));
				ok.set("orientation", floats(rot, 4));
				ok.set("scale", floats(scl, 3));
			}
			else if (component == "ModelComponent")
			{
				optr<GameObject> gameObject = manager.getGameObject(id).lock();
				if (!gameObject ||
					!gameObject->hasComponent<ModelComponent>())
				{
					this->sendErr(req, "no ModelComponent on '" + id + "'");
					return;
				}
				ok.set("mesh", gameObject->getComponentPtr<ModelComponent>()
					->getCurrentModelFileName());
			}
			else if (component == "ScriptComponent")
			{
				String script;
				bool enabled = false;
				if (!core.getObjectScript(id, script, enabled))
				{
					this->sendErr(req, "no ScriptComponent on '" + id + "'");
					return;
				}
				ok.set("script", script);
				ok.set("enabled", enabled ? "1" : "0");
			}
			else if (component == "RigidBodyComponent")
			{
				PhysicsWorld::BodyDesc d;
				if (!core.getRigidBodyDesc(id, d))
				{
					this->sendErr(req, "no RigidBodyComponent on '" + id + "'");
					return;
				}
				const float halfExtents[3] = { d.halfExtents.x,
					d.halfExtents.y, d.halfExtents.z };
				ok.set("body_type", std::to_string(static_cast<int>(d.bodyType)));
				ok.set("shape_type",
					std::to_string(static_cast<int>(d.shapeType)));
				ok.set("mass", std::to_string(d.mass));
				ok.set("friction", std::to_string(d.friction));
				ok.set("restitution", std::to_string(d.restitution));
				ok.set("planar", d.planar ? "1" : "0");
				ok.set("radius", std::to_string(d.radius));
				ok.set("half_height", std::to_string(d.halfHeight));
				ok.set("half_extents", floats(halfExtents, 3));
			}
			else if (component == "CameraComponent")
			{
				EditorCameraSettings c;
				if (!core.getCameraSettings(id, c))
				{
					this->sendErr(req, "no CameraComponent on '" + id + "'");
					return;
				}
				ok.set("projection_mode", std::to_string(c.projectionMode));
				ok.set("ortho_size", std::to_string(c.orthoSize));
			}
			else if (component == "SpriteComponent")
			{
				EditorSpriteSettings s;
				if (!core.getSpriteSettings(id, s))
				{
					this->sendErr(req, "no SpriteComponent on '" + id + "'");
					return;
				}
				const float tint[4] = { s.tint[0], s.tint[1], s.tint[2],
					s.tint[3] };
				ok.set("texture", s.textureName);
				ok.set("width", std::to_string(s.width));
				ok.set("height", std::to_string(s.height));
				ok.set("tint", floats(tint, 4));
				ok.set("flip_x", s.flipX ? "1" : "0");
				ok.set("flip_y", s.flipY ? "1" : "0");
				ok.set("z_order", std::to_string(s.zOrder));
				ok.set("visible", s.visible ? "1" : "0");
			}
			else
			{
				this->sendErr(req, "no typed property bundle for '" +
					component + "' (v1 exposes Transform/Model/Script/"
					"RigidBody/Camera/Sprite)");
				return;
			}
			this->sendOk(req, ok);
			return;
		}

		//--- typed property bundles (write, undoable) --------
		if (type == "set_component")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String component = request.get(DebugProtocol::FIELD_COMPONENT);
			bool applied = false;
			if (component == "TransformComponent")
			{
				EditorTransform before;
				if (!core.getObjectTransform(id, before))
				{
					this->sendErr(req, "no TransformComponent on '" + id + "'");
					return;
				}
				EditorTransform after = before;
				float v3[3];
				float v4[4];
				if (readFloats(request, "position", v3, 3))
					after.position = Vec3(v3[0], v3[1], v3[2]);
				if (readFloats(request, "orientation", v4, 4))
					after.orientation = Quat(v4[0], v4[1], v4[2], v4[3]);
				if (readFloats(request, "scale", v3, 3))
					after.scale = Vec3(v3[0], v3[1], v3[2]);
				applied = core.applyTransformChange(id, before, after);
			}
			else if (component == "ModelComponent")
			{
				if (!request.has("mesh"))
				{
					this->sendErr(req, "set ModelComponent needs a 'mesh' field");
					return;
				}
				applied = core.changeObjectMesh(id, request.get("mesh"));
			}
			else if (component == "ScriptComponent")
			{
				String script;
				bool enabled = false;
				if (!core.getObjectScript(id, script, enabled))
				{
					this->sendErr(req, "no ScriptComponent on '" + id + "'");
					return;
				}
				if (request.has("script")) script = request.get("script");
				if (request.has("enabled"))
					enabled = request.get("enabled") == "1";
				applied = core.changeObjectScript(id, script, enabled);
			}
			else if (component == "RigidBodyComponent")
			{
				PhysicsWorld::BodyDesc before;
				if (!core.getRigidBodyDesc(id, before))
				{
					this->sendErr(req, "no RigidBodyComponent on '" + id + "'");
					return;
				}
				PhysicsWorld::BodyDesc after = before;
				float v3[3];
				if (request.has("body_type"))
					after.bodyType = static_cast<PhysicsWorld::BodyType>(
						std::atoi(request.get("body_type").c_str()));
				if (request.has("shape_type"))
					after.shapeType = static_cast<PhysicsWorld::ShapeType>(
						std::atoi(request.get("shape_type").c_str()));
				if (request.has("mass"))
					after.mass = static_cast<float>(
						std::atof(request.get("mass").c_str()));
				if (request.has("friction"))
					after.friction = static_cast<float>(
						std::atof(request.get("friction").c_str()));
				if (request.has("restitution"))
					after.restitution = static_cast<float>(
						std::atof(request.get("restitution").c_str()));
				if (request.has("planar"))
					after.planar = request.get("planar") == "1";
				if (request.has("radius"))
					after.radius = static_cast<float>(
						std::atof(request.get("radius").c_str()));
				if (request.has("half_height"))
					after.halfHeight = static_cast<float>(
						std::atof(request.get("half_height").c_str()));
				if (readFloats(request, "half_extents", v3, 3))
					after.halfExtents = Vec3(v3[0], v3[1], v3[2]);
				applied = core.applyRigidBodyChange(id, before, after);
			}
			else if (component == "CameraComponent")
			{
				EditorCameraSettings before;
				if (!core.getCameraSettings(id, before))
				{
					this->sendErr(req, "no CameraComponent on '" + id + "'");
					return;
				}
				EditorCameraSettings after = before;
				if (request.has("projection_mode"))
					after.projectionMode =
						std::atoi(request.get("projection_mode").c_str());
				if (request.has("ortho_size"))
					after.orthoSize = static_cast<float>(
						std::atof(request.get("ortho_size").c_str()));
				applied = core.applyCameraChange(id, before, after);
			}
			else if (component == "SpriteComponent")
			{
				EditorSpriteSettings before;
				if (!core.getSpriteSettings(id, before))
				{
					this->sendErr(req, "no SpriteComponent on '" + id + "'");
					return;
				}
				EditorSpriteSettings after = before;
				float v4[4];
				if (request.has("texture")) after.textureName =
					request.get("texture");
				if (request.has("width")) after.width = static_cast<float>(
					std::atof(request.get("width").c_str()));
				if (request.has("height")) after.height = static_cast<float>(
					std::atof(request.get("height").c_str()));
				if (readFloats(request, "tint", v4, 4))
				{
					after.tint[0] = v4[0]; after.tint[1] = v4[1];
					after.tint[2] = v4[2]; after.tint[3] = v4[3];
				}
				if (request.has("flip_x")) after.flipX =
					request.get("flip_x") == "1";
				if (request.has("flip_y")) after.flipY =
					request.get("flip_y") == "1";
				if (request.has("z_order")) after.zOrder =
					std::atoi(request.get("z_order").c_str());
				if (request.has("visible")) after.visible =
					request.get("visible") == "1";
				applied = core.applySpriteChange(id, before, after);
			}
			else
			{
				this->sendErr(req, "no typed property bundle for '" +
					component + "'");
				return;
			}
			if (!applied)
			{
				this->sendErr(req, "set_component '" + component +
					"' on '" + id + "' was refused (see the editor log)");
				return;
			}
			this->sendOk(req);
			return;
		}

		//--- object mutation (undoable) ----------------------
		if (type == "create_object")
		{
			String id = request.get(DebugProtocol::FIELD_ID);
			String mesh = request.get("mesh");
			if (mesh.empty() || mesh == "cube")
			{
				mesh = RenderWorld::CUBE_MESH_NAME;
				if (id.empty()) id = core.generateObjectId("Cube");
			}
			if (id.empty()) id = core.generateObjectId("Object");
			if (manager.objectExists(id))
			{
				this->sendErr(req, "object id '" + id + "' already exists");
				return;
			}
			float p[3] = { 0.0f, 0.0f, 0.0f };
			readFloats(request, "position", p, 3);
			if (!core.executeCommand(onew(new CreateObjectCommand(
				id, mesh, Vec3(p[0], p[1], p[2])))))
			{
				this->sendErr(req, "create_object failed - mesh '" + mesh +
					"' did not load");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, id);
			this->sendOk(req, ok);
			return;
		}
		if (type == "delete_object")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			if (!manager.objectExists(id))
			{
				this->sendErr(req, "no such object '" + id + "'");
				return;
			}
			if (!core.executeCommand(onew(new DeleteObjectCommand(id))))
			{
				this->sendErr(req, "delete_object '" + id + "' was refused");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "duplicate_object")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			if (!manager.objectExists(id))
			{
				this->sendErr(req, "no such object '" + id + "'");
				return;
			}
			const String newId = core.makeDuplicateId(id);
			if (!core.executeCommand(onew(
				new DuplicateObjectCommand(id, newId))))
			{
				this->sendErr(req, "duplicate_object '" + id + "' was refused");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, newId);
			this->sendOk(req, ok);
			return;
		}
		if (type == "rename_object")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String newId = request.get("new_id");
			if (!core.renameObject(id, newId))
			{
				this->sendErr(req, "rename '" + id + "' -> '" + newId +
					"' was refused (empty/duplicate/unknown)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, newId);
			this->sendOk(req, ok);
			return;
		}
		if (type == "reparent_object")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String parent = request.get("parent");	// "" = make a root
			if (!core.reparentObject(id, parent))
			{
				this->sendErr(req, "reparent '" + id + "' onto '" + parent +
					"' was refused");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "set_active")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			if (!core.setObjectActive(id,
				request.get(DebugProtocol::FIELD_VALUE) == "1"))
			{
				this->sendErr(req, "set_active on '" + id + "' was refused");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "group_objects")
		{
			if (!core.groupSelected())
			{
				this->sendErr(req, "group needs a selection");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_ID, core.getSelectedObjectId());
			this->sendOk(req, ok);
			return;
		}

		//--- component add/remove ----------------------------
		if (type == "add_component")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String component = request.get(DebugProtocol::FIELD_COMPONENT);
			if (!core.addComponentToObject(id, component))
			{
				this->sendErr(req, "add_component '" + component + "' on '" +
					id + "' was refused (already present / unknown type)");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "remove_component")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			const String component = request.get(DebugProtocol::FIELD_COMPONENT);
			String blockedBy;
			if (!core.canRemoveComponent(id, component, &blockedBy))
			{
				this->sendErr(req, "remove_component '" + component +
					"' refused - '" + blockedBy + "' depends on it");
				return;
			}
			if (!core.removeComponentFromObject(id, component))
			{
				this->sendErr(req, "remove_component '" + component + "' on '" +
					id + "' was refused");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "list_addable_components")
		{
			DebugMessage ok(MSG_OK);
			ok.setList(DebugProtocol::LIST_COMPONENTS,
				core.getAddableComponentTypes());
			this->sendOk(req, ok);
			return;
		}

		//--- selection / undo-redo ---------------------------
		if (type == "select")
		{
			const String id = request.get(DebugProtocol::FIELD_ID);
			if (id.empty())
			{
				core.clearSelection();
			}
			else if (!manager.objectExists(id))
			{
				this->sendErr(req, "no such object '" + id + "'");
				return;
			}
			else
			{
				core.selectObject(id);
			}
			this->sendOk(req);
			return;
		}
		if (type == "undo")
		{
			if (!core.undo())
			{
				this->sendErr(req, "nothing to undo");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "redo")
		{
			if (!core.redo())
			{
				this->sendErr(req, "nothing to redo");
				return;
			}
			this->sendOk(req);
			return;
		}

		//--- scene / project documents -----------------------
		if (type == "new_scene")
		{
			if (clobberRefused()) return;
			newScene(state, core);
			this->sendOk(req);
			return;
		}
		if (type == "open_scene")
		{
			if (clobberRefused()) return;
			if (!openSceneFromPath(state, core,
				request.get(DebugProtocol::FIELD_SCENE)))
			{
				this->sendErr(req, "open_scene failed (see the editor log)");
				return;
			}
			this->sendOk(req);
			return;
		}
		if (type == "save_scene")
		{
			String path = request.get(DebugProtocol::FIELD_SCENE);
			if (path.empty()) path = state.currentScenePath;
			if (path.empty())
			{
				this->sendErr(req, "save_scene needs a 'scene' path (the "
					"current scene is untitled)");
				return;
			}
			if (!saveSceneToPath(state, core, path))
			{
				this->sendErr(req, "save_scene failed (see the editor log)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set(DebugProtocol::FIELD_SCENE, path);
			this->sendOk(req, ok);
			return;
		}
		if (type == "open_project")
		{
			if (clobberRefused()) return;
			if (!openProjectFromPath(state, core,
				request.get("path")))
			{
				this->sendErr(req, "open_project failed (see the editor log)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("project_root", state.project.getRootDirectory());
			ok.set("scene_path", state.currentScenePath);
			this->sendOk(req, ok);
			return;
		}
		if (type == "new_project")
		{
			if (clobberRefused()) return;
			if (!newProjectAtPath(state, core, request.get("path")))
			{
				this->sendErr(req, "new_project failed (see the editor log)");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("project_root", state.project.getRootDirectory());
			this->sendOk(req, ok);
			return;
		}
		if (type == "close_project")
		{
			if (clobberRefused()) return;
			closeProject(state, core);
			this->sendOk(req);
			return;
		}

		//--- play control (translated to the ONE player protocol) --
		if (type == "play")
		{
			if (context.play->isActive())
			{
				this->sendErr(req, "already playing");
				return;
			}
			if (!startPlay(*context.play, manager, state.project))
			{
				this->sendErr(req, "play could not start (see the editor log)");
				return;
			}
			// long op: the player boots over subsequent frames - report the
			// launch as accepted and let the host poll get_state for progress
			DebugMessage ok(MSG_OK);
			ok.set("accepted", "1");
			ok.set("play_mode", playSessionModeName(*context.play));
			this->sendOk(req, ok);
			return;
		}
		if (type == "stop")
		{
			if (!context.play->isActive())
			{
				this->sendErr(req, "not playing");
				return;
			}
			requestStopPlay(*context.play);
			this->sendOk(req);
			return;
		}
		if (type == "pause" || type == "resume" || type == "step")
		{
			if (!context.play->client.isConnected())
			{
				this->sendErr(req, "no live player to " + type);
				return;
			}
			if (type == "pause")
			{
				context.play->client.send(DebugMessage(DebugProtocol::MSG_PAUSE));
				context.play->mode = PlaySession::Mode::Paused;
			}
			else if (type == "resume")
			{
				context.play->client.send(DebugMessage(DebugProtocol::MSG_RESUME));
				context.play->mode = PlaySession::Mode::Playing;
			}
			else // step
			{
				context.play->client.send(DebugMessage(DebugProtocol::MSG_STEP));
			}
			this->sendOk(req);
			return;
		}

		//--- screenshot (out of band; returns the written path) --
		if (type == "screenshot")
		{
			const String path = request.get("path");
			if (path.empty())
			{
				this->sendErr(req, "screenshot needs a 'path'");
				return;
			}
			if (request.get("window") == "1")
			{
				// the whole editor window (chrome included)
				RenderSystem::get()->saveWindowContents(path);
			}
			else
			{
				// the chrome-free scene viewport (the EditorSceneRT RTT)
				if (!context.sceneTarget || !context.sceneTarget->texture)
				{
					this->sendErr(req, "no scene render target to capture");
					return;
				}
				context.sceneTarget->texture->writeContentsToFile(path);
			}
			DebugMessage ok(MSG_OK);
			ok.set("path", path);
			this->sendOk(req, ok);
			return;
		}

		//--- asset listing -----------------------------------
		if (type == "list_assets")
		{
			if (!state.project.isLoaded())
			{
				this->sendErr(req, "no project open");
				return;
			}
			DebugMessage ok(MSG_OK);
			ok.set("project_root", state.project.getRootDirectory());
			StringVector ids;
			StringVector paths;
			StringVector names;
			if (optr<AssetDatabase> const& database =
				state.project.getAssetDatabase())
			{
				// page defensively so a big project never blows the 64KiB frame
				const size_t maxEntries = 500;
				std::vector<AssetEntry> assets = database->listAssets();
				bool truncated = false;
				for (AssetEntry const& asset : assets)
				{
					if (ids.size() >= maxEntries)
					{
						truncated = true;
						break;
					}
					ids.push_back(asset.id);
					paths.push_back(asset.relativePath);
					names.push_back(asset.fileName);
				}
				ok.set("truncated", truncated ? "1" : "0");
			}
			ok.setList("asset_ids", ids);
			ok.setList("asset_paths", paths);
			ok.setList("asset_names", names);
			ok.setList("scenes", state.project.listScenes());
			this->sendOk(req, ok);
			return;
		}

		//--- console tail ------------------------------------
		if (type == "console_tail")
		{
			int count = 50;
			if (request.has("count"))
			{
				count = std::atoi(request.get("count").c_str());
			}
			if (count < 1) count = 1;
			if (count > 200) count = 200;	// respect the frame cap
			StringVector lines;
			StringVector levels;
			{
				std::lock_guard<std::mutex> lock(context.console->mutex);
				std::vector<ConsoleLine> const& all = context.console->lines;
				size_t start = all.size() >
					static_cast<size_t>(count)
					? all.size() - static_cast<size_t>(count) : 0;
				for (size_t i = start; i < all.size(); ++i)
				{
					lines.push_back(all[i].text);
					levels.push_back(all[i].level == ConsoleLevel::Error
						? "error" : all[i].level == ConsoleLevel::Warning
						? "warning" : "info");
				}
			}
			DebugMessage ok(MSG_OK);
			ok.setList("lines", lines);
			ok.setList("levels", levels);
			this->sendOk(req, ok);
			return;
		}

		//--- unknown -----------------------------------------
		this->sendErr(req, "unknown command '" + type + "'");
	}
	//---------------------------------------------------------
	//--- EditorControlSelfTest -------------------------------
	//---------------------------------------------------------
	void EditorControlSelfTest::begin(unsigned short port,
		std::string const& token, std::string const& screenshotPath)
	{
		this->mToken = token;
		this->mScreenshotPath = screenshotPath;
		this->mActive = true;
		this->mPhase = Phase::Connecting;
		this->mRequestSent = false;
		this->mDeadline = std::chrono::steady_clock::now() +
			std::chrono::seconds(30);
		if (!this->mClient.connect("127.0.0.1", port))
		{
			this->fail("control self-test: connect() failed");
		}
	}
	//---------------------------------------------------------
	void EditorControlSelfTest::send(String const& type, DebugMessage& message)
	{
		message.type = type;
		this->mOutstandingReq =
			"r" + std::to_string(++this->mReqCounter);
		message.set(DebugProtocol::FIELD_REQ, this->mOutstandingReq);
		this->mClient.send(message);
		this->mRequestSent = true;
	}
	//---------------------------------------------------------
	void EditorControlSelfTest::fail(String const& reason)
	{
		SDL_Log("orkige_editor: FAILED %s", reason.c_str());
		this->mPhase = Phase::Failed;
		this->mDone = true;
		this->mPassed = false;
		this->mActive = false;
	}
	//---------------------------------------------------------
	void EditorControlSelfTest::update(GameObjectManager& manager)
	{
		if (!this->mActive)
		{
			return;
		}
		this->mClient.update();
		if (std::chrono::steady_clock::now() > this->mDeadline)
		{
			this->fail("control self-test: timed out waiting for a reply");
			return;
		}
		if (this->mClient.getState() == DebugClient::State::Failed ||
			this->mClient.getState() == DebugClient::State::Disconnected)
		{
			this->fail("control self-test: link dropped");
			return;
		}

		// (1) wait for the TCP link, then start the sequence with a hello
		if (this->mPhase == Phase::Connecting)
		{
			if (this->mClient.isConnected())
			{
				this->mPhase = Phase::Hello;
				this->mRequestSent = false;
			}
			return;
		}

		// each phase sends its request once, then waits for the correlated ok
		if (!this->mRequestSent)
		{
			switch (this->mPhase)
			{
			case Phase::Hello:
			{
				DebugMessage hello;
				hello.set(DebugProtocol::FIELD_TOKEN, this->mToken);
				this->send("hello", hello);
				break;
			}
			case Phase::Hierarchy:
			{
				DebugMessage message;
				this->send("list_hierarchy", message);
				break;
			}
			case Phase::Create:
			{
				DebugMessage message;
				message.set(DebugProtocol::FIELD_ID, "McpProbe");
				message.set("position", "1 2 3");
				this->send("create_object", message);
				break;
			}
			case Phase::GetTransform:
			{
				DebugMessage message;
				message.set(DebugProtocol::FIELD_ID, this->mCreatedId);
				message.set(DebugProtocol::FIELD_COMPONENT,
					"TransformComponent");
				this->send("get_component", message);
				break;
			}
			case Phase::SetTransform:
			{
				DebugMessage message;
				message.set(DebugProtocol::FIELD_ID, this->mCreatedId);
				message.set(DebugProtocol::FIELD_COMPONENT,
					"TransformComponent");
				message.set("position", "4 5 6");
				this->send("set_component", message);
				break;
			}
			case Phase::VerifyTransform:
			{
				DebugMessage message;
				message.set(DebugProtocol::FIELD_ID, this->mCreatedId);
				message.set(DebugProtocol::FIELD_COMPONENT,
					"TransformComponent");
				this->send("get_component", message);
				break;
			}
			case Phase::Screenshot:
			{
				DebugMessage message;
				message.set("path", this->mScreenshotPath);
				this->send("screenshot", message);
				break;
			}
			default:
				break;
			}
			return;
		}

		// consume the reply for the outstanding request
		DebugMessage reply;
		if (!this->mClient.receive(reply))
		{
			return;
		}
		if (reply.type == EditorControlServer::MSG_ERR)
		{
			this->fail("control self-test: err reply '" +
				reply.get(DebugProtocol::FIELD_MESSAGE) + "'");
			return;
		}
		if (reply.type != EditorControlServer::MSG_OK ||
			reply.get(DebugProtocol::FIELD_REQ) != this->mOutstandingReq)
		{
			this->fail("control self-test: reply type/correlation mismatch "
				"(type='" + reply.type + "', req='" +
				reply.get(DebugProtocol::FIELD_REQ) + "' expected '" +
				this->mOutstandingReq + "')");
			return;
		}

		switch (this->mPhase)
		{
		case Phase::Hello:
			if (reply.get("authenticated") != "1")
			{
				this->fail("control self-test: hello did not authenticate");
				return;
			}
			SDL_Log("orkige_editor: control self-test - hello OK "
				"(editor %s, protocol %s)",
				reply.get("editor_version").c_str(),
				reply.get("protocol_version").c_str());
			this->mPhase = Phase::Hierarchy;
			break;
		case Phase::Hierarchy:
		{
			const StringVector& ids = reply.getList(DebugProtocol::LIST_IDS);
			if (ids.size() < 4)
			{
				this->fail("control self-test: list_hierarchy returned only " +
					std::to_string(ids.size()) + " objects (expected the "
					"Cube1-3 + TestMesh1 fixtures)");
				return;
			}
			SDL_Log("orkige_editor: control self-test - list_hierarchy OK "
				"(%zu objects)", ids.size());
			this->mPhase = Phase::Create;
			break;
		}
		case Phase::Create:
			this->mCreatedId = reply.get(DebugProtocol::FIELD_ID);
			if (this->mCreatedId.empty() ||
				!manager.objectExists(this->mCreatedId))
			{
				this->fail("control self-test: create_object did not create '" +
					this->mCreatedId + "' in the live scene");
				return;
			}
			SDL_Log("orkige_editor: control self-test - create_object OK "
				"('%s')", this->mCreatedId.c_str());
			this->mPhase = Phase::GetTransform;
			break;
		case Phase::GetTransform:
			if (reply.get("position") != "1 2 3")
			{
				this->fail("control self-test: get_component position was '" +
					reply.get("position") + "' (expected '1 2 3')");
				return;
			}
			SDL_Log("orkige_editor: control self-test - get_component OK "
				"(position '%s')", reply.get("position").c_str());
			this->mPhase = Phase::SetTransform;
			break;
		case Phase::SetTransform:
			SDL_Log("orkige_editor: control self-test - set_component OK");
			this->mPhase = Phase::VerifyTransform;
			break;
		case Phase::VerifyTransform:
			if (reply.get("position") != "4 5 6")
			{
				this->fail("control self-test: set_component did not take - "
					"position is '" + reply.get("position") + "'");
				return;
			}
			SDL_Log("orkige_editor: control self-test - transform mutation "
				"verified (position '%s')", reply.get("position").c_str());
			this->mPhase = Phase::Screenshot;
			break;
		case Phase::Screenshot:
		{
			std::error_code ignored;
			if (!std::filesystem::exists(this->mScreenshotPath, ignored) ||
				std::filesystem::file_size(this->mScreenshotPath, ignored) == 0)
			{
				this->fail("control self-test: screenshot file '" +
					this->mScreenshotPath + "' was not written");
				return;
			}
			SDL_Log("orkige_editor: control self-test - screenshot OK "
				"(wrote '%s')", this->mScreenshotPath.c_str());
			SDL_Log("orkige_editor: control self-test PASSED");
			this->mPhase = Phase::Done;
			this->mDone = true;
			this->mPassed = true;
			this->mActive = false;
			return;
		}
		default:
			break;
		}
		this->mRequestSent = false;
	}
}
