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
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
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
		// content attached to an already-hidden node does not inherit the
		// visibility - re-apply the owner's active state
		if(!componentOwner->isActiveInHierarchy())
		{
			this->setVisible(false);
		}
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
	void ModelComponent::setModelReference(String const & modelFileName)
	{
		if(modelFileName.empty())
		{
			// a live mesh tears down through removeModel (needs the owner); a
			// detached component (no mesh) just clears the recorded reference
			if(this->mesh)
			{
				this->removeModel();
			}
			else
			{
				this->modelFileName = "";
				this->modelAssetId = "";
			}
			return;
		}
		if(this->mNode)
		{
			this->loadModel(modelFileName);
			return;
		}
		// a detached load (no scene node yet): record the reference so a re-save
		// keeps it; the live mesh is built when the component gets its node
		this->modelFileName = modelFileName;
		this->modelAssetId = AssetDatabase::referenceIdForValue(
			modelFileName, "", AssetDatabase::REF_FILE_NAME);
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
	void ModelComponent::onSetActive(bool activeInHierarchy)
	{
		if(this->mNode)
		{
			// only over the model's OWN node (child GameObjects gate themselves)
			this->setVisible(activeInHierarchy);
		}
	}
	//---------------------------------------------------------
	void ModelComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: the mesh AssetRef
		// (its stable asset id rides in the record's reference field for rename
		// survival) is the only serialized field; runtime tweaks applied to the
		// mesh instance after loadModel (unlit fixup, visibility, ...) are not
		// serialized
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void ModelComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		// the mesh AssetRef is resolved against the active AssetDatabase (a
		// resolving id wins over a stale name - rename survival) then set through
		// setModelReference, which loads the model when the scene node exists
		SceneSerializer::loadComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(ModelComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(loadModel)
		OFUNCCR(getCurrentModelFileName)
		// reflected schema: the mesh reference (AssetRef, asset-kind
		// "mesh"); its stable id rides the record so a project rename survives
		OPROPERTY_REF("mesh", Orkige::PropertyKind::AssetRef, "mesh", getCurrentModelFileName, setModelReference, Orkige::PROP_NONE)
	OOBJECT_END
}
