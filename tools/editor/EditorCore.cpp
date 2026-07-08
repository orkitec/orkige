// EditorCore - UI-independent editor state + operations (see EditorCore.h).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorCore.h"

#include <engine_gocomponent/TransformComponent.h>
#include <engine_gocomponent/ModelComponent.h>
#include <engine_gocomponent/SpriteComponent.h>
#include <engine_gocomponent/CameraComponent.h>
#include <engine_gocomponent/RigidBodyComponent.h>
#include <engine_gocomponent/ScriptComponent.h>
#include <engine_util/PrimitiveUtil.h>
#include <core_serialization/XMLArchive.h>

#include <OgreEntity.h>
#include <OgreException.h>

#include <algorithm>
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
	const Ogre::Vector3 EditorCore::DUPLICATE_OFFSET(0.5f, 0.0f, 0.5f);
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
		Ogre::ColourValue const& tint = sprite->getTint();
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
		catch (Ogre::Exception const& e)
		{
			oDebugMsg("editor", 0, "EditorCore: mesh '" << meshName
				<< "' load failed: " << e.getDescription());
			// try not to lose the entity: reload the previous mesh
			if (!previousMesh.empty() && previousMesh != meshName)
			{
				try
				{
					model->loadModel(previousMesh);
					applyModelFixups(id);
				}
				catch (Ogre::Exception const&)
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
			deselectObject(oldId);
			addToSelection(newId);
		}
		return true;
	}
}
