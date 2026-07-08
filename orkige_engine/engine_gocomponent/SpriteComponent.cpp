/**************************************************************
	created:	2026/07/08 at 10:00
	filename: 	SpriteComponent.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "engine_gocomponent/SpriteComponent.h"
#include "engine_gocomponent/TransformComponent.h"
#include "engine_util/NodeUtil.h"
#include <core_game/GameObject.h>

#include <algorithm>

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(SpriteComponent, SpriteSetEvent);
	IMPL_OWNED_EVENTTYPE(SpriteComponent, SpriteRemovedEvent);

	// zOrder clamp: RENDER_QUEUE_MAIN (50) +- 40 keeps sprites inside the
	// valid render queue range and clear of the overlay queues (>= 95)
	const int SpriteComponent::ZORDER_MIN = -40;
	const int SpriteComponent::ZORDER_MAX = 40;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SpriteComponent::SpriteComponent()
	{
		this->mQuad = NULL;
		this->sceneNode = NULL;
		this->mTextureName = "";
		this->mWidth = 0.0f;	// derive both dimensions from the texture
		this->mHeight = 0.0f;
		this->mTexelWidth = 0.0f;
		this->mTexelHeight = 0.0f;
		this->mU0 = 0.0f;
		this->mV0 = 0.0f;
		this->mU1 = 1.0f;
		this->mV1 = 1.0f;
		this->mTint = Ogre::ColourValue::White;
		this->mFlipX = false;
		this->mFlipY = false;
		this->mZOrder = 0;
		this->mVisible = true;
		this->addDependency<TransformComponent>();
		this->mEventData = onew(new StringUtil::StringObject(StringUtil::BLANK));
	}
	//---------------------------------------------------------
	SpriteComponent::~SpriteComponent()
	{
	}
	//---------------------------------------------------------
	void SpriteComponent::loadSprite(String const & textureName)
	{
		oAssert(!textureName.empty());
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		oAssert(this->sceneNode);

		if(this->mQuad)
		{
			this->removeSprite();
		}

		// resolve through EVERY resource group (AUTODETECT): engine media and
		// project assets ("OrkigeProject" group) both work by plain file name
		Ogre::TexturePtr texture;
		try
		{
			texture = Ogre::TextureManager::getSingleton().load(textureName,
				Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME);
		}
		catch(Ogre::Exception const & e)
		{
			oDebugError("engine", 0, "SpriteComponent: texture '" << textureName
				<< "' failed to load: " << e.getDescription());
			return;
		}
		if(!texture)
		{
			oDebugError("engine", 0, "SpriteComponent: texture '" << textureName
				<< "' not found");
			return;
		}

		this->mTextureName = textureName;
		this->mTexelWidth = static_cast<float>(texture->getWidth());
		this->mTexelHeight = static_cast<float>(texture->getHeight());
		createSpriteMaterial(texture);

		this->mQuad = this->sceneNode->getCreator()->createManualObject(
			componentOwner->getObjectID() + ".SpriteComponent.quad");
		oAssert(this->mQuad);
		this->sceneNode->attachObject(this->mQuad);
		this->rebuildQuad();
		this->sceneNode->setVisible(this->mVisible);

		this->mEventData->setValue(textureName);
		componentOwner->triggerEvent(Event(SpriteComponent::SpriteSetEvent, this->mEventData));
	}
	//---------------------------------------------------------
	void SpriteComponent::removeSprite()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);

		if(this->mQuad)
		{
			this->sceneNode->detachObject(this->mQuad);
			this->sceneNode->getCreator()->destroyManualObject(this->mQuad);
		}

		this->mEventData->setValue(this->mTextureName);
		componentOwner->triggerEvent(Event(SpriteComponent::SpriteRemovedEvent, this->mEventData));
		this->mQuad = NULL;
		this->mTextureName = "";
		this->mTexelWidth = 0.0f;
		this->mTexelHeight = 0.0f;
	}
	//---------------------------------------------------------
	void SpriteComponent::setSize(float width, float height)
	{
		this->mWidth = width;
		this->mHeight = height;
		this->rebuildQuad();
	}
	//---------------------------------------------------------
	float SpriteComponent::getRenderedWidth() const
	{
		float width, height;
		resolveSize(this->mWidth, this->mHeight,
			this->mTexelWidth, this->mTexelHeight, width, height);
		return width;
	}
	//---------------------------------------------------------
	float SpriteComponent::getRenderedHeight() const
	{
		float width, height;
		resolveSize(this->mWidth, this->mHeight,
			this->mTexelWidth, this->mTexelHeight, width, height);
		return height;
	}
	//---------------------------------------------------------
	void SpriteComponent::setUVRect(float u0, float v0, float u1, float v1)
	{
		this->mU0 = u0;
		this->mV0 = v0;
		this->mU1 = u1;
		this->mV1 = v1;
		this->rebuildQuad();
	}
	//---------------------------------------------------------
	void SpriteComponent::setTint(float red, float green, float blue, float alpha)
	{
		this->mTint = Ogre::ColourValue(red, green, blue, alpha);
		this->rebuildQuad();
	}
	//---------------------------------------------------------
	void SpriteComponent::setFlip(bool flipX, bool flipY)
	{
		this->mFlipX = flipX;
		this->mFlipY = flipY;
		this->rebuildQuad();
	}
	//---------------------------------------------------------
	void SpriteComponent::setZOrder(int zOrder)
	{
		this->mZOrder = std::clamp(zOrder, ZORDER_MIN, ZORDER_MAX);
		if(this->mQuad)
		{
			this->mQuad->setRenderQueueGroup(renderQueueForZOrder(this->mZOrder));
		}
	}
	//---------------------------------------------------------
	void SpriteComponent::setSpriteVisible(bool visible)
	{
		this->mVisible = visible;
		if(this->sceneNode)
		{
			this->sceneNode->setVisible(visible);
		}
	}
	//---------------------------------------------------------
	bool SpriteComponent::isSpriteVisible() const
	{
		return this->mVisible;
	}
	//---------------------------------------------------------
	void SpriteComponent::resolveSize(float configuredWidth, float configuredHeight,
		float textureWidth, float textureHeight,
		float & outWidth, float & outHeight)
	{
		const float aspect = (textureWidth > 0.0f && textureHeight > 0.0f) ?
			textureWidth / textureHeight : 1.0f;
		outWidth = configuredWidth;
		outHeight = configuredHeight;
		if(outWidth <= 0.0f && outHeight <= 0.0f)
		{
			outHeight = 1.0f;
			outWidth = aspect;
		}
		else if(outWidth <= 0.0f)
		{
			outWidth = outHeight * aspect;
		}
		else if(outHeight <= 0.0f)
		{
			outHeight = outWidth / aspect;
		}
	}
	//---------------------------------------------------------
	void SpriteComponent::computeUVCorners(float u0, float v0, float u1, float v1,
		bool flipX, bool flipY, Ogre::Vector2 outCorners[4])
	{
		if(flipX)
		{
			std::swap(u0, u1);
		}
		if(flipY)
		{
			std::swap(v0, v1);
		}
		outCorners[0] = Ogre::Vector2(u0, v0);	// top-left
		outCorners[1] = Ogre::Vector2(u1, v0);	// top-right
		outCorners[2] = Ogre::Vector2(u1, v1);	// bottom-right
		outCorners[3] = Ogre::Vector2(u0, v1);	// bottom-left
	}
	//---------------------------------------------------------
	Ogre::uint8 SpriteComponent::renderQueueForZOrder(int zOrder)
	{
		const int queue = static_cast<int>(Ogre::RENDER_QUEUE_MAIN) +
			std::clamp(zOrder, ZORDER_MIN, ZORDER_MAX);
		return static_cast<Ogre::uint8>(queue);
	}
	//---------------------------------------------------------
	Ogre::MaterialPtr SpriteComponent::createSpriteMaterial(Ogre::TexturePtr const & texture)
	{
		oAssert(texture);
		const String materialName = "Sprite/" + texture->getName();
		Ogre::MaterialManager & materialManager = Ogre::MaterialManager::getSingleton();
		if(materialManager.resourceExists(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			return materialManager.getByName(materialName,
				Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		}
		// generated like PrimitiveUtil's "VertexColour" (material scripts are
		// banned): unlit, vertex colours tracked (the tint), alpha-BLENDED,
		// depth-checked/not-written, two-sided; the texture is bound as a
		// TexturePtr so the material never re-resolves it across groups
		Ogre::MaterialPtr material = materialManager.create(materialName,
			Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
		Ogre::Pass* pass = material->getTechnique(0)->getPass(0);
		pass->setLightingEnabled(false);
		pass->setVertexColourTracking(Ogre::TVC_DIFFUSE);
		pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
		pass->setDepthWriteEnabled(false);
		pass->setCullingMode(Ogre::CULL_NONE);
		Ogre::TextureUnitState* textureUnit = pass->createTextureUnitState();
		textureUnit->setTexture(texture);
		textureUnit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);
		return material;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void SpriteComponent::onAdd()
	{
		oAssert(!this->mQuad);
		oAssert(!this->sceneNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		Ogre::SceneNode* node = transformComponent->createChildSceneNode(componentOwner->getObjectID() + ".SpriteComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
	}
	//---------------------------------------------------------
	void SpriteComponent::onRemove()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);

		this->nodeListener->nodeCanBeDestroyed = true;

		if(this->mQuad)
		{
			this->sceneNode->detachObject(this->mQuad);
			this->sceneNode->getCreator()->destroyManualObject(this->mQuad);
			this->mQuad = NULL;
		}
		if(this->sceneNode)
		{
			NodeUtil::cleanSceneNode(this->sceneNode);
			this->sceneNode->removeAndDestroyAllChildren();
			transformComponent->removeAndDestroyChild(this->sceneNode->getName());
		}

		this->sceneNode = NULL;
		this->mTextureName = "";
		this->mTexelWidth = 0.0f;
		this->mTexelHeight = 0.0f;
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void SpriteComponent::rebuildQuad()
	{
		if(!this->mQuad)
		{
			return;	// nothing loaded yet - the setters only stored state
		}
		float width, height;
		resolveSize(this->mWidth, this->mHeight,
			this->mTexelWidth, this->mTexelHeight, width, height);
		Ogre::Vector2 uv[4];
		computeUVCorners(this->mU0, this->mV0, this->mU1, this->mV1,
			this->mFlipX, this->mFlipY, uv);
		const float halfWidth = width * 0.5f;
		const float halfHeight = height * 0.5f;
		// vertex order matches computeUVCorners: TL, TR, BR, BL; triangles
		// (0,3,2)(0,2,1) face +Z (the material renders two-sided anyway)
		const Ogre::Vector3 corners[4] = {
			{ -halfWidth,  halfHeight, 0.0f },
			{  halfWidth,  halfHeight, 0.0f },
			{  halfWidth, -halfHeight, 0.0f },
			{ -halfWidth, -halfHeight, 0.0f },
		};
		this->mQuad->clear();
		this->mQuad->estimateVertexCount(4);
		this->mQuad->estimateIndexCount(6);
		this->mQuad->begin("Sprite/" + this->mTextureName,
			Ogre::RenderOperation::OT_TRIANGLE_LIST);
		for(int each = 0; each < 4; ++each)
		{
			this->mQuad->position(corners[each]);
			this->mQuad->colour(this->mTint);
			this->mQuad->textureCoord(uv[each]);
		}
		this->mQuad->triangle(0, 3, 2);
		this->mQuad->triangle(0, 2, 1);
		this->mQuad->end();
		this->mQuad->setRenderQueueGroup(renderQueueForZOrder(this->mZOrder));
	}
	//---------------------------------------------------------
	void SpriteComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		ar << this->mTextureName;
		ar << this->mWidth << this->mHeight;
		ar << this->mU0 << this->mV0 << this->mU1 << this->mV1;
		ar << this->mTint.r << this->mTint.g << this->mTint.b << this->mTint.a;
		ar << this->mFlipX << this->mFlipY;
		ar << this->mZOrder;
		ar << this->mVisible;
	}
	//---------------------------------------------------------
	void SpriteComponent::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		String textureName;
		ar >> textureName;
		ar >> this->mWidth >> this->mHeight;
		ar >> this->mU0 >> this->mV0 >> this->mU1 >> this->mV1;
		ar >> this->mTint.r >> this->mTint.g >> this->mTint.b >> this->mTint.a;
		ar >> this->mFlipX >> this->mFlipY;
		ar >> this->mZOrder;
		ar >> this->mVisible;
		// a detached load (unit tests, tooling) only restores the state; the
		// quad needs the scene node the component gets on attachment
		if(!textureName.empty() && this->sceneNode)
		{
			this->loadSprite(textureName);
		}
		else
		{
			this->mTextureName = textureName;
		}
		if(this->sceneNode)
		{
			this->sceneNode->setVisible(this->mVisible);
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(SpriteComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(loadSprite)
		OFUNC(removeSprite)
		OFUNCCR(getTextureName)
		OFUNC(hasSprite)
		OFUNC(setSize)
		OFUNC(getWidth)
		OFUNC(getHeight)
		OFUNC(getRenderedWidth)
		OFUNC(getRenderedHeight)
		OFUNC(setUVRect)
		OFUNC(setTint)
		OFUNC(setFlip)
		OFUNC(getFlipX)
		OFUNC(getFlipY)
		OFUNC(setZOrder)
		OFUNC(getZOrder)
		OFUNC(setSpriteVisible)
		OFUNC(isSpriteVisible)
	OOBJECT_END
}
