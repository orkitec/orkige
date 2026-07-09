// EditorCore - the UI-independent heart of the Orkige editor.
//
// Everything a different UI shell (or a test) needs to drive the editor lives
// here: selection, scene dirty tracking, the active tool, the undo/redo
// command stack and the standard object operations (create / duplicate /
// delete / rename / transform). The ImGui shell in main.cpp only calls into
// this layer - EditorCore must never include ImGui (or SDL) headers.
//
// Testability split: the command stack, naming/rename validation and the
// serialization-based object commands (delete/rename/duplicate restore) are
// pure GameObjectManager + archive logic and run headless (tests/editor_core).
// Anything that touches TransformComponent/ModelComponent needs a live render
// scene and is exercised by the editor_edittest integration run.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <core_game/GameObjectManager.h>
#include <engine_physic/PhysicsWorld.h>
#include <engine_render/RenderMath.h>

#include <map>
#include <vector>

namespace Orkige
{
	class EditorCore;

	//! the classic tool set (Q/W/E/R)
	enum class EditorTool
	{
		Select,		//!< selection only, no gizmo (Q)
		Translate,	//!< move gizmo (W)
		Rotate,		//!< rotate gizmo (E)
		Scale		//!< scale gizmo (R)
	};

	//! gizmo reference frame (X toggles)
	enum class EditorTransformSpace
	{
		World,
		Local
	};

	//! plain position/orientation/scale value bundle (what a
	//! TransformChangeCommand stores as before/after state)
	struct EditorTransform
	{
		Vec3 position = Vec3::ZERO;
		Quat orientation = Quat::IDENTITY;
		Vec3 scale = Vec3::UNIT_SCALE;
	};

	//! @brief full serialized state of one GameObject, captured through the
	//! component save system (the exact per-object logic SceneSerializer
	//! uses). DeleteObject/Rename/Duplicate restore objects from this.
	//! @remarks XMLArchive is file-based, so capture/restore round-trip
	//! through a short-lived temp file; the archive XML itself is kept in
	//! memory between the two.
	class EditorObjectSnapshot
	{
	public:
		//! serialize all components of the given object; false if it is missing
		bool capture(GameObjectManager& gameObjectManager, String const& id);
		//! recreate the captured components on a NEW object named id
		//! (fails if the id is already taken or nothing was captured)
		bool restore(GameObjectManager& gameObjectManager, String const& id) const;
		bool empty() const { return mXml.empty(); }

	private:
		String mXml;		//!< the captured archive content
		String mParentId;	//!< captured parent link ("" = root)
		bool mActiveSelf = true;	//!< captured own active flag
		StringVector mTags;	//!< captured tag list (carried through rename/duplicate)
	};

	//! @brief serialized state of ONE component of one GameObject, captured
	//! through the same archive path EditorObjectSnapshot uses.
	//! RemoveComponentCommand restores the exact component state from this.
	class EditorComponentSnapshot
	{
	public:
		//! serialize the given component of the given object; false if missing
		bool capture(GameObjectManager& gameObjectManager, String const& id,
			String const& componentTypeName);
		//! re-add the captured component to the (existing) object and restore
		//! its serialized state (fails if the component is already attached)
		bool restore(GameObjectManager& gameObjectManager, String const& id) const;
		bool empty() const { return mXml.empty(); }

	private:
		String mXml;				//!< the captured archive content
		String mComponentTypeName;	//!< which component was captured
	};

	//! @brief base class of every undoable editor operation.
	//! @remarks Interactive drags produce one command per frame; commands
	//! sharing a nonzero merge session id (EditorCore::beginMergeSession)
	//! collapse into the first command of the drag via mergeWith - a gizmo
	//! drag is ONE undo step.
	class EditorCommand
	{
	public:
		virtual ~EditorCommand() {}
		//! apply the operation (also used for redo); false = refused, the
		//! command does not enter the stack
		virtual bool execute(EditorCore& core) = 0;
		//! revert the operation; false = undo failed, stack left untouched
		virtual bool unexecute(EditorCore& core) = 0;
		//! human-readable label for the Edit menu ("Undo <description>")
		virtual String getDescription() const = 0;
		//! absorb a follow-up command of the same interactive session;
		//! return true if absorbed (the new command is then discarded)
		virtual bool mergeWith(EditorCommand const& next)
		{
			(void)next;
			return false;
		}

