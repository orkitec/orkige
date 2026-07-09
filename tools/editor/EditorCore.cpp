// EditorCore - UI-independent editor state + operations (see EditorCore.h).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorCore.h"

#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_gocomponent/ScriptComponent.h>
#include <engine_render/RenderWorld.h>
#include <core_game/PrefabSerializer.h>
#include <core_serialization/XMLArchive.h>

#include <algorithm>
#include <exception>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <process.h>	// _getpid (snapshot temp file names)
#else
#include <unistd.h>		// getpid (snapshot temp file names)
#endif

namespace Orkige
{
	namespace
	{
		//! unique short-lived temp file for one snapshot round-trip. The name
		//! carries the PROCESS id besides the counter: multiple editor (or,
		//! in practice, parallel ctest) processes share the temp directory,
		//! and a counter-only name let one process's RAII cleanup delete the
		//! file another process was still round-tripping through.
		std::string makeSnapshotTempPath()
		{
			static std::atomic<unsigned int> counter{ 0 };
#ifdef _WIN32
			const unsigned long processId =
				static_cast<unsigned long>(_getpid());
#else
			const unsigned long processId = static_cast<unsigned long>(getpid());
#endif
			const std::string name = "orkige_editor_snapshot_" +
				std::to_string(processId) + "_" +
				std::to_string(++counter) + ".xml";
			return (std::filesystem::temp_directory_path() / name).string();
		}

		//! RAII removal of the snapshot temp file
		struct TempFileGuard
		{
			std::string path;
			~TempFileGuard()
			{
				std::error_code ignored;
				std::filesystem::remove(this->path, ignored);
			}
		};
	}

	//---------------------------------------------------------
	//--- EditorObjectSnapshot ---------------------------------
	//---------------------------------------------------------
	bool EditorObjectSnapshot::capture(GameObjectManager& gameObjectManager,
		String const& id)
	{
		optr<GameObject> gameObject =
			gameObjectManager.getGameObject(id).lock();
		if (!gameObject)
		{
			return false;
		}
		TempFileGuard tempFile{ makeSnapshotTempPath() };
		{
			// the exact per-object serialization SceneSerializer::saveScene
			// performs: component count, then per component the type name
			// followed by the component's serialized state
			optr<XMLArchive> ar = onew(new XMLArchive());
			if (!ar->startWriting(tempFile.path))
			{
				return false;
			}
			GameObject::ComponentMap const& components =
				gameObject->getComponents();
			unsigned int componentCount =
				static_cast<unsigned int>(components.size());
			ar << componentCount;
			for (auto const& [componentType, component] : components)
			{
				String componentTypeName = componentType.getName();
				ar << componentTypeName;
				ar->write(static_cast<ISerializeable&>(*component));
			}
			if (!ar->stopWriting())
			{
				return false;
			}
		}
		// the hierarchy state travels NEXT TO the component archive: the
		// parent is a link to another object (not component state) and the
		// active flag lives on the GameObject itself
		mParentId = gameObject->getParentId();
		mActiveSelf = gameObject->isActiveSelf();
		// tags travel with the object too (rename = serialize + recreate under a
		// new id, so the tags must be re-applied - which re-registers them in
		// the manager's tag index under the new id)
		mTags = gameObject->getTags();
		// keep the archive content in memory, the file is gone after this
		std::ifstream file(tempFile.path, std::ios::binary);
		std::ostringstream content;
		content << file.rdbuf();
		mXml = content.str();
		return !mXml.empty();
	}
	//---------------------------------------------------------
	bool EditorObjectSnapshot::restore(GameObjectManager& gameObjectManager,
		String const& id) const
	{
		if (mXml.empty() || gameObjectManager.objectExists(id))
		{
			return false;
		}
		TempFileGuard tempFile{ makeSnapshotTempPath() };
		{
			std::ofstream file(tempFile.path, std::ios::binary);
			file << mXml;
			if (!file.good())
			{
				return false;
			}
		}
		optr<XMLArchive> ar = onew(new XMLArchive());
		if (!ar->startReading(tempFile.path))
		{
			return false;
		}
		optr<GameObject> gameObject =
			gameObjectManager.createGameObject(id).lock();
		if (!gameObject)
		{
			ar->stopReading();
			return false;
		}
		bool loaded = true;
		unsigned int componentCount = 0;
		ar >> componentCount;
		for (unsigned int componentIndex = 0;
			componentIndex < componentCount && loaded; ++componentIndex)
		{
			String componentTypeName;
			ar >> componentTypeName;
			TypeInfo componentType(componentTypeName);
			if (!GameObject::isComponentRegistered(componentType))
			{
				oDebugMsg("editor", 0, "EditorObjectSnapshot: component type \""
					<< componentTypeName << "\" is not registered - cannot "
					"restore object: " << id);
				loaded = false;
				break;
			}
			// dependencies may already have added this component
			if (!gameObject->hasComponent(componentType))
			{
				if (!gameObject->addComponent(componentType))
				{
					loaded = false;
					break;
				}
			}
			GameObjectComponent* component =
				gameObject->getComponentPtr(componentType);
			oAssert(component);
			ar->read(static_cast<ISerializeable&>(*component));
		}
		ar->stopReading();
		if (!loaded)
		{
			// don't leave a half-restored object behind
			gameObjectManager.delGameObject(id);
			return false;
		}
		// re-attach where the object was: the restored LOCAL transform is
		// relative to that parent (keepWorldTransform=false, the scene-load
		// semantics). A vanished parent leaves the object a root - honest.
		if (!mParentId.empty())
		{
			if (gameObjectManager.objectExists(mParentId))
			{
				gameObject->setParent(mParentId, false);
			}
			else
			{
				oDebugMsg("editor", 0, "EditorObjectSnapshot: parent \""
					<< mParentId << "\" of restored object " << id
					<< " no longer exists - restored as a root");
			}
		}
		gameObject->setActive(mActiveSelf);
		gameObject->setTags(mTags);
		return true;
	}

	//---------------------------------------------------------
	//--- EditorComponentSnapshot ------------------------------
	//---------------------------------------------------------
	bool EditorComponentSnapshot::capture(GameObjectManager& gameObjectManager,
		String const& id, String const& componentTypeName)
	{
		optr<GameObject> gameObject =
			gameObjectManager.getGameObject(id).lock();
		if (!gameObject)
		{
			return false;
		}
		TypeInfo componentType(componentTypeName);
		if (!gameObject->hasComponent(componentType))
		{
			return false;
		}
		GameObjectComponent* component =
			gameObject->getComponentPtr(componentType);
		oAssert(component);
		TempFileGuard tempFile{ makeSnapshotTempPath() };
		{
			optr<XMLArchive> ar = onew(new XMLArchive());
			if (!ar->startWriting(tempFile.path))
			{
				return false;
			}
			ar->write(static_cast<ISerializeable&>(*component));
			if (!ar->stopWriting())
			{
				return false;
			}
		}
		std::ifstream file(tempFile.path, std::ios::binary);
		std::ostringstream content;
		content << file.rdbuf();
		mXml = content.str();
		mComponentTypeName = componentTypeName;
		return !mXml.empty();
	}
	//---------------------------------------------------------
	bool EditorComponentSnapshot::restore(GameObjectManager& gameObjectManager,
		String const& id) const
	{
		if (mXml.empty())
		{
			return false;
		}
		optr<GameObject> gameObject =
			gameObjectManager.getGameObject(id).lock();
		TypeInfo componentType(mComponentTypeName);
		if (!gameObject || gameObject->hasComponent(componentType) ||
			!GameObject::isComponentRegistered(componentType))
		{
			return false;
		}
		TempFileGuard tempFile{ makeSnapshotTempPath() };
		{
			std::ofstream file(tempFile.path, std::ios::binary);
			file << mXml;
			if (!file.good())
			{
				return false;
			}
		}
		optr<XMLArchive> ar = onew(new XMLArchive());
		if (!ar->startReading(tempFile.path))
		{
			return false;
		}
		// the component's own dependencies are still attached (they were
		// there when it was captured); addComponent re-adds any missing ones
		if (!gameObject->addComponent(componentType))
		{
			ar->stopReading();
			return false;
		}
		GameObjectComponent* component =
			gameObject->getComponentPtr(componentType);
		oAssert(component);
		ar->read(static_cast<ISerializeable&>(*component));
		ar->stopReading();
		return true;
	}

