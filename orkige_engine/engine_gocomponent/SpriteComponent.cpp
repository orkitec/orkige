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
#include "engine_render/RenderSystem.h"
#include "engine_render/RenderWorld.h"
#include <core_game/GameObject.h>
#include <core_debug/DebugMacros.h>
#include <core_project/AssetDatabase.h>

#include <algorithm>
#include <sstream>
#include <vector>

#if defined(__APPLE__)
#	include <TargetConditionals.h>
#endif

namespace
{
	//! the running platform token AssetDatabase import settings resolve
	//! against (per-platform sampler/maxSize overrides). Desktop = "" (the
	//! default block); the cook uses the same tokens at export time.
	Orkige::String currentPlatformToken()
	{
#if defined(__ANDROID__)
		return "android";
#elif defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_SIMULATOR)
		return "ios";
#else
		return "";
#endif
	}
}

namespace Orkige
{
	IMPL_OWNED_EVENTTYPE(SpriteComponent, SpriteSetEvent);
	IMPL_OWNED_EVENTTYPE(SpriteComponent, SpriteRemovedEvent);

	// the clamp range mirrors the facade quad's (classic mapping:
	// RENDER_QUEUE_MAIN (50) +- 40 keeps sprites inside the valid render
	// queue range and clear of the overlay queues (>= 95))
	const int SpriteComponent::ZORDER_MIN = -40;
	const int SpriteComponent::ZORDER_MAX = 40;
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	SpriteComponent::SpriteComponent()
	{
		this->mTextureName = "";
		this->mTextureAssetId = "";
		this->mWidth = 0.0f;	// derive both dimensions from the texture
		this->mHeight = 0.0f;
		this->mTexelWidth = 0.0f;
		this->mTexelHeight = 0.0f;
		this->mU0 = 0.0f;
		this->mV0 = 0.0f;
		this->mU1 = 1.0f;
		this->mV1 = 1.0f;
		this->mTint = Color::White;
		this->mFlipX = false;
		this->mFlipY = false;
		// the historic sprite sampler (also the facade quad's default); a
		// texture's import settings override it live on loadSprite
		this->mFilter = SpriteQuad::FILTER_BILINEAR;
		this->mAddressing = SpriteQuad::ADDRESS_CLAMP;
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
		oAssert(this->mNode);

		if(this->mQuad)
		{
			this->removeSprite();
		}

		// the facade resolves the texture through EVERY resource group
		// (AUTODETECT): engine media and project assets both work by plain
		// file name; a load failure was logged - the sprite stays empty
		optr<SpriteQuad> quad =
			RenderSystem::get()->getWorld()->createSpriteQuad(textureName);
		if(!quad)
		{
			return;
		}

		this->mQuad = quad;
		this->mTextureName = textureName;
		// the asset id tracks the texture: the open project's database knows
		// it ("" without a project, or for engine media - honest either way)
		this->mTextureAssetId = AssetDatabase::referenceIdForValue(
			textureName, "", AssetDatabase::REF_FILE_NAME);
		this->mQuad->getTextureSize(this->mTexelWidth, this->mTexelHeight);
		// honor the texture's import-settings sampler LIVE (before the state
		// push, so applyStateToQuad binds the right material/datablock)
		this->applyImportSettings();
		this->applyStateToQuad();
		this->mQuad->attachTo(this->getNode());
		this->applyVisibility();

		this->mEventData->setValue(textureName);
		componentOwner->triggerEvent(Event(SpriteComponent::SpriteSetEvent, this->mEventData));
	}
	//---------------------------------------------------------
	bool SpriteComponent::loadSpriteFromAtlas(String const & atlasName,
		String const & regionName)
	{
		oAssert(!atlasName.empty());
		oAssert(!regionName.empty());
		// the .oatlas is a small text resource (a `texture <file>` line + one
		// `<region> <x> <y> <w> <h>` line per region); read it through the
		// facade so this stays backend-neutral (no renderer/Ogre here)
		String atlasText;
		if(!RenderSystem::get()->readResourceText(atlasName, atlasText))
		{
			oDebugError("engine", 0, "SpriteComponent: atlas '" << atlasName
				<< "' not found");
			return false;
		}
		String textureFile;
		bool regionFound = false;
		float regionX = 0.0f, regionY = 0.0f, regionW = 0.0f, regionH = 0.0f;
		std::istringstream lines(atlasText);
		String line;
		while(std::getline(lines, line))
		{
			// tokenize on whitespace; skip comments and section headers
			std::istringstream tokens(line);
			String key;
			if(!(tokens >> key) || key.empty() || key[0] == '#' ||
				key[0] == '[')
			{
				continue;
			}
			if(key == "texture")
			{
				tokens >> textureFile;
			}
			else if(key == regionName)
			{
				if(tokens >> regionX >> regionY >> regionW >> regionH)
				{
					regionFound = true;
				}
			}
		}
		if(textureFile.empty())
		{
			oDebugError("engine", 0, "SpriteComponent: atlas '" << atlasName
				<< "' has no 'texture' line");
			return false;
		}
		if(!regionFound)
		{
			oDebugError("engine", 0, "SpriteComponent: atlas '" << atlasName
				<< "' has no region '" << regionName << "'");
			return false;
		}
		this->loadSprite(textureFile);
		if(!this->mQuad)
		{
			return false;	// the texture load already logged the failure
		}
		// the SHARED pixel-rect->UV primitive: region pixels -> UV (with the
		// same half-texel inset the flipbook uses against seam bleeding)
		float u0, v0, u1, v1;
		pixelRectToUV(regionX, regionY, regionW, regionH,
			this->mTexelWidth, this->mTexelHeight, u0, v0, u1, v1);
		this->setUVRect(u0, v0, u1, v1);
		return true;
	}
	//---------------------------------------------------------
	void SpriteComponent::removeSprite()
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);

		// RAII: dropping the handle detaches and destroys the quad geometry
		this->mQuad.reset();

		this->mEventData->setValue(this->mTextureName);
		componentOwner->triggerEvent(Event(SpriteComponent::SpriteRemovedEvent, this->mEventData));
		this->mTextureName = "";
		this->mTextureAssetId = "";
		this->mTexelWidth = 0.0f;
		this->mTexelHeight = 0.0f;
	}
	//---------------------------------------------------------
	void SpriteComponent::setSize(float width, float height)
	{
		this->mWidth = width;
		this->mHeight = height;
		if(this->mQuad)
		{
			this->mQuad->setSize(width, height);
		}
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
		if(this->mQuad)
		{
			this->mQuad->setUVRect(u0, v0, u1, v1);
		}
	}
	//---------------------------------------------------------
	void SpriteComponent::getUVRect(float & u0, float & v0, float & u1, float & v1) const
	{
		u0 = this->mU0;
		v0 = this->mV0;
		u1 = this->mU1;
		v1 = this->mV1;
	}
	//---------------------------------------------------------
	void SpriteComponent::getTextureSize(float & width, float & height) const
	{
		width = this->mTexelWidth;
		height = this->mTexelHeight;
	}
	//---------------------------------------------------------
	void SpriteComponent::setTint(float red, float green, float blue, float alpha)
	{
		this->mTint = Color(red, green, blue, alpha);
		if(this->mQuad)
		{
			this->mQuad->setTint(this->mTint);
		}
	}
	//---------------------------------------------------------
	void SpriteComponent::setFlip(bool flipX, bool flipY)
	{
		this->mFlipX = flipX;
		this->mFlipY = flipY;
		if(this->mQuad)
		{
			this->mQuad->setFlip(flipX, flipY);
		}
	}
	//---------------------------------------------------------
	void SpriteComponent::setFilter(SpriteQuad::FilterMode filter)
	{
		this->mFilter = filter;
		if(this->mQuad)
		{
			this->mQuad->setSampler(this->mFilter, this->mAddressing);
		}
	}
	//---------------------------------------------------------
	void SpriteComponent::setAddressing(SpriteQuad::AddressMode addressing)
	{
		this->mAddressing = addressing;
		if(this->mQuad)
		{
			this->mQuad->setSampler(this->mFilter, this->mAddressing);
		}
	}
	//---------------------------------------------------------
	void SpriteComponent::setZOrder(int zOrder)
	{
		this->mZOrder = std::clamp(zOrder, ZORDER_MIN, ZORDER_MAX);
		if(this->mQuad)
		{
			this->mQuad->setZOrder(this->mZOrder);
		}
	}
	//---------------------------------------------------------
	void SpriteComponent::setSpriteVisible(bool visible)
	{
		this->mVisible = visible;
		if(this->mNode)
		{
			this->applyVisibility();
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
		bool flipX, bool flipY, Vec2 outCorners[4])
	{
		if(flipX)
		{
			std::swap(u0, u1);
		}
		if(flipY)
		{
			std::swap(v0, v1);
		}
		outCorners[0] = Vec2(u0, v0);	// top-left
		outCorners[1] = Vec2(u1, v0);	// top-right
		outCorners[2] = Vec2(u1, v1);	// bottom-right
		outCorners[3] = Vec2(u0, v1);	// bottom-left
	}
	//---------------------------------------------------------
	void SpriteComponent::frameToUVRect(int frame, int columns, int rows,
		float textureWidth, float textureHeight,
		float & u0, float & v0, float & u1, float & v1)
	{
		// a degenerate grid shows the whole texture (honest fallback, no crash)
		if(columns <= 0 || rows <= 0)
		{
			u0 = 0.0f;
			v0 = 0.0f;
			u1 = 1.0f;
			v1 = 1.0f;
			return;
		}
		// row-major, top-left origin; clamp so an out-of-range frame stays on
		// the sheet instead of sampling past (0,0)-(1,1)
		const int cellCount = columns * rows;
		frame = std::clamp(frame, 0, cellCount - 1);
		const int column = frame % columns;
		const int row = frame / columns;
		const float cellWidth = 1.0f / static_cast<float>(columns);
		const float cellHeight = 1.0f / static_cast<float>(rows);
		u0 = static_cast<float>(column) * cellWidth;
		u1 = static_cast<float>(column + 1) * cellWidth;
		v0 = static_cast<float>(row) * cellHeight;
		v1 = static_cast<float>(row + 1) * cellHeight;
		// the same half-texel inset the atlas path uses (shared primitive)
		insetHalfTexel(u0, v0, u1, v1, textureWidth, textureHeight);
	}
	//---------------------------------------------------------
	void SpriteComponent::pixelRectToUV(float x, float y,
		float width, float height, float textureWidth, float textureHeight,
		float & u0, float & v0, float & u1, float & v1)
	{
		// no texture size = no normalization possible: show the whole texture
		if(textureWidth <= 0.0f || textureHeight <= 0.0f)
		{
			u0 = 0.0f;
			v0 = 0.0f;
			u1 = 1.0f;
			v1 = 1.0f;
			return;
		}
		u0 = x / textureWidth;
		u1 = (x + width) / textureWidth;
		v0 = y / textureHeight;
		v1 = (y + height) / textureHeight;
		// same seam-safety inset as the flipbook grid cells
		insetHalfTexel(u0, v0, u1, v1, textureWidth, textureHeight);
	}
	//---------------------------------------------------------
	void SpriteComponent::insetHalfTexel(float & u0, float & v0,
		float & u1, float & v1, float textureWidth, float textureHeight)
	{
		// bilinear filtering pulls neighbour texels at the rect edges (visible
		// atlas-seam bleeding); pulling in by half a texel on each side keeps
		// the sample inside the region. Needs the real texel size - each axis
		// is skipped when its size is unknown (<= 0).
		if(textureWidth > 0.0f)
		{
			const float insetU = 0.5f / textureWidth;
			u0 += insetU;
			u1 -= insetU;
		}
		if(textureHeight > 0.0f)
		{
			const float insetV = 0.5f / textureHeight;
			v0 += insetV;
			v1 -= insetV;
		}
	}
	//---------------------------------------------------------
	unsigned char SpriteComponent::renderQueueForZOrder(int zOrder)
	{
		// 50 = classic Ogre::RENDER_QUEUE_MAIN = the base of Ogre-Next's
		// default-FAST v2 queue window (0..99): the shared painter's base
		// both backends sort sprites from. Spelled as a literal so this
		// pure helper stays backend-free (B3); the live mapping sits in the
		// backends (RenderBackend sprite queue services).
		const int RENDER_QUEUE_MAIN = 50;
		const int queue = RENDER_QUEUE_MAIN +
			std::clamp(zOrder, ZORDER_MIN, ZORDER_MAX);
		return static_cast<unsigned char>(queue);
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void SpriteComponent::onAdd()
	{
		oAssert(!this->mQuad);
		oAssert(!this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		optr<TransformComponent> transformComponent = componentOwner->getComponent<TransformComponent>().lock();
		oAssert(transformComponent);
		optr<RenderNode> node = transformComponent->createChildNode(componentOwner->getObjectID() + ".SpriteComponent.sceneNode");
		oAssert(node);
		this->initSceneNodeGuard(node, componentOwner->getEventManager(), this);
	}
	//---------------------------------------------------------
	void SpriteComponent::onRemove()
	{
		// content first, then the node (a node must outlive its content)
		this->mQuad.reset();
		this->mTextureName = "";
		this->mTextureAssetId = "";
		this->mTexelWidth = 0.0f;
		this->mTexelHeight = 0.0f;
		this->deinitSceneNodeGuard();
	}
	//---------------------------------------------------------
	void SpriteComponent::onSetActive(bool activeInHierarchy)
	{
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	void SpriteComponent::applyVisibility()
	{
		oAssert(this->mNode);
		GameObject* componentOwner = this->getComponentOwner();
		const bool ownerActive = !componentOwner || componentOwner->isActiveInHierarchy();
		// only over the sprite's OWN node (child GameObjects gate themselves)
		this->setVisible(this->mVisible && ownerActive);
	}
	//---------------------------------------------------------
	void SpriteComponent::applyStateToQuad()
	{
		oAssert(this->mQuad);
		this->mQuad->setSize(this->mWidth, this->mHeight);
		this->mQuad->setUVRect(this->mU0, this->mV0, this->mU1, this->mV1);
		this->mQuad->setTint(this->mTint);
		this->mQuad->setFlip(this->mFlipX, this->mFlipY);
		this->mQuad->setSampler(this->mFilter, this->mAddressing);
		this->mQuad->setZOrder(this->mZOrder);
	}
	//---------------------------------------------------------
	void SpriteComponent::applyImportSettings()
	{
		// sampler comes from the texture ASSET's import settings (not per
		// sprite - so it is NOT serialized): resolve them through the open
		// project's database and apply for the running platform. No project /
		// no <texture> block keeps the constructor defaults.
		this->mFilter = SpriteQuad::FILTER_BILINEAR;
		this->mAddressing = SpriteQuad::ADDRESS_CLAMP;
		optr<AssetDatabase> const & database = AssetDatabase::getActive();
		if(!database || this->mTextureAssetId.empty())
		{
			return;
		}
		const String metaPath =
			database->metaFilePathForId(this->mTextureAssetId);
		if(metaPath.empty())
		{
			return;
		}
		TextureImport texture;
		if(!AssetDatabase::readImportSettings(metaPath, texture))
		{
			return;	// id-only sidecar - no sampler override
		}
		TextureImportSettings const & settings =
			texture.resolvedFor(currentPlatformToken());
		this->mFilter = (settings.filter == "point")
			? SpriteQuad::FILTER_POINT : SpriteQuad::FILTER_BILINEAR;
		this->mAddressing = (settings.wrap == "wrap")
			? SpriteQuad::ADDRESS_WRAP : SpriteQuad::ADDRESS_CLAMP;
	}
	//---------------------------------------------------------
	void SpriteComponent::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		// the stable asset id rides as an attribute NEXT TO the legacy
		// texture name - old builds/scenes stay mutually loadable
		ar->writeAttributed(this->mTextureName,
			AssetDatabase::REFERENCE_ID_ATTRIBUTE,
			AssetDatabase::referenceIdForValue(this->mTextureName,
				this->mTextureAssetId, AssetDatabase::REF_FILE_NAME));
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
		String textureAssetId;
		ar->readAttributed(textureName,
			AssetDatabase::REFERENCE_ID_ATTRIBUTE, textureAssetId);
		ar >> this->mWidth >> this->mHeight;
		ar >> this->mU0 >> this->mV0 >> this->mU1 >> this->mV1;
		ar >> this->mTint.r >> this->mTint.g >> this->mTint.b >> this->mTint.a;
		ar >> this->mFlipX >> this->mFlipY;
		ar >> this->mZOrder;
		ar >> this->mVisible;
		// a resolving asset id wins over a stale texture name (rename
		// survival); legacy scenes without ids keep loading via the name
		AssetDatabase::resolveReference(textureName, textureAssetId,
			AssetDatabase::REF_FILE_NAME);
		// a detached load (unit tests, tooling) only restores the state; the
		// quad needs the scene node the component gets on attachment
		if(!textureName.empty() && this->mNode)
		{
			this->loadSprite(textureName);
		}
		else
		{
			this->mTextureName = textureName;
		}
		// keep the serialized id even when no database could verify it (a
		// standalone scene load must not strip ids on a re-save)
		this->mTextureAssetId = textureAssetId;
		if(this->mNode)
		{
			this->applyVisibility();
		}
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	OOBJECT_IMPL(SpriteComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(loadSprite)
		OFUNC(loadSpriteFromAtlas)
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
