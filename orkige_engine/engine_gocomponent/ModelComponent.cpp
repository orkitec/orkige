/**************************************************************
	created:	2010/08/30 at 21:38
	filename: 	ModelComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/ModelComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include <core_game/GameObject.h>
#include <core_project/AssetDatabase.h>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(ModelComponent, ModelSetEvent);
	IMPL_OWNED_EVENTTYPE(ModelComponent, ModelRemovedEvent);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ModelComponent::ModelComponent()
	{
		this->modelFileName = "";
		this->modelAssetId = "";
		this->addDependency<TransformComponent>();
		this->eventData = onew(new StringUtil::StringObject(StringUtil::BLANK));
	}
	//---------------------------------------------------------
	ModelComponent::~ModelComponent()
	{
	}
	//---------------------------------------------------------
	void ModelComponent::loadModel(String const & modelFileName)
	{
		oAssert(!modelFileName.empty());
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		oAssert(this->mNode);

		if(this->mesh)
		{
			this->removeModel();
		}

		if(modelFileName.empty())
			return;

		// the facade resolves the mesh through every resource group
		// (engine media AND project assets); a load failure was already
		// logged - the component honestly stays empty then
		optr<MeshInstance> loaded =
			RenderSystem::get()->getWorld()->createMeshInstance(modelFileName);
		if(!loaded)
		{
			return;
		}

		this->mesh = loaded;
		this->modelFileName = modelFileName;
		// the asset id tracks the mesh: the open project's database knows it
		// ("" without a project, or for engine media - honest either way)
		this->modelAssetId = AssetDatabase::referenceIdForValue(
			modelFileName, "", AssetDatabase::REF_FILE_NAME);
		this->mesh->attachTo(this->getNode());
		this->eventData->setValue(modelFileName);
		componentOwner->triggerEvent(Event(ModelComponent::ModelSetEvent, this->eventData));
	}
	//---------------------------------------------------------
	void ModelComponent::removeModel()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);

		// RAII: dropping the handle detaches and destroys the backend entity
		// (the historical NodeUtil wipe chain)
		this->mesh.reset();

		this->eventData->setValue(this->modelFileName);
		componentOwner->triggerEvent(Event(ModelComponent::ModelRemovedEvent, this->eventData));
		this->modelFileName = "";
		this->modelAssetId = "";
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void ModelComponent::onAdd()
	{
		oAssert(this->modelFileName.empty());
		oAssert(!this->mesh);
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		optr<RenderNode> node = transformComponent->createChildNode(componentOwner->getObjectID() + ".ModelComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
	}
	//---------------------------------------------------------
	void ModelComponent::onRemove()
	{
		// content first, then the node (a node must outlive its content)
		this->mesh.reset();
		this->modelFileName = "";
		this->modelAssetId = "";
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void ModelComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// only the mesh resource name round-trips (its stable asset id rides
		// as an attribute next to it); runtime tweaks applied to the mesh
		// instance after loadModel (unlit fixup, visibility, ...) are NOT
		// serialized yet
		ar->writeAttributed(this->modelFileName,
			AssetDatabase::REFERENCE_ID_ATTRIBUTE,
			AssetDatabase::referenceIdForValue(this->modelFileName,
				this->modelAssetId, AssetDatabase::REF_FILE_NAME));
	}
	//---------------------------------------------------------
	void ModelComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		String fileName;
		String assetId;
		ar->readAttributed(fileName,
			AssetDatabase::REFERENCE_ID_ATTRIBUTE, assetId);
		// a resolving asset id wins over a stale file name (rename
		// survival); legacy scenes without ids keep loading via the name
		AssetDatabase::resolveReference(fileName, assetId,
			AssetDatabase::REF_FILE_NAME);
		if(!fileName.empty())
		{
			this->loadModel(fileName);
		}
		// keep the serialized id even when no database could verify it (a
		// standalone scene load must not strip ids on a re-save)
		this->modelAssetId = assetId;
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(ModelComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(loadModel)
		OFUNCCR(getCurrentModelFileName)
	OOBJECT_END
}
