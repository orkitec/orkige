/**************************************************************
	created:	2010/09/07 at 23:46
	filename: 	MovableText.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __MovableText_h__7_9_2010__23_46_50__
#define __MovableText_h__7_9_2010__23_46_50__

#include <core_base/Object.h>
#include "engine_module/EnginePrerequisites.h"

namespace Orkige
{
	//! create a billboarding object that displays a text
	class ORKIGE_ENGINE_DLL MovableText : public Ogre::MovableObject, public Ogre::Renderable
	{
		//--- Types -------------------------------------------
	public:
		//! Horizontal Text Alignment
		enum HorizontalAlignment
		{
			H_LEFT,
			H_CENTER
		};
		//! Vertical Text Alignment
		enum VerticalAlignment
		{
			V_BELOW, 
			V_ABOVE
		};
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		Ogre::String			fontName;
		Ogre::String			type;
		Ogre::String			name;
		// OGRE 14 dropped Ogre::UTFString; DisplayString is a plain (UTF-8) String now
		Ogre::DisplayString		caption;
		HorizontalAlignment		horizontalAlignment;
		VerticalAlignment		verticalAlignment;

		Ogre::ColourValue		color;
		Ogre::RenderOperation	renderOperation;
		Ogre::AxisAlignedBox	boundingBox;
		Ogre::LightList			lightList;

		unsigned int			charHeight;
		unsigned int			spaceWidth;

		bool					needUpdate;
		bool					updateColors;
		bool					onTop;

		float					timeUntilNextToggle;
		float					radius;
		float					additionalHeight;

		Ogre::Camera*			camera;
		Ogre::RenderWindow*		renderWindow;
		Ogre::Font*				font;
		Ogre::MaterialPtr		material;
		Ogre::MaterialPtr		backgroundMaterial;
		float					viewportAspectCoef;
		//--- Methods -----------------------------------------
	public:
		//! constructor
		MovableText(const Ogre::String & name, const Ogre::DisplayString & caption,
			const Ogre::String & fontName = "BlueHighway", int charHeight = 32,
			const Ogre::ColourValue & color = Ogre::ColourValue::White);
		//! destructor
		virtual ~MovableText();

		//! set font
		void setFontName(Ogre::String const & fontName);
		//! set text
		void setCaption(Ogre::DisplayString const & caption);
		//! set text color
		void setColor(Ogre::ColourValue const & color);
		//! set char size
		void setCharacterHeight(unsigned int height);
		//! set spacing
		void setSpaceWidth(unsigned int width);
		//! set text alignment
		void setTextAlignment(HorizontalAlignment const & horizontalAlignment, VerticalAlignment const & verticalAlignment);
		//! set text offset to alignment
		void setAdditionalHeight(float height);
		//! show on top of object?
		void showOnTop(bool show = true);

		//! get font name
		inline Ogre::String const & getFontName() const;
		//! get text
		inline Ogre::DisplayString const & getCaption() const;
		//! get text color
		inline Ogre::ColourValue const & getColor() const;
		//! get text char height
		inline unsigned int getCharacterHeight() const;
		//! get space size
		inline unsigned int getSpaceWidth() const;
		//! get height offset
		inline float getAdditionalHeight() const;
		//! should text be shown on top?
		inline bool getShowOnTop() const;
		//! get text bounds
		inline Ogre::AxisAlignedBox getBoundingBox(void);
	protected:
	private:
		// from MovableText, create the object
		void _setupGeometry();
		void _updateColors();

		// from MovableObject
		void getWorldTransforms(Ogre::Matrix4 *xform) const;
		float getBoundingRadius(void) const;
		float getSquaredViewDepth(const Ogre::Camera *cam) const;
		Ogre::Quaternion const & getWorldOrientation(void) const;
		Ogre::Vector3 const & getWorldPosition(void) const;
		Ogre::AxisAlignedBox const & getBoundingBox(void) const;
		Ogre::String const & getName(void) const;
		Ogre::String const & getMovableType(void) const;

		void  _notifyCurrentCamera(Ogre::Camera *cam);
		void  _updateRenderQueue(Ogre::RenderQueue* queue);

		// from renderable
		void  getRenderOperation(Ogre::RenderOperation &op);
		Ogre::MaterialPtr const & getMaterial(void) const;
		Ogre::LightList const & getLights(void) const;
		virtual void visitRenderables(Ogre::Renderable::Visitor* visitor, bool debugRenderables = false) {};
	};
	//---------------------------------------------------------
	inline Ogre::String const & MovableText::getFontName() const 
	{
		return this->fontName;
	}
	//---------------------------------------------------------
	inline Ogre::DisplayString const & MovableText::getCaption() const
	{
		return this->caption;
	}
	//---------------------------------------------------------
	inline Ogre::ColourValue const & MovableText::getColor() const 
	{
		return this->color;
	}
	//---------------------------------------------------------
	inline unsigned int MovableText::getCharacterHeight() const 
	{
		return this->charHeight;
	}
	//---------------------------------------------------------
	inline unsigned int MovableText::getSpaceWidth() const 
	{
		return this->spaceWidth;
	}
	//---------------------------------------------------------
	inline float MovableText::getAdditionalHeight() const 
	{
		return this->additionalHeight;
	}
	//---------------------------------------------------------
	inline bool MovableText::getShowOnTop() const 
	{
		return this->onTop;
	}
	//---------------------------------------------------------
	inline Ogre::AxisAlignedBox MovableText::getBoundingBox(void) 
	{ 
		return this->boundingBox; 
	}
	//---------------------------------------------------------
}

#endif //__MovableText_h__7_9_2010__23_46_50__
