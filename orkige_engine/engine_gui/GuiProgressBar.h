/********************************************************************
	created:    Wednesday 2010/11/17 at 16:04
	filename:   GuiProgressBar.h
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec	
*********************************************************************/

#ifndef __GUIPROGRESSBAR__h__17_11_2010__16_04_34__
#define __GUIPROGRESSBAR__h__17_11_2010__16_04_34__

#include "engine_gui/GuiDecorWidget.h"
#include "engine_gui/GuiLabel.h"

namespace Orkige
{
    class ORKIGE_ENGINE_DLL GuiProgressBar : public GuiWidget
    {
		OOBJECT(GuiProgressBar, GuiWidget);
		
        //-Types--------------------------------------------
    public:
		DECL_EVENTTYPE(ProgressBarChanged);
    protected:
    private:
        //-Variables----------------------------------------
    public:
    protected:
		optr<GuiLabel> label;			//!< current GuiProgressBar text
		optr<GuiDecorWidget> decor;		//!< current GuiProgressBar image
		optr<GuiDecorWidget> barDecore;		//!< current GuiProgressBar bar
		String baseSpriteName;				//!< base name of the button state sprite;
		
    private:
		Ogre::Vector2 barMaxSize;			//!bar size at 100%
		float progress;
        //-Methods------------------------------------------
    public:
        GuiProgressBar(String const & id, String const & spriteName,uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, GuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z);
        virtual ~GuiProgressBar();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();
		//! get text holding ui element
		inline woptr<GuiLabel> getLabel();
		//! get image ui element
		inline woptr<GuiDecorWidget> getDecor();

		//! set progress
		void setProgress(float _progress);
		//! increment/decrement progress
		void addProgress(float _progress);
		//! get progress
		inline float getProgress();

		//! get button text
		String getCaption();
		//! set button text
		void setCaption(String const & text);
		virtual void applyRenderTransform(Ui2DTransform const & transform);
		virtual void applyRenderAlpha(float alphaMultiplier);

    protected:
    private:
    };
	//---------------------------------------------------------------
	inline woptr<GuiLabel> GuiProgressBar::getLabel()
	{
		return this->label;
	}
	//---------------------------------------------------------------
	inline woptr<GuiDecorWidget> GuiProgressBar::getDecor()
	{
		return this->decor;
	}
	//---------------------------------------------------------------
	inline float GuiProgressBar::getProgress()
	{
		return this->progress;
	}
}
#endif //__GUIPROGRESSBAR__h__17_11_2010__16_04_34__ 