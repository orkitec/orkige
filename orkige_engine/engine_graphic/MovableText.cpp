/**************************************************************
	created:	2010/09/08 at 0:07
	filename: 	MovableText.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "engine_graphic/MovableText.h"
#include "engine_module/EnginePrerequisites.h"

#define POS_TEX_BINDING    0
#define COLOUR_BINDING     1
#if OGRE_VERSION_MAJOR >= 1
#if OGRE_VERSION_MINOR >= 4
#define OGRE_VERS_GREATER_EIHORT
#endif
#endif

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	MovableText::MovableText(const Ogre::String & _name, const Ogre::DisplayString & _caption,
		const Ogre::String & _fontName, int _charHeight, const Ogre::ColourValue & _color)
		: camera(NULL)
		, renderWindow(NULL)
		, font(NULL)
		, name(_name)
		, caption(_caption)
		, fontName(_fontName)
		, charHeight(_charHeight)
		, color(_color)
		, type("MovableText")
		, timeUntilNextToggle(0)
		, spaceWidth(0)
		, updateColors(true)
		, onTop(false)
		, horizontalAlignment(H_LEFT)
		, verticalAlignment(V_BELOW)
		, additionalHeight(0.0)
		, viewportAspectCoef (0.75) //set the attribute value before the first _setupGeometry call
	{
		if (name == "")
			Ogre::Exception(Ogre::Exception::ERR_INVALIDPARAMS, "Trying to create MovableText without name", "MovableText::MovableText");

		if (caption == "")
			Ogre::Exception(Ogre::Exception::ERR_INVALIDPARAMS, "Trying to create MovableText without caption", "MovableText::MovableText");

		this->renderOperation.vertexData = NULL;
		this->setFontName(this->fontName);
		this->_setupGeometry();
	}
	//---------------------------------------------------------
	MovableText::~MovableText()
	{
		if (this->renderOperation.vertexData)
			delete this->renderOperation.vertexData;
	}
	//---------------------------------------------------------
	void MovableText::setFontName(const Ogre::String & fontName)
	{
		if((Ogre::MaterialManager::getSingletonPtr()->resourceExists(this->name + "Material"))) 
		{ 
			Ogre::MaterialManager::getSingleton().remove(this->name + "Material"); 
		}

		if (this->fontName != fontName || !this->material || !this->font)
		{
			this->fontName = fontName;
			this->font = (Ogre::Font *)Ogre::FontManager::getSingleton().getByName(this->fontName).get();
			if (!this->font)
				Ogre::Exception(Ogre::Exception::ERR_ITEM_NOT_FOUND, "Could not find font " + fontName, "MovableText::setFontName");

			this->font->load();
			if (this->material)
			{
				Ogre::MaterialManager::getSingletonPtr()->remove(this->material->getName());
				this->material.reset();
			}

			this->material = this->font->getMaterial()->clone(this->name + "Material");
			if (!this->material->isLoaded())
				this->material->load();

			this->material->setDepthCheckEnabled(!this->onTop);
			this->material->setDepthBias(!this->onTop, 0);
			this->material->setDepthWriteEnabled(this->onTop);
			this->material->setLightingEnabled(false);
			this->needUpdate = true;
		}
	}
	//---------------------------------------------------------
	void MovableText::setCaption(const Ogre::DisplayString & caption)
	{
		if (caption != this->caption)
		{
			this->caption = caption;
			this->needUpdate = true;
		}
	}
	//---------------------------------------------------------
	void MovableText::setColor(const Ogre::ColourValue & color)
	{
		if (color != this->color)
		{
			this->color = color;
			this->updateColors = true;
		}
	}
	//---------------------------------------------------------
	void MovableText::setCharacterHeight(unsigned int height)
	{
		if (height != this->charHeight)
		{
			this->charHeight = height;
			this->needUpdate = true;
		}
	}
	//---------------------------------------------------------
	void MovableText::setSpaceWidth(unsigned int width)
	{
		if (width != this->spaceWidth)
		{
			this->spaceWidth = width;
			this->needUpdate = true;
		}
	}
	//---------------------------------------------------------
	void MovableText::setTextAlignment(const HorizontalAlignment & horizontalAlignment, const VerticalAlignment & verticalAlignment)
	{
		if(this->horizontalAlignment != horizontalAlignment)
		{
			this->horizontalAlignment = horizontalAlignment;
			this->needUpdate = true;
		}
		if(this->verticalAlignment != verticalAlignment)
		{
			this->verticalAlignment = verticalAlignment;
			this->needUpdate = true;
		}
	}
	//---------------------------------------------------------
	void MovableText::setAdditionalHeight( float height )
	{
		if( this->additionalHeight != height )
		{
			this->additionalHeight = height;
			this->needUpdate = true;
		}
	}
	//---------------------------------------------------------
	void MovableText::showOnTop(bool show)
	{
		if( this->onTop != show && this->material )
		{
			this->onTop = show;
			this->material->setDepthBias(!this->onTop, 0);
			this->material->setDepthCheckEnabled(!this->onTop);
			this->material->setDepthWriteEnabled(this->onTop);
		}
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void MovableText::_setupGeometry()
	{
		oAssert(this->font);
		oAssert(this->material);

		unsigned int vertexCount = static_cast<unsigned int>(this->caption.size() * 6);

		if (this->renderOperation.vertexData)
		{
			// Removed this test as it causes problems when replacing a caption
			// of the same size: replacing "Hello" with "hello"
			// as well as when changing the text alignment
			//if (mRenderOp.vertexData->vertexCount != vertexCount)
			{
				delete this->renderOperation.vertexData;
				this->renderOperation.vertexData = NULL;
				this->updateColors = true;
			}
		}

		if (!this->renderOperation.vertexData)
			this->renderOperation.vertexData = new Ogre::VertexData();

		this->renderOperation.indexData = 0;
		this->renderOperation.vertexData->vertexStart = 0;
		this->renderOperation.vertexData->vertexCount = vertexCount;
		this->renderOperation.operationType = Ogre::RenderOperation::OT_TRIANGLE_LIST; 
		this->renderOperation.useIndexes = false; 

		Ogre::VertexDeclaration * decl = this->renderOperation.vertexData->vertexDeclaration;
		Ogre::VertexBufferBinding * bind = this->renderOperation.vertexData->vertexBufferBinding;
		size_t offset = 0;

		// create/bind positions/tex.ccord. buffer
		if (!decl->findElementBySemantic(Ogre::VES_POSITION))
			decl->addElement(POS_TEX_BINDING, offset, Ogre::VET_FLOAT3, Ogre::VES_POSITION);

		offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);

		if (!decl->findElementBySemantic(Ogre::VES_TEXTURE_COORDINATES))
			decl->addElement(POS_TEX_BINDING, offset, Ogre::VET_FLOAT2, Ogre::VES_TEXTURE_COORDINATES, 0);

		Ogre::HardwareVertexBufferSharedPtr ptbuf = Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(decl->getVertexSize(POS_TEX_BINDING),
			this->renderOperation.vertexData->vertexCount,
			Ogre::HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY);
		bind->setBinding(POS_TEX_BINDING, ptbuf);

		// Colours - store these in a separate buffer because they change less often
		if (!decl->findElementBySemantic(Ogre::VES_DIFFUSE))
			decl->addElement(COLOUR_BINDING, 0, Ogre::VET_COLOUR, Ogre::VES_DIFFUSE);

		Ogre::HardwareVertexBufferSharedPtr cbuf = Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(decl->getVertexSize(COLOUR_BINDING),
			this->renderOperation.vertexData->vertexCount,
			Ogre::HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY);
		bind->setBinding(COLOUR_BINDING, cbuf);

		size_t charlen = this->caption.size();
		float *pVert = static_cast<float*>(ptbuf->lock(Ogre::HardwareBuffer::HBL_DISCARD));

		float largestWidth = 0;
		float left = 0 * 2.0 - 1.0;
		float top = -((0 * 2.0) - 1.0);

		// Derive space width from a capital A
		if (this->spaceWidth == 0)
			this->spaceWidth = (unsigned int)(this->font->getGlyphAspectRatio('A') * this->charHeight * 2.f);

		// for calculation of AABB
		Ogre::Vector3 min = Ogre::Vector3::ZERO;
		Ogre::Vector3 max = Ogre::Vector3::ZERO;
		float maxSquaredRadius = 50.f;
		bool first = true;

		// Use iterator
		// OGRE 14: DisplayString is a plain UTF-8 String, so this walks bytes;
		// like the old UTFString version it only handles single-unit characters
		Ogre::DisplayString::const_iterator i, iend;
		iend = this->caption.end();
		bool newLine = true;
		float len = 0.0f;

		if(this->verticalAlignment == MovableText::V_ABOVE)
		{
			// Raise the first line of the caption
			top += this->charHeight;
			for (i = this->caption.begin(); i != iend; ++i)
			{
				if (*i == '\n')
					top += this->charHeight * 2.0f;
			}
		}


		iend = this->caption.end();
		for( i = this->caption.begin(); i != iend; ++i )
		{
			if( newLine )
			{
				float len = 0.0f;
				for( Ogre::DisplayString::const_iterator j = i; j != iend; j++ )
				{
					Ogre::Font::CodePoint character = static_cast<unsigned char>(*j);
					if (character == 0x000D // CR
						|| character == 0x0085) // NEL
					{
						break;
					}
					else if (character == 0x0020) // space
					{
						len += this->spaceWidth;
					}
					else 
					{
						len += this->font->getGlyphAspectRatio(character) * this->charHeight * 2.0f * this->viewportAspectCoef;
					}
				}

				/*if( mAlignment == Right )
				left -= len;
				else if( mAlignment == Center )
				left -= len * 0.5;*/

				newLine = false;
			}

			Ogre::Font::CodePoint character = static_cast<unsigned char>(*i);
			if (character == 0x000D // CR
				|| character == 0x0085) // NEL
			{
				left = /*_getDerivedLeft() * 2.0*/ - 1.0f;
				top -= this->charHeight * 2.0f;
				newLine = true;
				// Also reduce tri count
				this->renderOperation.vertexData->vertexCount -= 6;
				continue;
			}
			else if (character == 0x0020) // space
			{
				// Just leave a gap, no tris
				left += this->spaceWidth;
				// Also reduce tri count
				this->renderOperation.vertexData->vertexCount -= 6;
				continue;
			}

			float horiz_height = this->font->getGlyphAspectRatio(character) * this->viewportAspectCoef ;
			const Ogre::Font::UVRect& uvRect = this->font->getGlyphTexCoords(character);

			// each vert is (x, y, z, u, v)
			//-------------------------------------------------------------------------------------
			// First tri
			//
			// Upper left
			*pVert++ = left;
			*pVert++ = top;
			*pVert++ = -1.0;
			*pVert++ = uvRect.left;
			*pVert++ = uvRect.top;

			top -= this->charHeight * 2.0f;

			// Bottom left
			*pVert++ = left;
			*pVert++ = top;
			*pVert++ = -1.0;
			*pVert++ = uvRect.left;
			*pVert++ = uvRect.bottom;

			top += this->charHeight * 2.0f;
			left += horiz_height * this->charHeight * 2.0f;

			// Top right
			*pVert++ = left;
			*pVert++ = top;
			*pVert++ = -1.0;
			*pVert++ = uvRect.right;
			*pVert++ = uvRect.top;
			//-------------------------------------------------------------------------------------

			//-------------------------------------------------------------------------------------
			// Second tri
			//
			// Top right (again)
			*pVert++ = left;
			*pVert++ = top;
			*pVert++ = -1.0;
			*pVert++ = uvRect.right;
			*pVert++ = uvRect.top;

			top -= this->charHeight * 2.0f;
			left -= horiz_height  * this->charHeight * 2.0f;

			// Bottom left (again)
			*pVert++ = left;
			*pVert++ = top;
			*pVert++ = -1.0;
			*pVert++ = uvRect.left;
			*pVert++ = uvRect.bottom;

			left += horiz_height  * this->charHeight * 2.0f;

			// Bottom right
			*pVert++ = left;
			*pVert++ = top;
			*pVert++ = -1.0;
			*pVert++ = uvRect.right;
			*pVert++ = uvRect.bottom;
			//-------------------------------------------------------------------------------------

			// Go back up with top
			top += this->charHeight * 2.0f;

			float currentWidth = (left + 1)/2 /*- _getDerivedLeft()*/;
			if (currentWidth > largestWidth)
			{
				largestWidth = currentWidth;

			}
		}



		// Unlock vertex buffer
		ptbuf->unlock();

		// update AABB/Sphere radius
		this->boundingBox = Ogre::AxisAlignedBox(min, max);
		this->radius = Ogre::Math::Sqrt(maxSquaredRadius);

		if (this->updateColors)
			this->_updateColors();

		this->needUpdate = false;
	}
	//---------------------------------------------------------
	void MovableText::_updateColors(void)
	{
		oAssert(this->font);
		oAssert(this->material);

		// Convert to system-specific
		Ogre::RGBA color;
		color = this->color.getAsBYTE();	// OGRE 14: Root::convertColourValue is deprecated
		Ogre::HardwareVertexBufferSharedPtr vbuf = this->renderOperation.vertexData->vertexBufferBinding->getBuffer(COLOUR_BINDING);
		Ogre::RGBA *pDest = static_cast<Ogre::RGBA*>(vbuf->lock(Ogre::HardwareBuffer::HBL_DISCARD));
		for (unsigned int i = 0; i < this->renderOperation.vertexData->vertexCount; ++i)
			*pDest++ = color;
		vbuf->unlock();
		this->updateColors = false;
	}
	//---------------------------------------------------------
	const Ogre::Quaternion & MovableText::getWorldOrientation(void) const
	{
		oAssert(this->camera);
		return this->camera->getDerivedOrientation();
	}
	//---------------------------------------------------------
	const Ogre::Vector3 & MovableText::getWorldPosition(void) const
	{
		oAssert(mParentNode);
		return mParentNode->_getDerivedPosition();
	}
	//---------------------------------------------------------
	void MovableText::getWorldTransforms(Ogre::Matrix4 * xform) const 
	{
		if (this->isVisible() && this->camera)
		{
			Ogre::Matrix3 rot3x3, scale3x3 = Ogre::Matrix3::IDENTITY;

			// store rotation in a matrix
			this->camera->getDerivedOrientation().ToRotationMatrix(rot3x3);

			// parent node position
			Ogre::Vector3 ppos = mParentNode->_getDerivedPosition() + Ogre::Vector3::UNIT_Y * this->additionalHeight;

			// apply scale
			scale3x3[0][0] = mParentNode->_getDerivedScale().x / 2;
			scale3x3[1][1] = mParentNode->_getDerivedScale().y / 2;
			scale3x3[2][2] = mParentNode->_getDerivedScale().z / 2;

			// apply all transforms to xform       
			*xform = (rot3x3 * scale3x3);
			xform->setTrans(ppos);
		}
	}
	//---------------------------------------------------------
	void MovableText::getRenderOperation(Ogre::RenderOperation & op)
	{
		if (this->isVisible())
		{
			if (this->needUpdate)
				this->_setupGeometry();
			if (this->updateColors)
				this->_updateColors();
			op = this->renderOperation;
		}
	}
	//---------------------------------------------------------
	void MovableText::_notifyCurrentCamera(Ogre::Camera *cam)
	{
		this->camera = cam;
	}
	//---------------------------------------------------------
	void MovableText::_updateRenderQueue(Ogre::RenderQueue* queue)
	{
		if (this->isVisible())
		{
			if (this->needUpdate)
				this->_setupGeometry();
			if (this->updateColors)
				this->_updateColors();

			queue->addRenderable(this, mRenderQueueID, OGRE_RENDERABLE_DEFAULT_PRIORITY);
			//      queue->addRenderable(this, mRenderQueueID, RENDER_QUEUE_SKIES_LATE);
		}
	}
	//---------------------------------------------------------	
	float MovableText::getBoundingRadius(void) const 
	{
		return this->radius;
	};
	//---------------------------------------------------------	
	float MovableText::getSquaredViewDepth(const Ogre::Camera *cam) const 
	{
		return 0;
	};
	//---------------------------------------------------------	
	Ogre::AxisAlignedBox const & MovableText::getBoundingBox(void) const 
	{
		return this->boundingBox;
	};
	//---------------------------------------------------------	
	Ogre::String const & MovableText::getName(void) const 
	{
		return this->name;
	};
	//---------------------------------------------------------	
	Ogre::String const & MovableText::getMovableType(void) const 
	{
		return this->type;
	};
	//---------------------------------------------------------	
	Ogre::MaterialPtr const & MovableText::getMaterial(void) const 
	{
		oAssert(this->material);
		return this->material;
	};
	//---------------------------------------------------------	
	Ogre::LightList const & MovableText::getLights(void) const 
	{
		return this->lightList;
	};
	//---------------------------------------------------------	
}
