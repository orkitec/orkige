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
// Anything that touches TransformComponent/ModelComponent needs live Ogre
// scene nodes and is exercised by the editor_edittest integration run.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <core_game/GameObjectManager.h>

#include <OgreVector.h>
#include <OgreQuaternion.h>

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
		Ogre::Vector3 position = Ogre::Vector3::ZERO;
		Ogre::Quaternion orientation = Ogre::Quaternion::IDENTITY;
		Ogre::Vector3 scale = Ogre::Vector3::UNIT_SCALE;
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
		String mXml;	//!< the captured archive content
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
			Ogre::Vector3 const& position);
		virtual bool execute(EditorCore& core) override;
		virtual bool unexecute(EditorCore& core) override;
		virtual String getDescription() const override;

	private:
		String mObjectId;
		String mMeshName;
		Ogre::Vector3 mPosition;
	};

	//! delete an object; undo restores the full serialized component state
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
		static const Ogre::Vector3 DUPLICATE_OFFSET;

		explicit EditorCore(GameObjectManager& gameObjectManager);

		GameObjectManager& getGameObjectManager() { return mGameObjectManager; }

		//--- selection ---------------------------------------
		String const& getSelectedObjectId() const { return mSelectedObjectId; }
		//! true if something is selected AND it still exists
		bool hasSelection() const;
		bool isSelected(String const& id) const { return mSelectedObjectId == id; }
		void selectObject(String const& id) { mSelectedObjectId = id; }
		void clearSelection() { mSelectedObjectId.clear(); }

		//--- tool state --------------------------------------
		EditorTool getActiveTool() const { return mActiveTool; }
		void setActiveTool(EditorTool tool) { mActiveTool = tool; }
		EditorTransformSpace getTransformSpace() const { return mTransformSpace; }
		void setTransformSpace(EditorTransformSpace space) { mTransformSpace = space; }
		void toggleTransformSpace();
		bool isSnapEnabled() const { return mSnapEnabled; }
		void setSnapEnabled(bool enabled) { mSnapEnabled = enabled; }

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
		//! clone the selected object (slightly offset, copy selected)
		bool duplicateSelected();
		//! delete the selected object (undo restores full component state)
		bool deleteSelected();
		//! rename an object (validateRename rules apply)
		bool renameObject(String const& id, String const& newId);
		//! record a before/after transform change as one undoable command;
		//! pass a merge session id to collapse a whole drag into one step
		bool applyTransformChange(String const& id,
			EditorTransform const& before, EditorTransform const& after,
			unsigned int mergeSession = 0);

		//--- transform access (engine-touching helpers) ------
		//! read the object's TransformComponent; false if object/component missing
		bool getObjectTransform(String const& id, EditorTransform& out) const;
		//! raw transform apply WITHOUT a command (commands call this)
		bool setObjectTransform(String const& id, EditorTransform const& transform);

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
		//! ModelComponent (NOT undoable - boot code and CreateObjectCommand
		//! use it). Requires a booted engine.
		bool instantiateModelObject(String const& id, String const& meshName,
			Ogre::Vector3 const& position);
		//! re-apply the unlit vertex-colour render state after a (re)load -
		//! ModelComponent does not serialize material tweaks yet. Safe to
		//! call on objects without a ModelComponent (does nothing).
		void applyModelFixups(String const& id);
		//! snapshot+recreate implementation behind RenameObjectCommand
		bool renameNow(String const& oldId, String const& newId);

	private:
		GameObjectManager& mGameObjectManager;
		String mSelectedObjectId;
		EditorTool mActiveTool = EditorTool::Translate;
		EditorTransformSpace mTransformSpace = EditorTransformSpace::World;
		bool mSnapEnabled = false;
		bool mSceneDirty = false;
		std::map<String, int> mNameCounters;	//!< generateObjectId state
		std::vector<optr<EditorCommand>> mUndoStack;
		std::vector<optr<EditorCommand>> mRedoStack;
		unsigned int mNextMergeSession = 1;
	};
}
