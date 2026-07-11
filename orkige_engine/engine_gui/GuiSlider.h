/********************************************************************
	created:    Wednesday 2010/11/03 at 19:56
	filename:   GuiSlider.h
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#ifndef __GuiSlider__h__3_11_2010__19_56_54__
#define __GuiSlider__h__3_11_2010__19_56_54__

#include "engine_gui/GuiDecorWidget.h"
#include <OgreStringVector.h>
#include "engine_gui/GuiSelectMenu.h"

namespace Orkige
{
    class ORKIGE_ENGINE_DLL GuiSlider : public GuiSelectMenu
    {
		OOBJECT(GuiSlider, GuiSelectMenu);
        //-Types--------------------------------------------
    public:
		//! @brief triggered when selection is changed
		//! @ingroup EngineEvents
		DECL_EVENTTYPE(SelectMenuEvent);
	protected:
    private:
        //-Variables----------------------------------------
    public:
    protected:
		optr<GuiDecorWidget> pin;			//!< decor for the grip element
		optr<GuiDecorWidget> pin_area;		//!< decor for the grip element
    private:
        //-Methods------------------------------------------
    public:
        GuiSlider(String const & id, String const & buttonId, String const & spriteName, uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z);
		virtual ~GuiSlider();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);

		virtual void onCursorPressed(Ogre::Vector2 const & cursorPos);
		virtual void onCursorReleased(Ogre::Vector2 const & cursorPos);
		virtual void onCursorMoved(Ogre::Vector2 const & cursorPos);

		virtual void showItem();
		virtual void setItems(const Ogre::StringVector& items);
		virtual void applyRenderTransform(Ui2DTransform const & transform);
		virtual void applyRenderAlpha(float alphaMultiplier);

    protected:
		//! dim the grip on top of the SelectMenu frame/label dim
		virtual void onEnabledChanged(bool enable);
    private:
		bool pinActive;
		std::vector<Ogre::Vector2> itemsPinSnap;
    };

}
#endif //__GuiSlider__h__3_11_2010__19_56_54__ 