		unsigned int getMergeSession() const { return mMergeSession; }
		void setMergeSession(unsigned int session) { mMergeSession = session; }

	private:
		unsigned int mMergeSession = 0;	//!< 0 = never merges
	};

	//! before/after transform change on one object (gizmo drag, inspector
	//! drag, scripted move) - mergeable within one interactive session
	class TransformChangeCommand : public EditorCommand
	{
	public:
		TransformChangeCommand(String const& objectId,
			EditorTransform const& before, EditorTransform const& after);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;
		virtual bool mergeWith(EditorCommand const& next) override;

		String const& getObjectId() const { return mObjectId; }
		EditorTransform const& getBefore() const { return mBefore; }
		EditorTransform const& getAfter() const { return mAfter; }

	private:
		String mObjectId;
		EditorTransform mBefore;
		EditorTransform mAfter;
	};

	//! create a mesh-carrying GameObject (Create Cube / Create Test Mesh)
	class CreateObjectCommand : public EditorCommand
	{
	public:
		CreateObjectCommand(String const& objectId, String const& meshName,
			Vec3 const& position);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		String mMeshName;
		Vec3 mPosition;
	};

	//! @brief delete an object; undo restores the full serialized component
	//! state, the parent link, the active flag AND re-attaches the children
	//! that moved up to the grandparent when the object went away
	class DeleteObjectCommand : public EditorCommand
	{
	public:
		explicit DeleteObjectCommand(String const& objectId);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		EditorObjectSnapshot mSnapshot;	//!< captured fresh on every execute
		StringVector mChildIds;			//!< direct children at delete time
		bool mWasSelected = false;
	};

	//! rename = serialize + recreate under the new id (GameObject ids are
	//! immutable map keys and component scene-node names derive from them)
	class RenameObjectCommand : public EditorCommand
	{
	public:
		RenameObjectCommand(String const& oldId, String const& newId);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mOldId;
		String mNewId;
	};

	//! clone an object (components via serialize/deserialize), offset it
	//! slightly and select the copy
	class DuplicateObjectCommand : public EditorCommand
	{
	public:
		DuplicateObjectCommand(String const& sourceId, String const& newId);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

		String const& getNewId() const { return mNewId; }

	private:
		String mSourceId;
		String mNewId;
	};

	//! @brief re-parent an object in the GameObject tree (Hierarchy drag &
	//! drop; "" = make it a root). The world transform is preserved (Unity
	//! semantics - GameObject::setParent keepWorldTransform); undo restores
	//! the previous parent AND the exact previous local transform.
	class ReparentObjectCommand : public EditorCommand
	{
	public:
		ReparentObjectCommand(String const& objectId, String const& newParentId);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		String mNewParentId;
		String mOldParentId;			//!< captured on execute
		EditorTransform mOldLocal;		//!< exact pre-reparent local transform
		bool mHadTransform = false;		//!< object carried a TransformComponent
	};

	//! toggle a GameObject's own active flag (Inspector checkbox) -
	//! GameObject::setActive dispatches onSetActive through the subtree
	class SetActiveObjectCommand : public EditorCommand
	{
	public:
		SetActiveObjectCommand(String const& objectId, bool active);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		bool mActive;
		bool mBefore = true;			//!< captured on execute
	};

	//! @brief set a GameObject's tag list (Inspector tags field) - one
	//! undoable step; the manager tag index is updated by GameObject::setTags
	class SetTagsCommand : public EditorCommand
	{
	public:
		SetTagsCommand(String const& objectId, StringVector const& tags);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		StringVector mTags;				//!< the tag list to apply
		StringVector mBefore;			//!< captured on execute
	};

	//! @brief Cmd/Ctrl+G: group the members under a NEW empty parent object
	//! (a bare TransformComponent at the members' world-bounds centre,
	//! parented under the members' common parent). Undo re-parents every
	//! member back (world transforms preserved) and deletes the group object.
	class GroupObjectsCommand : public EditorCommand
	{
	public:
		GroupObjectsCommand(StringVector const& memberIds, String const& groupId);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