	//---------------------------------------------------------
	//--- TransformChangeCommand -------------------------------
	//---------------------------------------------------------
	TransformChangeCommand::TransformChangeCommand(String const& objectId,
		EditorTransform const& before, EditorTransform const& after)
		: mObjectId(objectId), mBefore(before), mAfter(after)
	{
	}
	//---------------------------------------------------------
	bool TransformChangeCommand::execute(EditorCore& core)
	{
		return core.setObjectTransform(mObjectId, mAfter);
	}
	//---------------------------------------------------------
	bool TransformChangeCommand::unexecute(EditorCore& core)
	{
		return core.setObjectTransform(mObjectId, mBefore);
	}
	//---------------------------------------------------------
	String TransformChangeCommand::getDescription() const
	{
		return "Transform " + mObjectId;
	}
	//---------------------------------------------------------
	bool TransformChangeCommand::mergeWith(EditorCommand const& next)
	{
		TransformChangeCommand const* other =
			dynamic_cast<TransformChangeCommand const*>(&next);
		if (!other || other->mObjectId != mObjectId)
		{
			return false;
		}
		// keep my drag-start "before", absorb the newest "after"
		mAfter = other->mAfter;
		return true;
	}

	//---------------------------------------------------------
	//--- CreateObjectCommand ----------------------------------
	//---------------------------------------------------------
	CreateObjectCommand::CreateObjectCommand(String const& objectId,
		String const& meshName, Vec3 const& position)
		: mObjectId(objectId), mMeshName(meshName), mPosition(position)
	{
	}
	//---------------------------------------------------------
	bool CreateObjectCommand::execute(EditorCore& core)
	{
		if (!core.instantiateModelObject(mObjectId, mMeshName, mPosition))
		{
			return false;
		}
		core.selectObject(mObjectId);
		return true;
	}
	//---------------------------------------------------------
	bool CreateObjectCommand::unexecute(EditorCore& core)
	{
		core.deselectObject(mObjectId);
		return core.getGameObjectManager().delGameObject(mObjectId);
	}
	//---------------------------------------------------------
	String CreateObjectCommand::getDescription() const
	{
		return "Create " + mObjectId;
	}

	//---------------------------------------------------------
	//--- DeleteObjectCommand ----------------------------------
	//---------------------------------------------------------
	DeleteObjectCommand::DeleteObjectCommand(String const& objectId)
		: mObjectId(objectId)
	{
	}
	//---------------------------------------------------------
	bool DeleteObjectCommand::execute(EditorCore& core)
	{
		// captured fresh on every execute so redo restores the state the
		// object had when it was deleted again
		if (!mSnapshot.capture(core.getGameObjectManager(), mObjectId))
		{
			return false;
		}
		mWasSelected = core.isSelected(mObjectId);
		// direct children survive the delete re-parented to the grandparent
		// (GameObjectManager::delGameObject semantics) - remember them so
		// undo can re-attach them
		mChildIds = core.getGameObjectManager().getChildren(mObjectId);
		if (!core.getGameObjectManager().delGameObject(mObjectId))
		{
			return false;
		}
		core.deselectObject(mObjectId);
		return true;
	}
	//---------------------------------------------------------
	bool DeleteObjectCommand::unexecute(EditorCore& core)
	{
		if (!mSnapshot.restore(core.getGameObjectManager(), mObjectId))
		{
			return false;
		}
		core.applyModelFixups(mObjectId);
		// re-attach the children that moved to the grandparent on delete
		// (world transforms preserved, so nothing moves visually)
		for (String const& childId : mChildIds)
		{
			optr<GameObject> child = core.getGameObjectManager()
				.getGameObject(childId).lock();
			if (child)
			{
				child->setParent(mObjectId, true);
			}
		}
		if (mWasSelected)
		{
			core.addToSelection(mObjectId);
		}
		return true;
	}
	//---------------------------------------------------------
	String DeleteObjectCommand::getDescription() const
	{
		return "Delete " + mObjectId;
	}

	//---------------------------------------------------------
	//--- RenameObjectCommand ----------------------------------
	//---------------------------------------------------------
	RenameObjectCommand::RenameObjectCommand(String const& oldId,
		String const& newId)
		: mOldId(oldId), mNewId(newId)
	{
	}
	//---------------------------------------------------------
	bool RenameObjectCommand::execute(EditorCore& core)
	{
		return core.renameNow(mOldId, mNewId);
	}
	//---------------------------------------------------------
	bool RenameObjectCommand::unexecute(EditorCore& core)
	{
		return core.renameNow(mNewId, mOldId);
	}
	//---------------------------------------------------------
	String RenameObjectCommand::getDescription() const
	{
		return "Rename " + mOldId + " to " + mNewId;
	}

	//---------------------------------------------------------
	//--- DuplicateObjectCommand -------------------------------
	//---------------------------------------------------------
	DuplicateObjectCommand::DuplicateObjectCommand(String const& sourceId,
		String const& newId)
		: mSourceId(sourceId), mNewId(newId)
	{
	}
	//---------------------------------------------------------
	bool DuplicateObjectCommand::execute(EditorCore& core)
	{
		EditorObjectSnapshot snapshot;
		if (!snapshot.capture(core.getGameObjectManager(), mSourceId) ||
			!snapshot.restore(core.getGameObjectManager(), mNewId))
		{
			return false;
		}
		core.applyModelFixups(mNewId);
		// nudge the copy so it does not sit inside the source
		EditorTransform transform;
		if (core.getObjectTransform(mNewId, transform))
		{
			transform.position += EditorCore::DUPLICATE_OFFSET;
			core.setObjectTransform(mNewId, transform);
		}
		// selection moves from the source to its copy (a multi-select batch
		// thereby ends up with ALL copies selected)
		core.deselectObject(mSourceId);
		core.addToSelection(mNewId);
		return true;
	}
	//---------------------------------------------------------
	bool DuplicateObjectCommand::unexecute(EditorCore& core)
	{
		if (!core.getGameObjectManager().delGameObject(mNewId))
		{
			return false;
		}
		if (core.isSelected(mNewId))
		{
			core.deselectObject(mNewId);
			core.addToSelection(mSourceId);
		}
		return true;
	}
	//---------------------------------------------------------
	String DuplicateObjectCommand::getDescription() const
	{
		return "Duplicate " + mSourceId;
	}

	//---------------------------------------------------------
	//--- ReparentObjectCommand --------------------------------
	//---------------------------------------------------------
	ReparentObjectCommand::ReparentObjectCommand(String const& objectId,
		String const& newParentId)
		: mObjectId(objectId), mNewParentId(newParentId)
	{
	}
	//---------------------------------------------------------
	bool ReparentObjectCommand::execute(EditorCore& core)
	{
		optr<GameObject> gameObject = core.getGameObjectManager()
			.getGameObject(mObjectId).lock();
		if (!gameObject)
		{
			return false;
		}
		mOldParentId = gameObject->getParentId();
		if (mOldParentId == mNewParentId)
		{
			return false;	// no-op re-parent never enters the undo stack
		}
		// the exact local transform, so undo restores it bit-for-bit even if
		// something moved in between
		mHadTransform = core.getObjectTransform(mObjectId, mOldLocal);
		// world transform preserved (setParent refuses cycles/self/unknown)
		return gameObject->setParent(mNewParentId, true);
	}
	//---------------------------------------------------------
	bool ReparentObjectCommand::unexecute(EditorCore& core)
	{
		optr<GameObject> gameObject = core.getGameObjectManager()
			.getGameObject(mObjectId).lock();
		if (!gameObject || !gameObject->setParent(mOldParentId, true))
		{
			return false;
		}
		if (mHadTransform)
		{
			core.setObjectTransform(mObjectId, mOldLocal);
		}
		return true;
	}
	//---------------------------------------------------------
	String ReparentObjectCommand::getDescription() const
	{
		return mNewParentId.empty()
			? ("Unparent " + mObjectId)
			: ("Parent " + mObjectId + " to " + mNewParentId);
	}

