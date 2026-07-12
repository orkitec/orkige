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

	//! the classic tool set (Q/W/E/R) plus the 2D grid paint tool (B)
	enum class EditorTool
	{
		Select,		//!< selection only, no gizmo (Q)
		Translate,	//!< move gizmo (W)
		Rotate,		//!< rotate gizmo (E)
		Scale,		//!< scale gizmo (R)
		Paint		//!< prefab grid painting in 2D mode (B)
	};

	//! @brief the paint grid the 2D grid-paint tool snaps to. Cells are square
	//! and UNBOUNDED (an off-grid cell paints fine); when the scene carries a
	//! LevelComponent the cells coincide with the game's slots, otherwise the
	//! editor's translate snap step defines the cell size at the world origin.
	struct EditorPaintGrid
	{
		float originX = 0.0f;	//!< world X of cell (0,0)'s center
		float originY = 0.0f;	//!< world Y of cell (0,0)'s center
		float cellSize = 1.0f;	//!< cell edge length (always > 0)
	};

	//! integer cell coordinate for a world coordinate on one axis (nearest
	//! cell center; unbounded, so negatives are valid)
	int paintCellCoord(float world, float origin, float cellSize);
	//! world coordinate of a cell's center on one axis
	float paintCellCenter(int cell, float origin, float cellSize);

	//! @brief one component-property stamp the paint tool applies to a painted
	//! root - generic and reflection-driven (the palette uses it to stamp
	//! TileComponent.openEdges). addComponent is true when the prefab root does
	//! not already carry the component (then it is added before the property).
	struct EditorPaintStamp
	{
		String componentTypeName;	//!< the component to stamp
		bool addComponent = false;	//!< add the component first (missing on root)
		String propertyName;		//!< reflected property to set ("" = none)
		String value;				//!< canonical string form of the value
	};

	//! @brief what ONE paint action places into a grid cell. The tool paints
	//! two occupant kinds through the SAME seam: a PREFAB instance (its file +
	//! ref/id, the prefab-local children to suppress) OR a BARE-ASSET tile - a
	//! grid-cell-sized object built straight from a texture (SpriteTile) or an
	//! .oshape (ShapeTile), NO prefab file generated, carrying a TileComponent
	//! that stamps the source-asset id so the shared art propagates and a
	//! re-paint of the same asset is a no-op. Tags and reflected-property stamps
	//! apply to either kind. Built by the palette; consumed by paintTileAtCell.
	enum class PaintTileKind
	{
		Prefab,		//!< instantiate the .oprefab (prefabFilePath/prefabRef/...)
		SpriteTile,	//!< a bare quad tile: SpriteComponent(assetName) + TileComponent
		ShapeTile	//!< a bare shape tile: VectorShapeComponent(assetName) + TileComponent
	};
	struct EditorPaintDesc
	{
		PaintTileKind kind = PaintTileKind::Prefab;	//!< which occupant to place
		//--- PREFAB tile fields (kind == Prefab) ---
		String prefabFilePath;			//!< absolute .oprefab source
		String prefabRef;				//!< project-relative ref stored on the root
		String prefabAssetId;			//!< stable .orkmeta id
		StringVector suppressedChildren;	//!< prefab-local ids dropped at instantiate
		//--- BARE-ASSET tile fields (kind == SpriteTile / ShapeTile) ---
		String assetName;				//!< bare texture/.oshape file name (loadSprite/loadShape)
		String assetRef;				//!< project-relative ref (diagnostic)
		String assetId;					//!< stable id stamped on TileComponent.sourceAssetId
		//--- either kind ---
		StringVector tags;				//!< tags stamped on the root (empty = none)
		std::vector<EditorPaintStamp> stamps;	//!< reflected property stamps
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

	//! @brief create a sprite-carrying GameObject (a TransformComponent +
	//! SpriteComponent quad in the XY plane) - the shared primitive behind the
	//! Asset browser's "drag a texture into the scene" and any future
	//! sprite-object instantiation. Mirrors CreateObjectCommand: execute
	//! instantiates + selects, undo deselects + deletes; a texture that fails
	//! to load leaves the (empty) sprite object like ModelComponent does.
	class CreateSpriteObjectCommand : public EditorCommand
	{
	public:
		CreateSpriteObjectCommand(String const& objectId,
			String const& textureName, Vec3 const& position);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		String mTextureName;
		Vec3 mPosition;
	};

	//! @brief create a new GameObject carrying a VectorShapeComponent showing
	//! the given .oshape asset (Asset browser drag/double-click of a shape).
	//! instantiates + selects, undo deselects + deletes; a shape that fails to
	//! load leaves the (empty) shape object like the sprite variant does.
	class CreateVectorShapeObjectCommand : public EditorCommand
	{
	public:
		CreateVectorShapeObjectCommand(String const& objectId,
			String const& shapeName, Vec3 const& position);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		String mShapeName;
		Vec3 mPosition;
	};

	//! @brief instantiate a .oprefab asset into the scene as a NEW prefab
	//! instance (Asset browser drag/double-click). Unlike
	//! MakePrefabCommand (which converts an EXISTING subtree), this creates the
	//! instance root + its prefab-provided children from scratch and marks the
	//! root with the prefab reference. Undo removes the whole instance subtree
	//! (deepest first) and deselects; the .oprefab file on disk is untouched.
	class CreatePrefabInstanceCommand : public EditorCommand
	{
	public:
		//! suppressedChildren (optional) drops those prefab-local children on
		//! the new instance right at creation - the grid paint tool passes the
		//! open-edge wall locals; the asset-browser drop leaves it empty
		CreatePrefabInstanceCommand(String const& instanceRootId,
			String const& prefabFilePath, String const& prefabRef,
			String const& prefabAssetId, Vec3 const& position,
			StringVector const& suppressedChildren = StringVector());
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mRootId;
		String mPrefabFilePath;	//!< absolute .oprefab path (the instantiate source)
		String mPrefabRef;		//!< project-relative reference stored on the root
		String mPrefabAssetId;	//!< stable .orkmeta id riding next to the reference
		Vec3 mPosition;
		StringVector mSuppressedChildren;	//!< prefab-local ids dropped at instantiate
	};

	//! @brief delete a WHOLE object subtree as one undoable step, preserving
	//! prefab identity. Unlike DeleteObjectCommand (single object, children
	//! promote to the grandparent, prefabRef/suppressions lost), this captures
	//! the entire subtree (DFS, parents first) PLUS the root's
	//! prefabRef/assetId/suppressed list, deletes deepest first and on undo
	//! restores the subtree parents-first then re-marks the root as the prefab
	//! instance it was. The grid paint tool's erase/replace routes here so a
	//! painted instance (its "<root>/..." provided children and any scene-side
	//! extra children) comes back intact.
	class DeleteSubtreeCommand : public EditorCommand
	{
	public:
		explicit DeleteSubtreeCommand(String const& rootId);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mRootId;
		StringVector mSubtreeIds;	//!< captured subtree (DFS, parents first)
		std::vector<EditorObjectSnapshot> mSubtreeSnapshots;	//!< parallel to mSubtreeIds
		String mPrefabRef;			//!< root's prefab ref (re-marked on undo)
		String mPrefabAssetId;		//!< root's prefab asset id
		StringVector mSuppressed;	//!< root's suppressed-children list
		bool mWasSelected = false;
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
	//! drop; "" = make it a root). The world transform is preserved
	//! (GameObject::setParent keepWorldTransform); undo restores
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

	//! @brief the UNDOABLE half of "Create Prefab": convert the
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

	//! @brief delete a prefab-PROVIDED child by SUPPRESSING it (a
	//! provided child cannot just be removed - the prefab would bring it back
	//! on reload). Execute records the child's prefab-LOCAL id in the instance
	//! root's suppressedChildren AND removes the child subtree; undo drops the
	//! suppression entry and restores the subtree from its snapshots. Routed to
	//! from EditorCore::deleteSelected when the target is prefab-provided.
	class SuppressPrefabChildCommand : public EditorCommand
	{
	public:
		explicit SuppressPrefabChildCommand(String const& childId);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mChildId;
		String mRootId;			//!< instance root, resolved on execute
		String mLocalId;		//!< the child's prefab-local id, resolved on execute
		bool mAddedSuppression = false;	//!< did execute actually add the entry
		bool mWasSelected = false;
		StringVector mSubtreeIds;	//!< the removed provided subtree (DFS, parents first)
		std::vector<EditorObjectSnapshot> mSubtreeSnapshots;	//!< parallel to mSubtreeIds
	};

	//! @brief Revert a prefab instance to the pristine prefab: drops the
	//! instance's per-child property OVERRIDES and structural SUPPRESSIONS and
	//! re-instantiates the provided children from the .oprefab (the root's own
	//! components - its placement / v1 root override - are kept). Undoable: undo
	//! restores the exact pre-revert overrides, suppressions and provided
	//! children. Refused (and not entered) when the prefab file is unavailable.
	class RevertPrefabCommand : public EditorCommand
	{
	public:
		RevertPrefabCommand(String const& rootId, String const& prefabFilePath);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		//! restore the captured provided children (DFS, parents first)
		bool restoreCapturedChildren(EditorCore& core) const;
		//! remove the current provided subtree (deepest first)
		void removeProvidedChildren(EditorCore& core) const;

		String mRootId;
		String mPrefabFilePath;
		StringVector mOldSuppressed;		//!< suppressed list before revert
		GameObject::ChildOverrideMap mOldOverrides;	//!< overrides before revert
		StringVector mOldChildIds;			//!< provided subtree before revert (DFS)
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
		//! @brief absorb a follow-up CompositeCommand of the same interactive
		//! session by appending its (already executed) children - so a whole
		//! paint stroke (one composite per cell, all sharing the stroke's merge
		//! session) collapses into ONE undo step. Unexecute already runs in
		//! reverse, so the merged children unwind in the right order.
		virtual bool mergeWith(EditorCommand const& next) override;

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
	//! the holder would cascade-remove dependents, the editor blocks instead).
	//! Undo re-adds the component and restores its serialized
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

	// (the Camera and Sprite typed value bundles + their per-component
	// CameraChangeCommand/SpriteChangeCommand were retired: the
	// auto-generated Inspector and the generic MCP get/set_component
	// now edit these components through the reflection registry and the ONE
	// PropertyChangeCommand below - no per-component command/struct.)

	//! @brief the GENERIC, reflection-driven property edit: change
	//! ONE reflected property of ONE component, captured purely as PropertyValue
	//! canonical strings ({objectId, componentTypeName, propertyName, before,
	//! after}). execute/unexecute resolve the component + PropertyDesc off the
	//! schema and apply through the reflected setter, so the change takes effect
	//! live in the viewport (a Sprite reloads its texture, a Transform moves).
	//! This is the ONE command the auto-generated Inspector routes every edit
	//! through - no per-component command type. Mergeable within one interactive
	//! session (a slider drag collapses to one undo step) when the session id and
	//! the (object,component,property) target match.
	class PropertyChangeCommand : public EditorCommand
	{
	public:
		PropertyChangeCommand(String const& objectId,
			String const& componentTypeName, String const& propertyName,
			String const& beforeValue, String const& afterValue);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;
		virtual bool mergeWith(EditorCommand const& next) override;

	private:
		String mObjectId;
		String mComponentTypeName;
		String mPropertyName;
		String mBefore;		//!< the property's canonical string before the edit
		String mAfter;		//!< the property's canonical string after the edit
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
		//! adjustable snap step values (editable snap settings);
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
		//! set the object's own active flag (undoable)
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
		//! @brief is the object a prefab-PROVIDED child (some ancestor is an
		//! instance root and the id lies in its namespace)? Deleting such a
		//! child suppresses it rather than plainly removing it.
		bool isPrefabProvidedChild(String const& id) const;
		//! @brief may Apply / Revert run on this object right now? True only for
		//! a live prefab instance ROOT (getPrefabRef non-empty).
		bool canApplyOrRevertPrefab(String const& id) const;
		//! @brief Apply an instance's current state back to its .oprefab: writes
		//! the whole live subtree (with its per-child overrides baked in) as the
		//! new prefab, then clears the instance's local overrides + suppressions
		//! (they are now part of the asset) and re-baselines the children.
		//! NOT undoable - the filesystem overwrite is a side effect (like
		//! re-importing a mesh); refused for a non-instance or a failed write.
		bool applyPrefabToSource(String const& id, String const& prefabFilePath);
		//! @brief Revert an instance to the pristine prefab (undoable): see
		//! RevertPrefabCommand. Refused when the prefab file is unavailable.
		bool revertPrefabInstance(String const& id, String const& prefabFilePath);
		//! record a before/after transform change as one undoable command;
		//! pass a merge session id to collapse a whole drag into one step
		bool applyTransformChange(String const& id,
			EditorTransform const& before, EditorTransform const& after,
			unsigned int mergeSession = 0);

		//--- 2D grid painting (undoable) ---------------------
		//! @brief the paint grid for the current scene: the geometry of the
		//! first object carrying a LevelComponent when one exists, otherwise
		//! {origin (0,0), cellSize = the translate snap step}. Cells coincide
		//! with the game's slots whenever a LevelComponent is present.
		EditorPaintGrid resolvePaintGrid() const;
		//! @brief the id of the TILE occupying the cell centered at (centerX,
		//! centerY), or "" when the cell is free. A cell is occupied by a
		//! ROOT-level object (no parent) whose TransformComponent position lies
		//! within half a cell of the center in BOTH axes AND that is a paintable
		//! tile: a PREFAB instance (non-empty prefabRef) OR a BARE-ASSET tile
		//! (carries a TileComponent). Plain scene objects (a Ball, a Level, a
		//! loose Decoration - neither a prefab instance nor a tile marker) are
		//! never returned; they cannot be painted over or erased.
		String findTileAtCell(float centerX, float centerY,
			float cellSize) const;
		//! @brief paint a tile into the cell centered at (centerX, centerY) as
		//! ONE undoable step - a prefab instance OR a bare-asset sprite/shape tile
		//! per desc.kind. An occupant of the same cell is replaced (erase then
		//! create), even across kinds (a sprite tile can replace a prefab tile).
		//! Returns false (no-op) when the cell already holds an IDENTICAL tile
		//! (same prefab + suppressed set + stamps, or same bare asset id) - so
		//! dragging across a painted cell does not churn the undo stack. Pass the
		//! stroke's merge session so consecutive cells collapse into one undo
		//! step. Requires a booted engine.
		bool paintTileAtCell(EditorPaintDesc const& desc,
			float centerX, float centerY, float cellSize,
			unsigned int mergeSession = 0);
		//! @brief erase the tile in the cell centered at (centerX, centerY) as
		//! one undoable step (DeleteSubtreeCommand), whatever kind it is; false
		//! (no-op) when the cell is free. Pass the stroke's merge session.
		bool eraseTileAtCell(float centerX, float centerY, float cellSize,
			unsigned int mergeSession = 0);
		//! @brief would painting desc onto the existing tile occupantId be a
		//! no-op (the occupant already IS that tile)? Prefab: same prefab ref,
		//! suppressed set and stamps; bare tile: same source-asset id (or bare
		//! name) and matching visual component for the kind. Drives the
		//! drag-across-a-cell no-op in paintTileAtCell.
		bool tileCellIsIdentical(String const& occupantId,
			EditorPaintDesc const& desc) const;

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
		// (applyCameraChange / applySpriteChange retired - the
		// Inspector and MCP edit Camera/Sprite through applyPropertyChange.)

		//--- generic reflected property edit ----
		//! @brief read a reflected property (schema-driven) as its canonical
		//! string form; false if the object/component/property is missing or the
		//! property has no getter. componentTypeName is the component type name
		//! (the schema key half), propertyName the PropertyDesc name.
		bool getObjectProperty(String const& id, String const& componentTypeName,
			String const& propertyName, String& outValue) const;
		//! @brief write a reflected property from its canonical string, routing
		//! through the reflected setter (the component's real accessor - the
		//! change takes effect live: a sprite reloads its texture, a transform
		//! moves the node). false if the object/component/property is missing,
		//! the property is read-only, or the string does not parse for the kind.
		//! A Quat value is re-normalized before it is applied (an inspector drag
		//! can send an unnormalized quaternion). Raw apply WITHOUT a command
		//! (PropertyChangeCommand calls this).
		bool setObjectProperty(String const& id, String const& componentTypeName,
			String const& propertyName, String const& value);
		//! @brief record a before/after reflected property change as ONE undoable
		//! PropertyChangeCommand (the auto Inspector's edit path); pass a merge
		//! session id to collapse a whole slider drag into one undo step.
		bool applyPropertyChange(String const& id,
			String const& componentTypeName, String const& propertyName,
			String const& before, String const& after,
			unsigned int mergeSession = 0);
		//! @brief the FULL property schema of a live component: static per-type
		//! UNION the dynamic per-instance schema - so the auto
		//! Inspector lists a ScriptComponent's exported script properties too.
		//! Empty when the object/component is missing.
		PropertySchema getComponentPropertySchema(String const& id,
			String const& componentTypeName) const;

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

		//--- script transaction (editor scripts: one run = one undo step) ---
		// The merge-session idiom collapses HOMOGENEOUS mergeable commands of one
		// interactive drag; an editor SCRIPT applies HETEROGENEOUS commands
		// (create + set + paint ...) that do not merge, so it brackets its whole
		// run in a transaction that folds every command executed in between into a
		// single CompositeCommand undo step - the same primitive multi-select
		// delete/duplicate already use.
		//! @brief begin a script transaction: every command executed until
		//! endScriptTransaction is grouped. Not nestable (asserts if already open).
		void beginScriptTransaction();
		//! @brief close a script transaction. commit=true folds every command
		//! executed since begin into ONE undo step (a CompositeCommand labelled
		//! `description`); a no-op run (nothing executed) leaves the stack
		//! untouched. commit=false (a failed run) UNEXECUTES every command executed
		//! since begin, in reverse, and drops them - so a failed script leaves NO
		//! partial edits. Returns how many commands were folded / rolled back.
		std::size_t endScriptTransaction(bool commit, String const& description);
		//! is a script transaction currently open
		bool inScriptTransaction() const { return mInScriptTransaction; }
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
		//! @brief create a GameObject carrying a sprite through
		//! TransformComponent + SpriteComponent (loadSprite(textureName), NOT
		//! undoable - CreateSpriteObjectCommand wraps it). A missing texture
		//! only logs (the sprite stays empty); requires a booted engine.
		bool instantiateSpriteObject(String const& id, String const& textureName,
			Vec3 const& position);
		//! @brief create a GameObject carrying a flat-colour vector shape through
		//! TransformComponent + VectorShapeComponent (loadShape(shapeName), NOT
		//! undoable - CreateVectorShapeObjectCommand wraps it). A missing/malformed
		//! shape only logs (the shape stays empty); requires a booted engine.
		bool instantiateVectorShapeObject(String const& id, String const& shapeName,
			Vec3 const& position);
		//! @brief create a NEW prefab instance from a .oprefab file: the
		//! deterministic instance-namespace subtree plus the marked root at the
		//! given position (NOT undoable - CreatePrefabInstanceCommand wraps it).
		//! suppressedChildren (optional) drops those prefab-local children right
		//! at instantiation and records them on the root. On a failed
		//! instantiate the partial subtree is torn down again. Requires a booted
		//! engine.
		bool instantiatePrefabInstance(String const& instanceRootId,
			String const& prefabFilePath, String const& prefabRef,
			String const& prefabAssetId, Vec3 const& position,
			StringVector const& suppressedChildren = StringVector());
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
		//! script-transaction bracket (@see beginScriptTransaction): the undo
		//! stack size captured at begin - commands executed after it are folded
		//! into one step (commit) or rolled back (abort) at end
		bool mInScriptTransaction = false;
		std::size_t mScriptTransactionMark = 0;
		//! the open project's collision layers (dropdown source; default =
		//! single "Default" layer) - the editor reads this, never simulates
		PhysicsWorld::LayerConfig mPhysicsLayers;
	};
}