		String const& getGroupId() const { return mGroupId; }

	private:
		StringVector mMemberIds;
		String mGroupId;
		StringVector mOldParentIds;		//!< parallel to mMemberIds, captured on execute
	};

	//! @brief the UNDOABLE half of "Create Prefab" (Unity-style): convert the
	//! subtree rooted at an object into a prefab INSTANCE of the already
	//! written .oprefab file - the live children are replaced by the
	//! deterministic "<root>/<localId>" instance objects the file provides
	//! and the root gets its prefabRef/assetId mark. Undo removes the
	//! instance children, clears the mark and restores the original children
	//! with their full serialized state; the prefab FILE stays on disk either
	//! way (a fs side effect, not undoable - like an imported mesh).
	class MakePrefabCommand : public EditorCommand
	{
	public:
		MakePrefabCommand(String const& rootId, String const& prefabFilePath,
			String const& prefabRef, String const& prefabAssetId);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		//! restore the pre-conversion children from the snapshots (DFS order,
		//! parents first - the snapshots re-attach the parent links)
		bool restoreOriginalChildren(EditorCore& core) const;

		String mRootId;
		String mPrefabFilePath;	//!< absolute .oprefab path (the instantiate source)
		String mPrefabRef;		//!< project-relative reference stored on the root
		String mPrefabAssetId;	//!< stable .orkmeta id riding next to the reference
		StringVector mOldChildIds;	//!< pre-conversion descendants (DFS), captured fresh on execute
		std::vector<EditorObjectSnapshot> mOldChildSnapshots;	//!< parallel to mOldChildIds
	};

	//! @brief a batch of commands that does/undoes as ONE undo step
	//! (multi-select delete/duplicate). Execute runs the children in order
	//! and rolls the already-executed prefix back if one refuses; unexecute
	//! runs in reverse order.
	class CompositeCommand : public EditorCommand
	{
	public:
		explicit CompositeCommand(String const& description);
		//! append a child (call before the composite is executed)
		void addCommand(optr<EditorCommand> const& command);
		bool empty() const { return mCommands.empty(); }
		std::size_t size() const { return mCommands.size(); }
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mDescription;
		std::vector<optr<EditorCommand>> mCommands;
	};

	//! @brief add a component (by type name) to an object. Dependencies the
	//! component pulls in (ComponentHolder's addDependency machinery) are
	//! added automatically by the holder; undo removes the component AND the
	//! dependencies that were added along with it.
	class AddComponentCommand : public EditorCommand
	{
	public:
		AddComponentCommand(String const& objectId,
			String const& componentTypeName);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		String mComponentTypeName;
		//! every component type execute actually added (requested type first,
		//! then the auto-added dependencies) - undo removes exactly these
		StringVector mAddedTypeNames;
	};

	//! @brief remove a component from an object; refused while another
	//! attached component depends on it (EditorCore::canRemoveComponent -
	//! the holder would cascade-remove dependents, the editor blocks instead,
	//! like Unity). Undo re-adds the component and restores its serialized
	//! state from the snapshot.
	class RemoveComponentCommand : public EditorCommand
	{
	public:
		RemoveComponentCommand(String const& objectId,
			String const& componentTypeName);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		String mComponentTypeName;
		EditorComponentSnapshot mSnapshot;	//!< captured fresh on every execute
	};

	//! @brief swap the mesh of a ModelComponent (Inspector mesh field);
	//! execute/unexecute reload the entity through ModelComponent::loadModel
	class ChangeMeshCommand : public EditorCommand
	{
	public:
		ChangeMeshCommand(String const& objectId, String const& beforeMesh,
			String const& afterMesh);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		String mBeforeMesh;
		String mAfterMesh;
	};

	//! @brief edit a ScriptComponent from the Inspector: the script file path
	//! and the enabled flag change as ONE undoable command (the ChangeMesh
	//! pattern; both values travel together so a single command type covers
	//! the path field and the checkbox)
	class ChangeScriptCommand : public EditorCommand
	{
	public:
		ChangeScriptCommand(String const& objectId,
			String const& beforeScript, bool beforeEnabled,
			String const& afterScript, bool afterEnabled);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		String mBeforeScript;
		bool mBeforeEnabled;
		String mAfterScript;
		bool mAfterEnabled;
	};

