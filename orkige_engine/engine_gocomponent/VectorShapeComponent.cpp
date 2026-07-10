/**************************************************************
	created:	2026/07/10 at 12:00
	filename: 	VectorShapeComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/VectorShapeComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_gocomponent/ComponentPropertyReflect.h"
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include "engine_render/RenderNode.h"
#include "core_util/VectorShapeAsset.h"
#include <core_game/GameObject.h>
#include <core_game/SceneSerializer.h>
#include <core_debug/DebugMacros.h>

#include <algorithm>
#include <vector>

namespace Orkige
{
	// the clamp range mirrors the facade quad's window (shared 2D painter's
	// window: sprites and shapes sort against each other by the same rule)
	const int VectorShapeComponent::ZORDER_MIN = -40;
	const int VectorShapeComponent::ZORDER_MAX = 40;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	VectorShapeComponent::VectorShapeComponent()
	{
		this->mShapeName = "";
		this->mTint = Color::White;
		this->mScale = 1.0f;
		this->mEdgeSoftness = 0.0f;	// auto: derived from the shape bounds
		this->mZOrder = 0;
		this->mVisible = true;
		this->addDependency<TransformComponent>();
	}
	//---------------------------------------------------------
	VectorShapeComponent::~VectorShapeComponent()
	{
	}
	//---------------------------------------------------------
	void VectorShapeComponent::loadShape(String const & shapeName)
	{
		oAssert(!shapeName.empty());
		oAssert(this->getComponentOwner());
		oAssert(this->mNode);

		if(this->mMesh)
		{
			this->removeShape();
		}
		// the `.oshape` is a small text resource read through the facade so this
		// stays backend-neutral (no renderer/Ogre here); AUTODETECT resolves
		// engine media and project assets alike
		String text;
		if(!RenderSystem::get()->readResourceText(shapeName, text))
		{
			oDebugError("engine", 0, "VectorShapeComponent: shape '" << shapeName
				<< "' not found");
			return;
		}
		std::vector<VectorTessellator::Region> regions;
		if(!VectorShapeAsset::parse(text, regions))
		{
			oDebugError("engine", 0, "VectorShapeComponent: shape '" << shapeName
				<< "' is malformed");
			return;
		}

		this->mMesh = RenderSystem::get()->getWorld()->createVectorMesh();
		if(!this->mMesh)
		{
			return;
		}
		this->mShapeName = shapeName;
		this->mRegions.swap(regions);
		this->rebuildMesh();
		this->mMesh->attachTo(this->getNode());
		this->applyVisibility();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::removeShape()
	{
		// RAII: dropping the handle detaches and destroys the mesh geometry
		this->mMesh.reset();
		this->mShapeName = "";
		this->mRegions.clear();
		this->mBuilt.clear();
	}
	//---------------------------------------------------------
	std::size_t VectorShapeComponent::getTriangleCount() const
	{
		return this->mBuilt.triangleCount();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setTint(float red, float green, float blue,
		float alpha)
	{
		this->mTint = Color(red, green, blue, alpha);
		// the tint lives in the vertex colours - refill to apply it
		if(this->mMesh)
		{
			this->rebuildMesh();
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setScale(float scale)
	{
		this->mScale = scale;
		if(this->mNode)
		{
			// uniform scale on the shape's own node (z is flat, so z-scale is
			// harmless); the feather width stays shape-local and scales with it
			this->getNode()->setScale(Vec3(scale, scale, scale));
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setEdgeSoftness(float width)
	{
		this->mEdgeSoftness = width;
		if(this->mMesh)
		{
			this->rebuildMesh();	// feather is baked geometry
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setZOrder(int zOrder)
	{
		this->mZOrder = std::clamp(zOrder, ZORDER_MIN, ZORDER_MAX);
		if(this->mMesh)
		{
			this->mMesh->setZOrder(this->mZOrder);
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setShapeVisible(bool visible)
	{
		this->mVisible = visible;
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	bool VectorShapeComponent::isShapeVisible() const
	{
		return this->mVisible;
	}
	//---------------------------------------------------------
	void VectorShapeComponent::setShapeReference(String const & shapeName)
	{
		if(shapeName.empty())
		{
			// a live mesh tears down; a detached component just clears the name
			if(this->mMesh)
			{
				this->removeShape();
			}
			else
			{
				this->mShapeName = "";
			}
			return;
		}
		// a detached load (unit tests, tooling) only records the state; the mesh
		// needs the scene node the component gets on attachment
		if(this->mNode)
		{
			this->loadShape(shapeName);
			return;
		}
		this->mShapeName = shapeName;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void VectorShapeComponent::onAdd()
	{
		oAssert(!this->mMesh);
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent =
			componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		optr<RenderNode> node = transformComponent->createChildNode(
			componentOwner->getObjectID() + ".VectorShapeComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
		// a shape reference recorded while detached loads now the node exists
		if(!this->mShapeName.empty() && !this->mMesh)
		{
			this->loadShape(this->mShapeName);
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::onRemove()
	{
		// content first, then the node (a node must outlive its content)
		this->mMesh.reset();
		this->mShapeName = "";
		this->mRegions.clear();
		this->mBuilt.clear();
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::onSetActive(bool activeInHierarchy)
	{
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::rebuildMesh()
	{
		if(!this->mMesh)
		{
			return;
		}
		// feather width: an explicit edgeSoftness, else a default proportional
		// to the shape bounds (constant visual weight at any authored scale)
		const VectorTessellator::Bounds bounds =
			VectorTessellator::computeBounds(this->mRegions);
		const float feather = (this->mEdgeSoftness > 0.0f)
			? this->mEdgeSoftness
			: VectorTessellator::defaultFeatherWidth(bounds);
		VectorTessellator::build(this->mRegions, feather, this->mBuilt);

		// convert the POD mesh to facade vertices, multiplying the instance
		// tint into every fill/feather colour
		std::vector<VectorMesh::Vertex> vertices;
		vertices.reserve(this->mBuilt.positions.size());
		for(std::size_t each = 0; each < this->mBuilt.positions.size(); ++each)
		{
			VectorTessellator::Point const & point = this->mBuilt.positions[each];
			VectorTessellator::Colour const & colour = this->mBuilt.colours[each];
			VectorMesh::Vertex vertex;
			vertex.position = Vec2(point.x, point.y);
			vertex.colour = Color(colour.r * this->mTint.r,
				colour.g * this->mTint.g, colour.b * this->mTint.b,
				colour.a * this->mTint.a);
			vertices.push_back(vertex);
		}
		this->mMesh->setMesh(vertices.empty() ? NULL : vertices.data(),
			vertices.size(),
			this->mBuilt.indices.empty() ? NULL : this->mBuilt.indices.data(),
			this->mBuilt.indices.size());
		this->applyStateToMesh();
	}
	//---------------------------------------------------------
	void VectorShapeComponent::applyStateToMesh()
	{
		if(this->mMesh)
		{
			this->mMesh->setZOrder(this->mZOrder);
		}
		if(this->mNode)
		{
			this->getNode()->setScale(
				Vec3(this->mScale, this->mScale, this->mScale));
		}
	}
	//---------------------------------------------------------
	void VectorShapeComponent::applyVisibility()
	{
		oAssert(this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		const bool ownerActive =
			!componentOwner || componentOwner->isActiveInHierarchy();
		// only over the shape's OWN node (child GameObjects gate themselves)
		this->setVisible(this->mVisible && ownerActive);
	}
	//---------------------------------------------------------
	void VectorShapeComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// reflection-driven NAMED serialization: tint, scale, edge softness,
		// zOrder, visibility and the shape AssetRef (its stable id rides the
		// record for rename survival) are written by name off the declared
		// schema. The shape is declared LAST so the scalar state is set before
		// the mesh rebuild reads it on load (@see loadComponentProperties)
		SceneSerializer::saveComponentProperties(ar, *this);
	}
	//---------------------------------------------------------
	void VectorShapeComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		SceneSerializer::loadComponentProperties(ar, *this);
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(VectorShapeComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(loadShape)
		OFUNC(removeShape)
		OFUNCCR(getShapeName)
		OFUNC(hasShape)
		OFUNC(getTriangleCount)
		OFUNC(setTint)
		OFUNC(setScale)
		OFUNC(getScale)
		OFUNC(setEdgeSoftness)
		OFUNC(getEdgeSoftness)
		OFUNC(setZOrder)
		OFUNC(getZOrder)
		OFUNC(setShapeVisible)
		OFUNC(isShapeVisible)
		// reflected schema: scalar/colour state THEN the shape reference last
		// (its setter rebuilds the mesh from the state set just above)
		OPROPERTY("tint", Orkige::PropertyKind::Color, getTint, setTintColor, Orkige::PROP_NONE)
		OPROPERTY("scale", Orkige::PropertyKind::Float, getScale, setScale, Orkige::PROP_NONE)
		OPROPERTY("edgeSoftness", Orkige::PropertyKind::Float, getEdgeSoftness, setEdgeSoftness, Orkige::PROP_NONE)
		OPROPERTY("zOrder", Orkige::PropertyKind::Int, getZOrder, setZOrder, Orkige::PROP_NONE)
		OPROPERTY("visible", Orkige::PropertyKind::Bool, isShapeVisible, setShapeVisible, Orkige::PROP_NONE)
		OPROPERTY_REF("shape", Orkige::PropertyKind::AssetRef, "shape", getShapeName, setShapeReference, Orkige::PROP_NONE)
	OOBJECT_END
}
