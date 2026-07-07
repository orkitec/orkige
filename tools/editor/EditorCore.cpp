// EditorCore - UI-independent editor state + operations (see EditorCore.h).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorCore.h"

#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_util/PrimitiveUtil.h>
#include <core_serialization/XMLArchive.h>

#include <OgreEntity.h>
#include <OgreException.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Orkige
{
	namespace
	{
		//! unique short-lived temp file for one snapshot round-trip
		std::string makeSnapshotTempPath()
		{
			static std::atomic<unsigned int> counter{ 0 };
			const std::string name = "orkige_editor_snapshot_" +
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
		}
		return loaded;
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
		String const& meshName, Ogre::Vector3 const& position)
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
		if (core.isSelected(mObjectId))
		{
			core.clearSelection();
		}
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
		if (!core.getGameObjectManager().delGameObject(mObjectId))
		{
			return false;
		}
		if (mWasSelected)
		{
			core.clearSelection();
		}
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
		if (mWasSelected)
		{
			core.selectObject(mObjectId);
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
		core.selectObject(mNewId);
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
			core.selectObject(mSourceId);
		}
		return true;
	}
	//---------------------------------------------------------
	String DuplicateObjectCommand::getDescription() const
	{
		return "Duplicate " + mSourceId;
	}

	//---------------------------------------------------------
	//--- EditorCore -------------------------------------------
	//---------------------------------------------------------
	const float EditorCore::SNAP_TRANSLATE = 0.5f;
	const float EditorCore::SNAP_ROTATE_DEGREES = 15.0f;
	const float EditorCore::SNAP_SCALE = 0.1f;
	const Ogre::Vector3 EditorCore::DUPLICATE_OFFSET(0.5f, 0.0f, 0.5f);
	//---------------------------------------------------------
	EditorCore::EditorCore(GameObjectManager& gameObjectManager)
		: mGameObjectManager(gameObjectManager)
	{
	}
	//---------------------------------------------------------
	bool EditorCore::hasSelection() const
	{
		return !mSelectedObjectId.empty() &&
			mGameObjectManager.objectExists(mSelectedObjectId);
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
			PrimitiveUtil::CUBE_MESH_NAME, Ogre::Vector3::ZERO)));
	}
	//---------------------------------------------------------
	bool EditorCore::createTestMesh()
	{
		const String id = generateObjectId("TestMesh");
		return executeCommand(onew(new CreateObjectCommand(id,
			"test_mesh.glb", Ogre::Vector3::ZERO)));
	}
	//---------------------------------------------------------
	bool EditorCore::duplicateSelected()
	{
		if (!hasSelection())
		{
			return false;
		}
		const String sourceId = mSelectedObjectId;
		return executeCommand(onew(new DuplicateObjectCommand(sourceId,
			makeDuplicateId(sourceId))));
	}
	//---------------------------------------------------------
	bool EditorCore::deleteSelected()
	{
		if (!hasSelection())
		{
			return false;
		}
		return executeCommand(onew(new DeleteObjectCommand(mSelectedObjectId)));
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
		String const& meshName, Ogre::Vector3 const& position)
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
		catch (Ogre::Exception const& e)
		{
			oDebugMsg("editor", 0, "EditorCore: mesh '" << meshName
				<< "' load failed: " << e.getDescription());
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
		Ogre::Entity* model =
			gameObject->getComponentPtr<ModelComponent>()->getModel();
		if (model)
		{
			// imported materials keep lighting enabled - under the editor's
			// ambient-only light the vertex colours would drown
			PrimitiveUtil::makeEntityVertexColourUnlit(model);
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
		if (!mGameObjectManager.delGameObject(oldId))
		{
			return false;
		}
		if (!snapshot.restore(mGameObjectManager, newId))
		{
			// try not to lose the object: restore under the old id
			snapshot.restore(mGameObjectManager, oldId);
			applyModelFixups(oldId);
			return false;
		}
		applyModelFixups(newId);
		if (wasSelected)
		{
			selectObject(newId);
		}
		return true;
	}
}