	//---------------------------------------------------------
	//--- SetActiveObjectCommand -------------------------------
	//---------------------------------------------------------
	SetActiveObjectCommand::SetActiveObjectCommand(String const& objectId,
		bool active)
		: mObjectId(objectId), mActive(active)
	{
	}
	//---------------------------------------------------------
	bool SetActiveObjectCommand::execute(EditorCore& core)
	{
		optr<GameObject> gameObject = core.getGameObjectManager()
			.getGameObject(mObjectId).lock();
		if (!gameObject)
		{
			return false;
		}
		mBefore = gameObject->isActiveSelf();
		if (mBefore == mActive)
		{
			return false;	// no-op toggles never enter the undo stack
		}
		gameObject->setActive(mActive);
		return true;
	}
	//---------------------------------------------------------
	bool SetActiveObjectCommand::unexecute(EditorCore& core)
	{
		optr<GameObject> gameObject = core.getGameObjectManager()
			.getGameObject(mObjectId).lock();
		if (!gameObject)
		{
			return false;
		}
		gameObject->setActive(mBefore);
		return true;
	}
	//---------------------------------------------------------
	String SetActiveObjectCommand::getDescription() const
	{
		return (mActive ? "Activate " : "Deactivate ") + mObjectId;
	}

	//---------------------------------------------------------
	//--- SetTagsCommand ---------------------------------------
	//---------------------------------------------------------
	SetTagsCommand::SetTagsCommand(String const& objectId,
		StringVector const& tags)
		: mObjectId(objectId), mTags(tags)
	{
	}
	//---------------------------------------------------------
	bool SetTagsCommand::execute(EditorCore& core)
	{
		optr<GameObject> gameObject = core.getGameObjectManager()
			.getGameObject(mObjectId).lock();
		if (!gameObject)
		{
			return false;
		}
		mBefore = gameObject->getTags();
		gameObject->setTags(mTags);
		// a no-op change (setTags cleaned to the same set) never enters undo
		if (gameObject->getTags() == mBefore)
		{
			return false;
		}
		return true;
	}
	//---------------------------------------------------------
	bool SetTagsCommand::unexecute(EditorCore& core)
	{
		optr<GameObject> gameObject = core.getGameObjectManager()
			.getGameObject(mObjectId).lock();
		if (!gameObject)
		{
			return false;
		}
		gameObject->setTags(mBefore);
		return true;
	}
	//---------------------------------------------------------
	String SetTagsCommand::getDescription() const
	{
		return "Set tags on " + mObjectId;
	}

	//---------------------------------------------------------
	//--- GroupObjectsCommand ----------------------------------
	//---------------------------------------------------------
	GroupObjectsCommand::GroupObjectsCommand(StringVector const& memberIds,
		String const& groupId)
		: mMemberIds(memberIds), mGroupId(groupId)
	{
	}
	//---------------------------------------------------------
	bool GroupObjectsCommand::execute(EditorCore& core)
	{
		GameObjectManager& manager = core.getGameObjectManager();
		if (mMemberIds.empty() || manager.objectExists(mGroupId))
		{
			return false;
		}
		for (String const& memberId : mMemberIds)
		{
			if (!manager.objectExists(memberId))
			{
				return false;
			}
		}
		// the group inherits the members' COMMON parent (mixed parents = root)
		// so grouping never changes where the subtree lives in the tree
		mOldParentIds.clear();
		String commonParentId =
			manager.getGameObject(mMemberIds.front()).lock()->getParentId();
		for (String const& memberId : mMemberIds)
		{
			const String parentId =
				manager.getGameObject(memberId).lock()->getParentId();
			mOldParentIds.push_back(parentId);
			if (parentId != commonParentId)
			{
				commonParentId.clear();
			}
		}
		// pivot: the average of the members' world positions - like Unity's
		// "create empty parent", the group sits at the selection's centre
		Vec3 centre = Vec3::ZERO;
		int transformCount = 0;
		for (String const& memberId : mMemberIds)
		{
			optr<GameObject> member = manager.getGameObject(memberId).lock();
			if (member->hasComponent<TransformComponent>())
			{
				centre += member->getComponentPtr<TransformComponent>()
					->getWorldPosition();
				++transformCount;
			}
		}
		if (transformCount > 0)
		{
			centre /= static_cast<float>(transformCount);
		}
		optr<GameObject> group = manager.createGameObject(mGroupId).lock();
		if (!group)
		{
			return false;
		}
		// a live editor always has TransformComponent registered; the
		// headless editor_core tests group bare objects (logically identical,
		// no transform composition without a render scene anyway)
		if (GameObject::isComponentRegistered(
			TransformComponent::getClassTypeInfo()))
		{
			if (!group->addComponent<TransformComponent>())
			{
				manager.delGameObject(mGroupId);
				return false;
			}
		}
		if (!commonParentId.empty())
		{
			group->setParent(commonParentId, false);
		}
		if (group->hasComponent<TransformComponent>())
		{
			group->getComponentPtr<TransformComponent>()
				->setWorldPosition(centre);
		}
		for (String const& memberId : mMemberIds)
		{
			// cannot fail: the members exist and the fresh group is nobody's
			// descendant - but stay honest if it ever does
			optr<GameObject> member = manager.getGameObject(memberId).lock();
			if (!member->setParent(mGroupId, true))
			{
				oDebugMsg("editor", 0, "GroupObjectsCommand: could not parent "
					<< memberId << " under " << mGroupId);
			}
		}
		core.clearSelection();
		core.addToSelection(mGroupId);
		return true;
	}
	//---------------------------------------------------------
	bool GroupObjectsCommand::unexecute(EditorCore& core)
	{
		GameObjectManager& manager = core.getGameObjectManager();
		if (!manager.objectExists(mGroupId))
		{
			return false;
		}
		for (std::size_t index = 0; index < mMemberIds.size(); ++index)
		{
			optr<GameObject> member =
				manager.getGameObject(mMemberIds[index]).lock();
			if (member)
			{
				member->setParent(mOldParentIds[index], true);
			}
		}
		if (!manager.delGameObject(mGroupId))
		{
			return false;
		}
		core.clearSelection();
		for (String const& memberId : mMemberIds)
		{
			if (manager.objectExists(memberId))
			{
				core.addToSelection(memberId);
			}
		}
		return true;
	}
	//---------------------------------------------------------
	String GroupObjectsCommand::getDescription() const
	{
		return "Group " + std::to_string(mMemberIds.size()) +
			(mMemberIds.size() == 1 ? " Object" : " Objects");
	}

	//---------------------------------------------------------
	//--- MakePrefabCommand ------------------------------------
	//---------------------------------------------------------
	MakePrefabCommand::MakePrefabCommand(String const& rootId,
		String const& prefabFilePath, String const& prefabRef,
		String const& prefabAssetId)
		: mRootId(rootId), mPrefabFilePath(prefabFilePath),
		mPrefabRef(prefabRef), mPrefabAssetId(prefabAssetId)
	{
	}
	//---------------------------------------------------------
	bool MakePrefabCommand::execute(EditorCore& core)
	{
		GameObjectManager& manager = core.getGameObjectManager();
		optr<GameObject> root = manager.getGameObject(mRootId).lock();
		// converting an existing instance again is a plain re-make of its
		// file - it never reaches this command (see EditorCore::canMakePrefab
		// and the createPrefabFromSelection shell flow)
		if (!root || !root->getPrefabRef().empty())
		{
			return false;
		}
		// captured fresh on every execute so redo converts the state the
		// subtree had when it was converted again
		mOldChildIds.clear();
		mOldChildSnapshots.clear();
		const StringVector subtree = manager.collectSubtreeIds(mRootId);
		for (String const& id : subtree)
		{
			if (id == mRootId)
			{
				continue;
			}
			EditorObjectSnapshot snapshot;
			if (!snapshot.capture(manager, id))
			{
				return false;
			}
			mOldChildIds.push_back(id);
			mOldChildSnapshots.push_back(snapshot);
		}
		// swap the live children for the instance-namespace objects the just
		// written prefab file provides (identical state, deterministic ids)
		for (auto it = mOldChildIds.rbegin(); it != mOldChildIds.rend(); ++it)
		{
			manager.delGameObject(*it);
			core.deselectObject(*it);
		}
		if (PrefabSerializer::instantiatePrefab(mPrefabFilePath, manager,
			mRootId, StringVector()) != PrefabSerializer::INSTANTIATE_OK)
		{
			oDebugMsg("editor", 0, "MakePrefabCommand: instantiating '"
				<< mPrefabFilePath << "' failed - restoring the original "
				"children of " << mRootId);
			restoreOriginalChildren(core);
			return false;
		}
		for (String const& id : manager.collectSubtreeIds(mRootId))
		{
			core.applyModelFixups(id);
		}
		root->setPrefabRef(mPrefabRef, mPrefabAssetId);
		root->setSuppressedPrefabChildren(StringVector());
		core.selectObject(mRootId);
		return true;
	}
	//---------------------------------------------------------
	bool MakePrefabCommand::unexecute(EditorCore& core)
	{
		GameObjectManager& manager = core.getGameObjectManager();
		optr<GameObject> root = manager.getGameObject(mRootId).lock();
		if (!root)
		{
			return false;
		}
		// remove the instance-namespace children (deepest first), unmark the
		// root, then bring the original children back from the snapshots
		const StringVector subtree = manager.collectSubtreeIds(mRootId);
		for (auto it = subtree.rbegin(); it != subtree.rend(); ++it)
		{
			if (*it != mRootId)
			{
				manager.delGameObject(*it);
				core.deselectObject(*it);
			}
		}
		root->setPrefabRef("", "");
		return restoreOriginalChildren(core);
	}
	//---------------------------------------------------------
	String MakePrefabCommand::getDescription() const
	{
		return "Create Prefab from " + mRootId;
	}
	//---------------------------------------------------------
	bool MakePrefabCommand::restoreOriginalChildren(EditorCore& core) const
	{
		bool restored = true;
		for (std::size_t index = 0; index < mOldChildIds.size(); ++index)
		{
			if (!mOldChildSnapshots[index].restore(core.getGameObjectManager(),
				mOldChildIds[index]))
			{
				oDebugMsg("editor", 0, "MakePrefabCommand: could not restore "
					"original child " << mOldChildIds[index]);
				restored = false;
				continue;
			}
			core.applyModelFixups(mOldChildIds[index]);
		}
		return restored;
	}