	//! before/after RigidBodyComponent creation-parameter (BodyDesc) change -
	//! mergeable within one interactive session like a transform drag
	class RigidBodyChangeCommand : public EditorCommand
	{
	public:
		RigidBodyChangeCommand(String const& objectId,
			PhysicsWorld::BodyDesc const& before,
			PhysicsWorld::BodyDesc const& after);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;
		virtual bool mergeWith(EditorCommand const& next) override;

	private:
		String mObjectId;
		PhysicsWorld::BodyDesc mBefore;
		PhysicsWorld::BodyDesc mAfter;
	};

	//! plain value bundle of the CameraComponent state the Inspector edits
	//! (plain int/float on purpose - no engine enums/Ogre types in the UI seam)
	struct EditorCameraSettings
	{
		int projectionMode = 0;		//!< CameraComponent::ProjectionMode value
		float orthoSize = 5.0f;		//!< orthographic vertical half-extent
	};

	//! before/after CameraComponent projection change - mergeable like a drag
	class CameraChangeCommand : public EditorCommand
	{
	public:
		CameraChangeCommand(String const& objectId,
			EditorCameraSettings const& before,
			EditorCameraSettings const& after);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;
		virtual bool mergeWith(EditorCommand const& next) override;

	private:
		String mObjectId;
		EditorCameraSettings mBefore;
		EditorCameraSettings mAfter;
	};