	//---------------------------------------------------------
	//--- CompositeCommand -------------------------------------
	//---------------------------------------------------------
	CompositeCommand::CompositeCommand(String const& description)
		: mDescription(description)
	{
	}
	//---------------------------------------------------------
	void CompositeCommand::addCommand(optr<EditorCommand> const& command)
	{
		oAssert(command);
		mCommands.push_back(command);
	}
	//---------------------------------------------------------
	bool CompositeCommand::execute(EditorCore& core)
	{
		if (mCommands.empty())
		{
			return false;
		}
		for (std::size_t i = 0; i < mCommands.size(); ++i)
		{
			if (!mCommands[i]->execute(core))
			{
				// roll the already-executed prefix back so a refused batch
				// leaves the scene untouched
				for (std::size_t j = i; j-- > 0;)
				{
					mCommands[j]->unexecute(core);
				}
				return false;
			}
		}
		return true;
	}
	//---------------------------------------------------------
	bool CompositeCommand::unexecute(EditorCore& core)
	{
		bool undone = true;
		for (std::size_t i = mCommands.size(); i-- > 0;)
		{
			undone = mCommands[i]->unexecute(core) && undone;
		}
		return undone;
	}
	//---------------------------------------------------------
	String CompositeCommand::getDescription() const
	{
		return mDescription;
	}

	//---------------------------------------------------------
	//--- AddComponentCommand ----------------------------------
	//---------------------------------------------------------
	AddComponentCommand::AddComponentCommand(String const& objectId,
		String const& componentTypeName)
		: mObjectId(objectId), mComponentTypeName(componentTypeName)
	{
	}
	//---------------------------------------------------------
	bool AddComponentCommand::execute(EditorCore& core)
	{
		optr<GameObject> gameObject =
			core.getGameObjectManager().getGameObject(mObjectId).lock();
		TypeInfo componentType(mComponentTypeName);
		if (!gameObject || !GameObject::isComponentRegistered(componentType) ||
			gameObject->hasComponent(componentType))
		{
			return false;
		}
		// the holder auto-adds missing dependencies - diff the attached set
		// so undo can take exactly the components this command brought in
		const TypeInfoList before = gameObject->getAttachedComponentTypes();
		if (!gameObject->addComponent(componentType))
		{
			return false;
		}
		mAddedTypeNames.clear();
		mAddedTypeNames.push_back(mComponentTypeName);
		for (TypeInfo const& attached : gameObject->getAttachedComponentTypes())
		{
			if (!(attached == componentType) &&
				std::find(before.begin(), before.end(), attached) ==
					before.end())
			{
				mAddedTypeNames.push_back(attached.getName());
			}
		}
		return true;
	}
	//---------------------------------------------------------
	bool AddComponentCommand::unexecute(EditorCore& core)
	{
		optr<GameObject> gameObject =
			core.getGameObjectManager().getGameObject(mObjectId).lock();
		if (!gameObject)
		{
			return false;
		}
		// the requested type goes first; removeComponent cascade-removes
		// dependents, so later entries may already be gone - check each time
		for (String const& typeName : mAddedTypeNames)
		{
			TypeInfo componentType(typeName);
			if (gameObject->hasComponent(componentType))
			{
				gameObject->removeComponent(componentType);
			}
		}
		for (String const& typeName : mAddedTypeNames)
		{
			if (gameObject->hasComponent(TypeInfo(typeName)))
			{
				return false;
			}
		}
		return true;
	}
	//---------------------------------------------------------
	String AddComponentCommand::getDescription() const
	{
		return "Add " + mComponentTypeName + " to " + mObjectId;
	}

	//---------------------------------------------------------
	//--- RemoveComponentCommand -------------------------------
	//---------------------------------------------------------
	RemoveComponentCommand::RemoveComponentCommand(String const& objectId,
		String const& componentTypeName)
		: mObjectId(objectId), mComponentTypeName(componentTypeName)
	{
	}
	//---------------------------------------------------------
	bool RemoveComponentCommand::execute(EditorCore& core)
	{
		// refuse while another attached component depends on this one (the
		// holder would cascade-remove the dependents; the editor blocks the
		// operation instead, like Unity does)
		if (!core.canRemoveComponent(mObjectId, mComponentTypeName))
		{
			return false;
		}
		// captured fresh on every execute so redo removes the state the
		// component had when it was removed again
		if (!mSnapshot.capture(core.getGameObjectManager(), mObjectId,
			mComponentTypeName))
		{
			return false;
		}
		optr<GameObject> gameObject =
			core.getGameObjectManager().getGameObject(mObjectId).lock();
		oAssert(gameObject);
		return gameObject->removeComponent(TypeInfo(mComponentTypeName));
	}
	//---------------------------------------------------------
	bool RemoveComponentCommand::unexecute(EditorCore& core)
	{
		if (!mSnapshot.restore(core.getGameObjectManager(), mObjectId))
		{
			return false;
		}
		// a restored ModelComponent reloaded its entity through load()
		core.applyModelFixups(mObjectId);
		return true;
	}
	//---------------------------------------------------------
	String RemoveComponentCommand::getDescription() const
	{
		return "Remove " + mComponentTypeName + " from " + mObjectId;
	}

	//---------------------------------------------------------
	//--- ChangeMeshCommand ------------------------------------
	//---------------------------------------------------------
	ChangeMeshCommand::ChangeMeshCommand(String const& objectId,
		String const& beforeMesh, String const& afterMesh)
		: mObjectId(objectId), mBeforeMesh(beforeMesh), mAfterMesh(afterMesh)
	{
	}
	//---------------------------------------------------------
	bool ChangeMeshCommand::execute(EditorCore& core)
	{
		return core.setObjectMesh(mObjectId, mAfterMesh);
	}
	//---------------------------------------------------------
	bool ChangeMeshCommand::unexecute(EditorCore& core)
	{
		return core.setObjectMesh(mObjectId, mBeforeMesh);
	}
	//---------------------------------------------------------
	String ChangeMeshCommand::getDescription() const
	{
		return "Change Mesh of " + mObjectId;
	}

	//---------------------------------------------------------
	//--- ChangeScriptCommand ----------------------------------
	//---------------------------------------------------------
	ChangeScriptCommand::ChangeScriptCommand(String const& objectId,
		String const& beforeScript, bool beforeEnabled,
		String const& afterScript, bool afterEnabled)
		: mObjectId(objectId), mBeforeScript(beforeScript),
		mBeforeEnabled(beforeEnabled), mAfterScript(afterScript),
		mAfterEnabled(afterEnabled)
	{
	}
	//---------------------------------------------------------
	bool ChangeScriptCommand::execute(EditorCore& core)
	{
		return core.setObjectScript(mObjectId, mAfterScript, mAfterEnabled);
	}
	//---------------------------------------------------------
	bool ChangeScriptCommand::unexecute(EditorCore& core)
	{
		return core.setObjectScript(mObjectId, mBeforeScript, mBeforeEnabled);
	}
	//---------------------------------------------------------
	String ChangeScriptCommand::getDescription() const
	{
		return "Change Script of " + mObjectId;
	}

	//---------------------------------------------------------
	//--- RigidBodyChangeCommand -------------------------------
	//---------------------------------------------------------
	RigidBodyChangeCommand::RigidBodyChangeCommand(String const& objectId,
		PhysicsWorld::BodyDesc const& before,
		PhysicsWorld::BodyDesc const& after)
		: mObjectId(objectId), mBefore(before), mAfter(after)
	{
	}
	//---------------------------------------------------------
	bool RigidBodyChangeCommand::execute(EditorCore& core)
	{
		return core.setRigidBodyDesc(mObjectId, mAfter);
	}
	//---------------------------------------------------------
	bool RigidBodyChangeCommand::unexecute(EditorCore& core)
	{
		return core.setRigidBodyDesc(mObjectId, mBefore);
	}
	//---------------------------------------------------------
	String RigidBodyChangeCommand::getDescription() const
	{
		return "Edit RigidBody " + mObjectId;
	}
	//---------------------------------------------------------
	bool RigidBodyChangeCommand::mergeWith(EditorCommand const& next)
	{
		RigidBodyChangeCommand const* other =
			dynamic_cast<RigidBodyChangeCommand const*>(&next);
		if (!other || other->mObjectId != mObjectId)
		{
			return false;
		}
		// keep my drag-start "before", absorb the newest "after"
		mAfter = other->mAfter;
		return true;
	}

	//---------------------------------------------------------
	//--- CameraChangeCommand ----------------------------------
	//---------------------------------------------------------
	CameraChangeCommand::CameraChangeCommand(String const& objectId,
		EditorCameraSettings const& before, EditorCameraSettings const& after)
		: mObjectId(objectId), mBefore(before), mAfter(after)
	{
	}
	//---------------------------------------------------------
	bool CameraChangeCommand::execute(EditorCore& core)
	{
		return core.setCameraSettings(mObjectId, mAfter);
	}
	//---------------------------------------------------------
	bool CameraChangeCommand::unexecute(EditorCore& core)
	{
		return core.setCameraSettings(mObjectId, mBefore);
	}
	//---------------------------------------------------------
	String CameraChangeCommand::getDescription() const
	{
		return "Edit Camera " + mObjectId;
	}
	//---------------------------------------------------------
	bool CameraChangeCommand::mergeWith(EditorCommand const& next)
	{
		CameraChangeCommand const* other =
			dynamic_cast<CameraChangeCommand const*>(&next);
		if (!other || other->mObjectId != mObjectId)
		{
			return false;
		}
		mAfter = other->mAfter;
		return true;
	}

	//---------------------------------------------------------
	//--- SpriteChangeCommand ----------------------------------
	//---------------------------------------------------------
	SpriteChangeCommand::SpriteChangeCommand(String const& objectId,
		EditorSpriteSettings const& before, EditorSpriteSettings const& after)
		: mObjectId(objectId), mBefore(before), mAfter(after)
	{
	}
	//---------------------------------------------------------
	bool SpriteChangeCommand::execute(EditorCore& core)
	{
		return core.setSpriteSettings(mObjectId, mAfter);
	}
	//---------------------------------------------------------
	bool SpriteChangeCommand::unexecute(EditorCore& core)
	{
		return core.setSpriteSettings(mObjectId, mBefore);
	}
	//---------------------------------------------------------
	String SpriteChangeCommand::getDescription() const
	{
		return "Edit Sprite " + mObjectId;
	}
	//---------------------------------------------------------
	bool SpriteChangeCommand::mergeWith(EditorCommand const& next)
	{
		SpriteChangeCommand const* other =
			dynamic_cast<SpriteChangeCommand const*>(&next);
		if (!other || other->mObjectId != mObjectId)
		{
			return false;
		}
		mAfter = other->mAfter;
		return true;
	}