	//! plain value bundle of the SpriteComponent state the Inspector edits
	struct EditorSpriteSettings
	{
		String textureName;			//!< texture resource name ("" = no sprite)
		float width = 0.0f;			//!< world units (<= 0 = from texture aspect)
		float height = 0.0f;		//!< world units (<= 0 = from texture aspect)
		float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };	//!< RGBA colour tint
		bool flipX = false;			//!< mirror horizontally
		bool flipY = false;			//!< mirror vertically
		int zOrder = 0;				//!< sprite sort order
		bool visible = true;		//!< sprite visibility
	};

	//! before/after SpriteComponent change - mergeable like a drag
	class SpriteChangeCommand : public EditorCommand
	{
	public:
		SpriteChangeCommand(String const& objectId,
			EditorSpriteSettings const& before,
			EditorSpriteSettings const& after);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;
		virtual bool mergeWith(EditorCommand const& next) override;

	private:
		String mObjectId;
		EditorSpriteSettings mBefore;
		EditorSpriteSettings mAfter;
	};

	//! @brief UI-independent editor state and operations.
	//! All mutating object operations go through the command stack so they
	//! are undoable; every executed/undone/redone command marks the scene
	//! dirty.
	class EditorCore
	{
	public:
		//! result of validateRename
		enum class NameValidation
		{
			Ok,
			Empty,		//!< new name is empty
			Exists,		//!< another object already has that name
			Unchanged	//!< new name equals the current one (a no-op)
		};

		//! snap step while dragging with Cmd/Ctrl held (or snap toggled on)
		static const float SNAP_TRANSLATE;			//!< world units
		static const float SNAP_ROTATE_DEGREES;	//!< degrees
		static const float SNAP_SCALE;				//!< scale step
		//! duplicated objects appear next to the source, not inside it
		static const Vec3 DUPLICATE_OFFSET;

		explicit EditorCore(GameObjectManager& gameObjectManager);

		GameObjectManager& getGameObjectManager() { return mGameObjectManager; }

		//--- selection ---------------------------------------
		// The selection is an ordered set with a PRIMARY (the most recently
		// selected id = the back of the list). The Inspector and the gizmo
		// operate on the primary; delete/duplicate operate on the whole set.
		//! the primary selection ("" if nothing is selected)
		String getSelectedObjectId() const;
		//! true if something is selected AND the primary still exists
		bool hasSelection() const;
		//! is the id part of the selection set
		bool isSelected(String const& id) const;
		//! replace the whole selection with this one id
		void selectObject(String const& id);
		//! Cmd/Ctrl+click: add the id (it becomes primary) or, if it is
		//! already selected, remove it from the set
		void toggleSelection(String const& id);
		//! add to the selection set without clearing (id becomes primary)
		void addToSelection(String const& id);
		//! remove one id from the selection set (no-op if not selected)
		void deselectObject(String const& id);
		void clearSelection() { mSelection.clear(); }
		//! the ordered selection set (oldest first, primary last)
		StringVector const& getSelection() const { return mSelection; }
		std::size_t getSelectionCount() const { return mSelection.size(); }

		//--- tool state --------------------------------------
		EditorTool getActiveTool() const { return mActiveTool; }
		void setActiveTool(EditorTool tool) { mActiveTool = tool; }
		EditorTransformSpace getTransformSpace() const { return mTransformSpace; }
		void setTransformSpace(EditorTransformSpace space) { mTransformSpace = space; }
		void toggleTransformSpace();
		bool isSnapEnabled() const { return mSnapEnabled; }
		void setSnapEnabled(bool enabled) { mSnapEnabled = enabled; }
		//! adjustable snap step values (Unity-style editable snap settings);
		//! they default to the SNAP_* constants and are edited via the
		//! toolbar's snap popover - setSnapValues clamps every step to a
		//! sane positive minimum (a zero step would freeze the gizmo)
		float getSnapTranslate() const { return mSnapTranslate; }
		float getSnapRotateDegrees() const { return mSnapRotateDegrees; }
		float getSnapScale() const { return mSnapScale; }
		void setSnapValues(float translate, float rotateDegrees, float scale);

		//--- scene dirty tracking ----------------------------
		bool isSceneDirty() const { return mSceneDirty; }
		void markSceneDirty() { mSceneDirty = true; }
		void clearSceneDirty() { mSceneDirty = false; }

		//--- naming ------------------------------------------
		//! first free "<prefix><N>" id (counters restart per scene)
		String generateObjectId(String const& prefix);
		//! first free "<sourceId> Copy"/"<sourceId> Copy <N>" id
		String makeDuplicateId(String const& sourceId) const;
		//! rename rules: empty and duplicate names are rejected
		NameValidation validateRename(String const& currentId,
			String const& newId) const;

		//--- object operations (undoable) --------------------
		//! auto-named cube at the origin, selected afterwards
		bool createCube();
		//! auto-named glTF test mesh at the origin, selected afterwards
		bool createTestMesh();
		//! clone ALL selected objects (slightly offset, copies selected);
		//! multiple clones batch into one undo step (CompositeCommand)
		bool duplicateSelected();
		//! delete ALL selected objects (undo restores full component state);
		//! multiple deletes batch into one undo step (CompositeCommand)
		bool deleteSelected();
		//! rename an object (validateRename rules apply)
		bool renameObject(String const& id, String const& newId);
		//! @brief may the object be re-parented onto newParentId right now?
		//! ("" = root is always fine; refused for self, unknown ids and own
		//! descendants - the same rules GameObject::setParent enforces, so the
		//! Hierarchy can grey out invalid drop targets without side effects)
		bool canReparent(String const& id, String const& newParentId) const;
		//! @brief re-parent an object (undoable, world transform preserved);
		//! when the id is part of a multi-selection the WHOLE selection
		//! re-parents as one undo step (invalid members are skipped)
		bool reparentObject(String const& id, String const& newParentId);
		//! set the object's own active flag (undoable, Unity SetActive)
		bool setObjectActive(String const& id, bool active);
		//! @brief replace an object's tag list (undoable); refused if the object
		//! is missing or the tags did not change
		bool setObjectTags(String const& id, StringVector const& tags);
		//! read an object's tag list; false if the object is missing
		bool getObjectTags(String const& id, StringVector& out) const;
		//! @brief Cmd/Ctrl+G: group ALL selected objects under a new empty
		//! parent (auto-named "Group<N>", selected afterwards) - one undo step
		bool groupSelected();
		//! @brief may "Create Prefab" run on this object right now? Refused
		//! for unknown ids, prefab-PROVIDED objects (a prefab child cannot
		//! become its own prefab) and subtrees containing a nested instance
		//! BELOW the root; an instance root itself is fine (re-making its
		//! prefab is the v1 edit loop). reason (optional) receives the
		//! human-readable refusal for the Console.
		bool canMakePrefab(String const& id, String* reason = nullptr) const;
		//! @brief the undoable half of Create Prefab: convert the subtree
		//! rooted at id into an instance of the ALREADY WRITTEN prefab file
		//! (see MakePrefabCommand). The caller wrote the file
		//! (PrefabSerializer::savePrefab) and minted its asset id - the fs
		//! side effect is not undoable, the conversion is.
		bool makePrefabInstance(String const& id, String const& prefabFilePath,
			String const& prefabRef, String const& prefabAssetId);
		//! record a before/after transform change as one undoable command;
		//! pass a merge session id to collapse a whole drag into one step
		bool applyTransformChange(String const& id,
			EditorTransform const& before, EditorTransform const& after,
			unsigned int mergeSession = 0);

		//--- component operations (undoable) ------------------
		//! all component type names the Add Component popup may offer
		//! (everything registered in the GameObjectComponent factory), sorted
		StringVector getAddableComponentTypes() const;
		//! add a component by type name (undoable; dependencies are pulled in
		//! automatically and removed again on undo)
		bool addComponentToObject(String const& id,
			String const& componentTypeName);
		//! remove a component by type name (undoable; refused while another
		//! attached component depends on it - see canRemoveComponent)
		bool removeComponentFromObject(String const& id,
			String const& componentTypeName);
		//! @brief may the component be removed right now? False while another
		//! ATTACHED component of the object lists it as a dependency (the
		//! dependency info addDependency registered); blockedBy (optional)
		//! receives the name of the first dependent component.
		bool canRemoveComponent(String const& id,
			String const& componentTypeName, String* blockedBy = nullptr) const;
		//! swap a ModelComponent's mesh (undoable; entity reloads); refused
		//! if the object has no ModelComponent or the mesh fails to load
		bool changeObjectMesh(String const& id, String const& meshName);
		//! @brief change a ScriptComponent's script path and/or enabled flag
		//! (one undoable command); refused if the object has no
		//! ScriptComponent or nothing changed. The editor never RUNS scripts
		//! (it does not tick components) - the path is not validated here,
		//! the playing runtime reports load errors.
		bool changeObjectScript(String const& id, String const& scriptFile,
			bool enabled);
		//! record a before/after RigidBodyComponent BodyDesc change as one
		//! undoable command (merge session = one drag, like transforms)
		bool applyRigidBodyChange(String const& id,
			PhysicsWorld::BodyDesc const& before,
			PhysicsWorld::BodyDesc const& after,
			unsigned int mergeSession = 0);
		//! record a before/after CameraComponent projection change (undoable)
		bool applyCameraChange(String const& id,
			EditorCameraSettings const& before,
			EditorCameraSettings const& after,
			unsigned int mergeSession = 0);
		//! record a before/after SpriteComponent change (undoable)
		bool applySpriteChange(String const& id,
			EditorSpriteSettings const& before,
			EditorSpriteSettings const& after,
			unsigned int mergeSession = 0);

		//--- component access (helpers the commands share) ----
		//--- physics collision layers (project-config, read-only in the editor) ---
		//! @brief load the open project's collision-layer config (the dropdown
		//! source for the RigidBody Inspector); resets to the built-in default
		//! (a single "Default" layer) when the project has no physics.olayers.
		//! The editor never runs physics - this is purely for authoring.
		void loadPhysicsLayers(Project const& project);
		//! reset the collision-layer config to the built-in default
		void resetPhysicsLayers();
		//! the collision-layer names offered by the RigidBody layer dropdown
		StringVector getPhysicsLayerNames() const;

		//! read the RigidBodyComponent's creation parameters; false if missing
		bool getRigidBodyDesc(String const& id,
			PhysicsWorld::BodyDesc& out) const;
		//! raw BodyDesc apply WITHOUT a command (commands call this)
		bool setRigidBodyDesc(String const& id,
			PhysicsWorld::BodyDesc const& desc);
		//! read the CameraComponent's projection state; false if missing
		bool getCameraSettings(String const& id,
			EditorCameraSettings& out) const;
		//! raw camera projection apply WITHOUT a command (commands call this)
		bool setCameraSettings(String const& id,
			EditorCameraSettings const& settings);
		//! read the SpriteComponent's state; false if missing
		bool getSpriteSettings(String const& id,
			EditorSpriteSettings& out) const;
		//! @brief raw sprite state apply WITHOUT a command (commands call this);
		//! a changed texture name (re)loads the sprite, "" removes it
		bool setSpriteSettings(String const& id,
			EditorSpriteSettings const& settings);
		//! raw mesh (re)load WITHOUT a command (commands call this); reloads
		//! the old mesh and returns false if the new one fails to load
		bool setObjectMesh(String const& id, String const& meshName);
		//! read the ScriptComponent's script path + enabled flag; false if missing
		bool getObjectScript(String const& id, String& scriptFile,
			bool& enabled) const;
		//! raw script path/enabled apply WITHOUT a command (commands call this)
		bool setObjectScript(String const& id, String const& scriptFile,
			bool enabled);

		//--- transform access (engine-touching helpers) ------
		//! @brief read the object's LOCAL transform (what the Inspector edits
		//! and the scene serializes; identical to world for roots)
		bool getObjectTransform(String const& id, EditorTransform& out) const;
		//! raw LOCAL transform apply WITHOUT a command (commands call this)
		bool setObjectTransform(String const& id, EditorTransform const& transform);
		//! @brief read the object's WORLD transform (composed through the
		//! GameObject tree) - what the Scene panel gizmo manipulates
		bool getObjectWorldTransform(String const& id, EditorTransform& out) const;
		//! @brief convert a WORLD transform into the object's parent-relative
		//! LOCAL one (the gizmo edits world space, the undoable
		//! TransformChangeCommand stores local values)
		bool worldToLocalTransform(String const& id,
			EditorTransform const& world, EditorTransform& outLocal) const;

		//--- undo/redo ---------------------------------------
		//! run the command; on success the redo stack clears and the command
		//! enters the undo stack (or merges into its session predecessor)
		bool executeCommand(optr<EditorCommand> const& command);
		bool undo();
		bool redo();
		bool canUndo() const { return !mUndoStack.empty(); }
		bool canRedo() const { return !mRedoStack.empty(); }
		//! description of the command undo/redo would apply ("" if none)
		String getUndoDescription() const;
		String getRedoDescription() const;
		//! open a new interactive merge session (gizmo/inspector drag)
		unsigned int beginMergeSession() { return mNextMergeSession++; }
		std::size_t getUndoStackSize() const { return mUndoStack.size(); }
		std::size_t getRedoStackSize() const { return mRedoStack.size(); }
		void clearHistory();
		//! back to a pristine editor: no selection, no history, clean scene
		//! (File > New/Open call this)
		void resetForScene();

		//--- internals shared with the commands --------------
		//! create a GameObject carrying a mesh through TransformComponent +
		//! ModelComponent (NOT undoable - the scripted-test fixture setup
		//! and CreateObjectCommand use it). Requires a booted engine.
		bool instantiateModelObject(String const& id, String const& meshName,
			Vec3 const& position);
		//! re-apply the unlit vertex-colour render state after a (re)load -
		//! ModelComponent does not serialize material tweaks yet. Safe to
		//! call on objects without a ModelComponent (does nothing).
		void applyModelFixups(String const& id);
		//! snapshot+recreate implementation behind RenameObjectCommand
		bool renameNow(String const& oldId, String const& newId);

	private:
		GameObjectManager& mGameObjectManager;
		StringVector mSelection;	//!< ordered selection set, primary = back
		EditorTool mActiveTool = EditorTool::Translate;
		EditorTransformSpace mTransformSpace = EditorTransformSpace::World;
		bool mSnapEnabled = false;
		//! editable snap steps (initialised to the SNAP_* constants in the
		//! constructor - the static members live in the .cpp)
		float mSnapTranslate;
		float mSnapRotateDegrees;
		float mSnapScale;
		bool mSceneDirty = false;
		std::map<String, int> mNameCounters;	//!< generateObjectId state
		std::vector<optr<EditorCommand>> mUndoStack;
		std::vector<optr<EditorCommand>> mRedoStack;
		unsigned int mNextMergeSession = 1;
		//! the open project's collision layers (dropdown source; default =
		//! single "Default" layer) - the editor reads this, never simulates
		PhysicsWorld::LayerConfig mPhysicsLayers;
	};
}