	//---------------------------------------------------------
	//--- EditorCore -------------------------------------------
	//---------------------------------------------------------
	const float EditorCore::SNAP_TRANSLATE = 0.5f;
	const float EditorCore::SNAP_ROTATE_DEGREES = 15.0f;
	const float EditorCore::SNAP_SCALE = 0.1f;
	const Vec3 EditorCore::DUPLICATE_OFFSET(0.5f, 0.0f, 0.5f);
	//---------------------------------------------------------
	EditorCore::EditorCore(GameObjectManager& gameObjectManager)
		: mGameObjectManager(gameObjectManager),
		mSnapTranslate(EditorCore::SNAP_TRANSLATE),
		mSnapRotateDegrees(EditorCore::SNAP_ROTATE_DEGREES),
		mSnapScale(EditorCore::SNAP_SCALE)
	{
	}
	//---------------------------------------------------------
	void EditorCore::setSnapValues(float translate, float rotateDegrees,
		float scale)
	{
		// a zero/negative step would freeze the gizmo mid-drag - clamp to a
		// sane positive minimum instead of trusting the UI's field values
		mSnapTranslate = std::max(translate, 0.001f);
		mSnapRotateDegrees = std::max(rotateDegrees, 0.1f);
		mSnapScale = std::max(scale, 0.001f);
	}
	//---------------------------------------------------------
	String EditorCore::getSelectedObjectId() const
	{
		return mSelection.empty() ? String() : mSelection.back();
	}
	//---------------------------------------------------------
	bool EditorCore::hasSelection() const
	{
		return !mSelection.empty() &&
			mGameObjectManager.objectExists(mSelection.back());
	}
	//---------------------------------------------------------
	bool EditorCore::isSelected(String const& id) const
	{
		return std::find(mSelection.begin(), mSelection.end(), id) !=
			mSelection.end();
	}
	//---------------------------------------------------------
	void EditorCore::selectObject(String const& id)
	{
		mSelection.clear();
		mSelection.push_back(id);
	}
	//---------------------------------------------------------
	void EditorCore::toggleSelection(String const& id)
	{
		if (isSelected(id))
		{
			deselectObject(id);
		}
		else
		{
			mSelection.push_back(id); // newest member becomes the primary
		}
	}
	//---------------------------------------------------------
	void EditorCore::addToSelection(String const& id)
	{
		if (!isSelected(id))
		{
			mSelection.push_back(id);
		}
	}
	//---------------------------------------------------------
	void EditorCore::deselectObject(String const& id)
	{
		mSelection.erase(
			std::remove(mSelection.begin(), mSelection.end(), id),
			mSelection.end());
	}
	//---------------------------------------------------------
	void EditorCore::toggleTransformSpace()
	{
		mTransformSpace = (mTransformSpace == EditorTransformSpace::World)
			? EditorTransformSpace::Local : EditorTransformSpace::World;
	}
	//---------------------------------------------------------
	String EditorCore::generateObjectId(String const& prefix)
	{
		int& counter = mNameCounters[prefix];
		String id;
		do
		{
			++counter;
			id = prefix + std::to_string(counter);
		} while (mGameObjectManager.objectExists(id));
		return id;
	}
	//---------------------------------------------------------
	String EditorCore::makeDuplicateId(String const& sourceId) const
	{
		const String base = sourceId + " Copy";
		if (!mGameObjectManager.objectExists(base))
		{
			return base;
		}
		int suffix = 2;
		String id;
		do
		{
			id = base + " " + std::to_string(suffix);
			++suffix;
		} while (mGameObjectManager.objectExists(id));
		return id;
	}
	//---------------------------------------------------------
	EditorCore::NameValidation EditorCore::validateRename(
		String const& currentId, String const& newId) const
	{
		if (newId.empty())
		{
			return NameValidation::Empty;
		}
		if (newId == currentId)
		{
			return NameValidation::Unchanged;
		}
		if (mGameObjectManager.objectExists(newId))
		{
			return NameValidation::Exists;
		}
		return NameValidation::Ok;
	}
	//---------------------------------------------------------
	bool EditorCore::createCube()
	{
		const String id = generateObjectId("Cube");
		return executeCommand(onew(new CreateObjectCommand(id,
			RenderWorld::CUBE_MESH_NAME, Vec3::ZERO)));
	}
	//---------------------------------------------------------
	bool EditorCore::createTestMesh()
	{
		const String id = generateObjectId("TestMesh");
		return executeCommand(onew(new CreateObjectCommand(id,
			"test_mesh.glb", Vec3::ZERO)));
	}
	//---------------------------------------------------------
	bool EditorCore::duplicateSelected()
	{
		// only selected ids that still exist take part (the set may carry
		// stale ids of objects that vanished outside the command stack)
		StringVector sourceIds;
		for (String const& id : mSelection)
		{
			if (mGameObjectManager.objectExists(id))
			{
				sourceIds.push_back(id);
			}
		}
		if (sourceIds.empty())
		{
			return false;
		}
		if (sourceIds.size() == 1)
		{
			return executeCommand(onew(new DuplicateObjectCommand(sourceIds[0],
				makeDuplicateId(sourceIds[0]))));
		}
		// multi-select: ONE undo step for the whole batch
		optr<CompositeCommand> batch = onew(new CompositeCommand(
			"Duplicate " + std::to_string(sourceIds.size()) + " Objects"));
		for (String const& id : sourceIds)
		{
			batch->addCommand(onew(new DuplicateObjectCommand(id,
				makeDuplicateId(id))));
		}
		return executeCommand(batch);
	}
	//---------------------------------------------------------
	bool EditorCore::deleteSelected()
	{
		StringVector doomedIds;
		for (String const& id : mSelection)
		{
			if (mGameObjectManager.objectExists(id))
			{
				doomedIds.push_back(id);
			}
		}
		if (doomedIds.empty())
		{
			return false;
		}
		if (doomedIds.size() == 1)
		{
			return executeCommand(onew(new DeleteObjectCommand(doomedIds[0])));
		}
		// multi-select: ONE undo step restores the whole batch
		optr<CompositeCommand> batch = onew(new CompositeCommand(
			"Delete " + std::to_string(doomedIds.size()) + " Objects"));
		for (String const& id : doomedIds)
		{
			batch->addCommand(onew(new DeleteObjectCommand(id)));
		}
		return executeCommand(batch);
	}
	//---------------------------------------------------------
	bool EditorCore::renameObject(String const& id, String const& newId)
	{
		if (!mGameObjectManager.objectExists(id) ||
			validateRename(id, newId) != NameValidation::Ok)
		{
			return false;
		}
		return executeCommand(onew(new RenameObjectCommand(id, newId)));
	}
	//---------------------------------------------------------
	bool EditorCore::canReparent(String const& id,
		String const& newParentId) const
	{
		if (!mGameObjectManager.objectExists(id))
		{
			return false;
		}
		if (newParentId.empty())
		{
			return true;	// making something a root is always legal
		}
		if (newParentId == id ||
			!mGameObjectManager.objectExists(newParentId))
		{
			return false;
		}
		// the cycle guard: never onto an own descendant
		return !mGameObjectManager.isDescendantOf(newParentId, id);
	}
	//---------------------------------------------------------
	bool EditorCore::reparentObject(String const& id, String const& newParentId)
	{
		if (!canReparent(id, newParentId))
		{
			return false;
		}
		// dragging a member of a multi-selection moves the WHOLE selection;
		// only the TOPMOST selected objects move - members whose ancestor is
		// selected too follow that ancestor (Unity drag semantics)
		StringVector movingIds;
		if (isSelected(id) && getSelectionCount() > 1)
		{
			for (String const& selectedId : mSelection)
			{
				if (!canReparent(selectedId, newParentId))
				{
					continue;
				}
				bool ancestorSelected = false;
				for (String const& otherId : mSelection)
				{
					if (otherId != selectedId &&
						mGameObjectManager.isDescendantOf(selectedId, otherId))
					{
						ancestorSelected = true;
						break;
					}
				}
				if (!ancestorSelected)
				{
					movingIds.push_back(selectedId);
				}
			}
		}
		else
		{
			movingIds.push_back(id);
		}
		if (movingIds.empty())
		{
			return false;
		}
		if (movingIds.size() == 1)
		{
			return executeCommand(onew(new ReparentObjectCommand(movingIds[0],
				newParentId)));
		}
		optr<CompositeCommand> batch = onew(new CompositeCommand(
			"Reparent " + std::to_string(movingIds.size()) + " Objects"));
		for (String const& movingId : movingIds)
		{
			batch->addCommand(onew(new ReparentObjectCommand(movingId,
				newParentId)));
		}
		return executeCommand(batch);
	}
	//---------------------------------------------------------
	bool EditorCore::setObjectActive(String const& id, bool active)
	{
		if (!mGameObjectManager.objectExists(id))
		{
			return false;
		}
		return executeCommand(onew(new SetActiveObjectCommand(id, active)));
	}
	//---------------------------------------------------------
	bool EditorCore::setObjectTags(String const& id, StringVector const& tags)
	{
		if (!mGameObjectManager.objectExists(id))
		{
			return false;
		}
		return executeCommand(onew(new SetTagsCommand(id, tags)));
	}
	//---------------------------------------------------------
	bool EditorCore::getObjectTags(String const& id, StringVector& out) const
	{
		optr<GameObject> gameObject = mGameObjectManager.getGameObject(id).lock();
		if (!gameObject)
		{
			return false;
		}
		out = gameObject->getTags();
		return true;
	}
	//---------------------------------------------------------
	void EditorCore::loadPhysicsLayers(Project const& project)
	{
		mPhysicsLayers.loadForProject(project);
	}
	//---------------------------------------------------------
	void EditorCore::resetPhysicsLayers()
	{
		mPhysicsLayers.loadDefaults();
	}
	//---------------------------------------------------------
	StringVector EditorCore::getPhysicsLayerNames() const
	{
		return mPhysicsLayers.names;
	}
	//---------------------------------------------------------
	bool EditorCore::groupSelected()
	{
		// only the TOPMOST selected objects become group members - selected
		// descendants of selected objects simply ride along
		StringVector memberIds;
		for (String const& id : mSelection)
		{
			if (!mGameObjectManager.objectExists(id))
			{
				continue;
			}
			bool ancestorSelected = false;
			for (String const& otherId : mSelection)
			{
				if (otherId != id &&
					mGameObjectManager.isDescendantOf(id, otherId))
				{
					ancestorSelected = true;
					break;
				}
			}
			if (!ancestorSelected)
			{
				memberIds.push_back(id);
			}
		}
		if (memberIds.empty())
		{
			return false;
		}
		return executeCommand(onew(new GroupObjectsCommand(memberIds,
			generateObjectId("Group"))));
	}
	//---------------------------------------------------------
	bool EditorCore::canMakePrefab(String const& id, String* reason) const
	{
		optr<GameObject> root = mGameObjectManager.getGameObject(id).lock();
		if (!root)
		{
			if (reason)
			{
				*reason = "the object does not exist";
			}
			return false;
		}
		if (PrefabSerializer::isPrefabProvided(mGameObjectManager, *root))
		{
			if (reason)
			{
				*reason = "a prefab-provided child cannot become its own "
					"prefab (v1 has no nested prefabs)";
			}
			return false;
		}
		// nested instances BELOW the root are refused; the root's OWN
		// prefabRef is fine - re-making an instance's prefab is the edit loop
		for (String const& subtreeId :
			mGameObjectManager.collectSubtreeIds(id))
		{
			if (subtreeId == id)
			{
				continue;
			}
			optr<GameObject> descendant =
				mGameObjectManager.getGameObject(subtreeId).lock();
			if (descendant && !descendant->getPrefabRef().empty())
			{
				if (reason)
				{
					*reason = "the subtree contains the prefab instance '" +
						subtreeId + "' (v1 has no nested prefabs)";
				}
				return false;
			}
		}
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::makePrefabInstance(String const& id,
		String const& prefabFilePath, String const& prefabRef,
		String const& prefabAssetId)
	{
		if (!canMakePrefab(id))
		{
			return false;
		}
		return executeCommand(onew(new MakePrefabCommand(id, prefabFilePath,
			prefabRef, prefabAssetId)));
	}
	//---------------------------------------------------------
	bool EditorCore::applyTransformChange(String const& id,
		EditorTransform const& before, EditorTransform const& after,
		unsigned int mergeSession)
	{
		optr<EditorCommand> command =
			onew(new TransformChangeCommand(id, before, after));
		command->setMergeSession(mergeSession);
		return executeCommand(command);
	}
	//---------------------------------------------------------
	StringVector EditorCore::getAddableComponentTypes() const
	{
		StringVector typeNames;
		for (TypeInfo const& componentType :
			GameObject::getRegisteredComponentTypes())
		{
			typeNames.push_back(componentType.getName());
		}
		std::sort(typeNames.begin(), typeNames.end());
		return typeNames;
	}
	//---------------------------------------------------------
	bool EditorCore::addComponentToObject(String const& id,
		String const& componentTypeName)
	{
		return executeCommand(onew(new AddComponentCommand(id,
			componentTypeName)));
	}
	//---------------------------------------------------------
	bool EditorCore::removeComponentFromObject(String const& id,
		String const& componentTypeName)
	{
		return executeCommand(onew(new RemoveComponentCommand(id,
			componentTypeName)));
	}
	//---------------------------------------------------------
	bool EditorCore::canRemoveComponent(String const& id,
		String const& componentTypeName, String* blockedBy) const
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		TypeInfo componentType(componentTypeName);
		if (!gameObject || !gameObject->hasComponent(componentType))
		{
			return false;
		}
		// blocked while any OTHER attached component lists the type as a
		// dependency (the exact info the components' addDependency calls
		// registered - the same data addComponent uses to auto-add them)
		for (auto const& [attachedType, component] :
			gameObject->getComponents())
		{
			if (attachedType == componentType)
			{
				continue;
			}
			TypeInfoList const& dependencies = component->getDependencies();
			if (std::find(dependencies.begin(), dependencies.end(),
				componentType) != dependencies.end())
			{
				if (blockedBy)
				{
					*blockedBy = attachedType.getName();
				}
				return false;
			}
		}
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::changeObjectMesh(String const& id, String const& meshName)
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<ModelComponent>() ||
			meshName.empty())
		{
			return false;
		}
		const String currentMesh = gameObject
			->getComponentPtr<ModelComponent>()->getCurrentModelFileName();
		if (meshName == currentMesh)
		{
			return false; // a no-op must not pollute the undo stack
		}
		return executeCommand(onew(new ChangeMeshCommand(id, currentMesh,
			meshName)));
	}
	//---------------------------------------------------------
	bool EditorCore::changeObjectScript(String const& id,
		String const& scriptFile, bool enabled)
	{
		String currentScript;
		bool currentEnabled = true;
		if (!getObjectScript(id, currentScript, currentEnabled))
		{
			return false;
		}
		if (scriptFile == currentScript && enabled == currentEnabled)
		{
			return false; // a no-op must not pollute the undo stack
		}
		return executeCommand(onew(new ChangeScriptCommand(id,
			currentScript, currentEnabled, scriptFile, enabled)));
	}
	//---------------------------------------------------------
	bool EditorCore::getObjectScript(String const& id, String& scriptFile,
		bool& enabled) const
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<ScriptComponent>())
		{
			return false;
		}
		ScriptComponent* script =
			gameObject->getComponentPtr<ScriptComponent>();
		scriptFile = script->getScriptFile();
		enabled = script->isScriptEnabled();
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::setObjectScript(String const& id,
		String const& scriptFile, bool enabled)
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<ScriptComponent>())
		{
			return false;
		}
		ScriptComponent* script =
			gameObject->getComponentPtr<ScriptComponent>();
		script->setScriptFile(scriptFile);
		script->setScriptEnabled(enabled);
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::applyRigidBodyChange(String const& id,
		PhysicsWorld::BodyDesc const& before,
		PhysicsWorld::BodyDesc const& after, unsigned int mergeSession)
	{
		optr<EditorCommand> command =
			onew(new RigidBodyChangeCommand(id, before, after));
		command->setMergeSession(mergeSession);
		return executeCommand(command);
	}
	//---------------------------------------------------------
	bool EditorCore::getRigidBodyDesc(String const& id,
		PhysicsWorld::BodyDesc& out) const
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<RigidBodyComponent>())
		{
			return false;
		}
		out = gameObject->getComponentPtr<RigidBodyComponent>()->getBodyDesc();
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::setRigidBodyDesc(String const& id,
		PhysicsWorld::BodyDesc const& desc)
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<RigidBodyComponent>())
		{
			return false;
		}
		RigidBodyComponent* rigidBody =
			gameObject->getComponentPtr<RigidBodyComponent>();
		if (rigidBody->hasBody())
		{
			// the editor only edits CREATION parameters; a live body exists
			// in play/runtime contexts the editor never edits locally
			return false;
		}
		rigidBody->setBodyType(desc.bodyType);
		switch (desc.shapeType)
		{
		case PhysicsWorld::ST_SPHERE:
			rigidBody->setSphereShape(desc.radius);
			break;
		case PhysicsWorld::ST_CAPSULE:
			rigidBody->setCapsuleShape(desc.halfHeight, desc.radius);
			break;
		case PhysicsWorld::ST_BOX:
		default:
			rigidBody->setBoxShape(desc.halfExtents);
			break;
		}
		rigidBody->setMass(desc.mass);
		rigidBody->setFriction(desc.friction);
		rigidBody->setRestitution(desc.restitution);
		rigidBody->setPlanarMode(desc.planar);
		rigidBody->setLayer(desc.layer);
		rigidBody->setIsSensor(desc.isSensor);
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::applyCameraChange(String const& id,
		EditorCameraSettings const& before,
		EditorCameraSettings const& after, unsigned int mergeSession)
	{
		optr<EditorCommand> command =
			onew(new CameraChangeCommand(id, before, after));
		command->setMergeSession(mergeSession);
		return executeCommand(command);
	}
	//---------------------------------------------------------
	bool EditorCore::getCameraSettings(String const& id,
		EditorCameraSettings& out) const
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<CameraComponent>())
		{
			return false;
		}
		CameraComponent* camera = gameObject->getComponentPtr<CameraComponent>();
		out.projectionMode = static_cast<int>(camera->getProjectionMode());
		out.orthoSize = camera->getOrthoSize();
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::setCameraSettings(String const& id,
		EditorCameraSettings const& settings)
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<CameraComponent>())
		{
			return false;
		}
		CameraComponent* camera = gameObject->getComponentPtr<CameraComponent>();
		camera->setProjectionMode(static_cast<CameraComponent::ProjectionMode>(
			settings.projectionMode));
		camera->setOrthoSize(settings.orthoSize);
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::applySpriteChange(String const& id,
		EditorSpriteSettings const& before,
		EditorSpriteSettings const& after, unsigned int mergeSession)
	{
		optr<EditorCommand> command =
			onew(new SpriteChangeCommand(id, before, after));
		command->setMergeSession(mergeSession);
		return executeCommand(command);
	}
	//---------------------------------------------------------
	bool EditorCore::getSpriteSettings(String const& id,
		EditorSpriteSettings& out) const
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<SpriteComponent>())
		{
			return false;
		}
		SpriteComponent* sprite = gameObject->getComponentPtr<SpriteComponent>();
		out.textureName = sprite->getTextureName();
		out.width = sprite->getWidth();
		out.height = sprite->getHeight();
		Color const& tint = sprite->getTint();
		out.tint[0] = tint.r;
		out.tint[1] = tint.g;
		out.tint[2] = tint.b;
		out.tint[3] = tint.a;
		out.flipX = sprite->getFlipX();
		out.flipY = sprite->getFlipY();
		out.zOrder = sprite->getZOrder();
		out.visible = sprite->isSpriteVisible();
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::setSpriteSettings(String const& id,
		EditorSpriteSettings const& settings)
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<SpriteComponent>())
		{
			return false;
		}
		SpriteComponent* sprite = gameObject->getComponentPtr<SpriteComponent>();
		sprite->setSize(settings.width, settings.height);
		sprite->setTint(settings.tint[0], settings.tint[1], settings.tint[2],
			settings.tint[3]);
		sprite->setFlip(settings.flipX, settings.flipY);
		sprite->setZOrder(settings.zOrder);
		sprite->setSpriteVisible(settings.visible);
		if (settings.textureName != sprite->getTextureName())
		{
			if (settings.textureName.empty())
			{
				if (sprite->hasSprite())
				{
					sprite->removeSprite();
				}
			}
			else
			{
				// loadSprite logs (not throws) on a missing texture; the
				// sprite keeps the old texture name only on a hard failure
				sprite->loadSprite(settings.textureName);
			}
		}
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::setObjectMesh(String const& id, String const& meshName)
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<ModelComponent>() ||
			meshName.empty())
		{
			return false;
		}
		ModelComponent* model = gameObject->getComponentPtr<ModelComponent>();
		const String previousMesh = model->getCurrentModelFileName();
		try
		{
			model->loadModel(meshName);
		}
		catch (std::exception const& e)
		{
			oDebugMsg("editor", 0, "EditorCore: mesh '" << meshName
				<< "' load failed: " << e.what());
			// try not to lose the entity: reload the previous mesh
			if (!previousMesh.empty() && previousMesh != meshName)
			{
				try
				{
					model->loadModel(previousMesh);
					applyModelFixups(id);
				}
				catch (std::exception const&)
				{
				}
			}
			return false;
		}
		applyModelFixups(id);
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::getObjectTransform(String const& id,
		EditorTransform& out) const
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<TransformComponent>())
		{
			return false;
		}
		TransformComponent* transform =
			gameObject->getComponentPtr<TransformComponent>();
		out.position = transform->getPosition();
		out.orientation = transform->getOrientation();
		out.scale = transform->getScale();
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::setObjectTransform(String const& id,
		EditorTransform const& transform)
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<TransformComponent>())
		{
			return false;
		}
		TransformComponent* transformComponent =
			gameObject->getComponentPtr<TransformComponent>();
		transformComponent->setPosition(transform.position);
		transformComponent->setOrientation(transform.orientation);
		transformComponent->setScale(transform.scale);
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::getObjectWorldTransform(String const& id,
		EditorTransform& out) const
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<TransformComponent>())
		{
			return false;
		}
		TransformComponent* transform =
			gameObject->getComponentPtr<TransformComponent>();
		out.position = transform->getWorldPosition();
		out.orientation = transform->getWorldOrientation();
		out.scale = transform->getWorldScale();
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::worldToLocalTransform(String const& id,
		EditorTransform const& world, EditorTransform& outLocal) const
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<TransformComponent>())
		{
			return false;
		}
		TransformComponent* transform =
			gameObject->getComponentPtr<TransformComponent>();
		optr<RenderNode> parent =
			transform->getNode() ? transform->getNode()->getParent()
				: optr<RenderNode>();
		if (!parent)
		{
			// attached to the world root: local IS world
			outLocal = world;
			return true;
		}
		// the same parent-relative math TransformComponent's keep-world
		// re-parenting uses (component-wise scale, matching inherit-scale)
		const Vec3 parentPosition = parent->getWorldPosition();
		const Quat parentOrientation = parent->getWorldOrientation();
		Vec3 parentScale = parent->getScale();
		for (optr<RenderNode> ancestor = parent->getParent(); ancestor;
			ancestor = ancestor->getParent())
		{
			parentScale = parentScale * ancestor->getScale();
		}
		const Quat inverseOrientation = parentOrientation.Inverse();
		outLocal.position = (inverseOrientation *
			(world.position - parentPosition)) / parentScale;
		outLocal.orientation = inverseOrientation * world.orientation;
		outLocal.scale = world.scale / parentScale;
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::executeCommand(optr<EditorCommand> const& command)
	{
		if (!command || !command->execute(*this))
		{
			return false;
		}
		mRedoStack.clear();
		markSceneDirty();
		// interactive drags: collapse into the session's first command
		if (!mUndoStack.empty() && command->getMergeSession() != 0 &&
			mUndoStack.back()->getMergeSession() == command->getMergeSession() &&
			mUndoStack.back()->mergeWith(*command))
		{
			return true;
		}
		mUndoStack.push_back(command);
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::undo()
	{
		if (mUndoStack.empty())
		{
			return false;
		}
		optr<EditorCommand> command = mUndoStack.back();
		if (!command->unexecute(*this))
		{
			oDebugMsg("editor", 0, "EditorCore: undo of \""
				<< command->getDescription() << "\" failed");
			return false;
		}
		mUndoStack.pop_back();
		mRedoStack.push_back(command);
		markSceneDirty();
		return true;
	}
	//---------------------------------------------------------
	bool EditorCore::redo()
	{
		if (mRedoStack.empty())
		{
			return false;
		}
		optr<EditorCommand> command = mRedoStack.back();
		if (!command->execute(*this))
		{
			oDebugMsg("editor", 0, "EditorCore: redo of \""
				<< command->getDescription() << "\" failed");
			return false;
		}
		mRedoStack.pop_back();
		mUndoStack.push_back(command);
		markSceneDirty();
		return true;
	}
	//---------------------------------------------------------
	String EditorCore::getUndoDescription() const
	{
		return mUndoStack.empty() ? String()
			: mUndoStack.back()->getDescription();
	}
	//---------------------------------------------------------
	String EditorCore::getRedoDescription() const
	{
		return mRedoStack.empty() ? String()
			: mRedoStack.back()->getDescription();
	}
	//---------------------------------------------------------
	void EditorCore::clearHistory()
	{
		mUndoStack.clear();
		mRedoStack.clear();
	}
	//---------------------------------------------------------
	void EditorCore::resetForScene()
	{
		clearSelection();
		clearHistory();
		clearSceneDirty();
		mNameCounters.clear();
	}
	//---------------------------------------------------------
	bool EditorCore::instantiateModelObject(String const& id,
		String const& meshName, Vec3 const& position)
	{
		optr<GameObject> gameObject =
			mGameObjectManager.createGameObject(id).lock();
		// ModelComponent depends on TransformComponent - added automatically
		if (!gameObject || !gameObject->addComponent<ModelComponent>())
		{
			return false;
		}
		try
		{
			gameObject->getComponentPtr<ModelComponent>()->loadModel(meshName);
		}
		catch (std::exception const& e)
		{
			oDebugMsg("editor", 0, "EditorCore: mesh '" << meshName
				<< "' load failed: " << e.what());
			mGameObjectManager.delGameObject(id);
			return false;
		}
		applyModelFixups(id);
		gameObject->getComponentPtr<TransformComponent>()
			->setPosition(position);
		return true;
	}
	//---------------------------------------------------------
	void EditorCore::applyModelFixups(String const& id)
	{
		optr<GameObject> gameObject =
			mGameObjectManager.getGameObject(id).lock();
		if (!gameObject || !gameObject->hasComponent<ModelComponent>())
		{
			return;
		}
		optr<MeshInstance> mesh =
			gameObject->getComponentPtr<ModelComponent>()->getMeshInstance();
		if (mesh)
		{
			// imported materials keep lighting enabled - under the editor's
			// ambient-only light the vertex colours would drown
			mesh->setVertexColourUnlit();
		}
	}
	//---------------------------------------------------------
	bool EditorCore::renameNow(String const& oldId, String const& newId)
	{
		if (!mGameObjectManager.objectExists(oldId) ||
			mGameObjectManager.objectExists(newId))
		{
			return false;
		}
		EditorObjectSnapshot snapshot;
		if (!snapshot.capture(mGameObjectManager, oldId))
		{
			return false;
		}
		const bool wasSelected = isSelected(oldId);
		// children reference their parent BY id - remember them so they can
		// follow the rename (delGameObject moves them to the grandparent)
		const StringVector childIds = mGameObjectManager.getChildren(oldId);
		if (!mGameObjectManager.delGameObject(oldId))
		{
			return false;
		}
		const String restoredId =
			snapshot.restore(mGameObjectManager, newId) ? newId : String();
		if (restoredId.empty())
		{
			// try not to lose the object: restore under the old id
			snapshot.restore(mGameObjectManager, oldId);
			applyModelFixups(oldId);
		}
		// re-point the children at the surviving id (world transforms
		// preserved, so nothing moves visually)
		const String& parentId = restoredId.empty() ? oldId : restoredId;
		for (String const& childId : childIds)
		{
			optr<GameObject> child =
				mGameObjectManager.getGameObject(childId).lock();
			if (child && mGameObjectManager.objectExists(parentId))
			{
				child->setParent(parentId, true);
			}
		}
		if (restoredId.empty())
		{
			return false;
		}
		applyModelFixups(newId);
		if (wasSelected)
		{
			deselectObject(oldId);
			addToSelection(newId);
		}
		return true;
	}
}
