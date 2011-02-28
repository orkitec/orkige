/********************************************************************
	created:    Wednesday 2010/11/17 at 16:04
	filename:   FastGuiProgressBar.h
	author:     hicham.allaoui  
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#ifndef __FASTGUIPROGRESSBAR__h__17_11_2010__16_04_34__
#define __FASTGUIPROGRESSBAR__h__17_11_2010__16_04_34__

#include "engine_fastgui/FastGuiDecorWidget.h"
#include "engine_fastgui/FastGuiLabel.h"

namespace Orkige
{
    class FastGuiProgressBar : public FastGuiWidget
    {
		OOBJECT(FastGuiProgressBar, FastGuiWidget);
		
        //-Types--------------------------------------------
    public:
		DECL_EVENTTYPE(ProgressBarChanged);
    protected:
    private:
        //-Variables----------------------------------------
    public:
    protected:
		optr<FastGuiLabel> label;			//!< current FastGuiProgressBar text
		optr<FastGuiDecorWidget> decor;		//!< current FastGuiProgressBar image
		optr<FastGuiDecorWidget> barDecore;		//!< current FastGuiProgressBar bar
		String baseSpriteName;				//!< base name of the button state sprite;
		
    private:
		Ogre::Vector2 barMaxSize;			//!bar size at 100%
		float progress;
        //-Methods------------------------------------------
    public:
        FastGuiProgressBar(String const & id, String const & spriteName,uint defaultGlyphIndex, String const & text, Ogre::Vector2 const & position, FastGuiLabel::LabelAlignment textAlignment, Ogre::Vector2 const & size, String const & atlas, uint z);
        virtual ~FastGuiProgressBar();

		virtual void setPosition(Ogre::Real left, Ogre::Real top);
		virtual void setSize(Ogre::Real width, Ogre::Real height);
		virtual Ogre::Vector2 getSize();
		virtual Ogre::Vector2 getPosition();
		//! get text holding ui element
		inline woptr<FastGuiLabel> getLabel();
		//! get image ui element
		inline woptr<FastGuiDecorWidget> getDecor();

		//! set progress
		void setProgress(float _progress);
		//! set progress
		void addProgress(float _progress);
		//! set progress
		inline float getProgress();


		String getCaption();
		//! set button text
		void setCaption(String const & text);

    protected:
    private:
    };
	//---------------------------------------------------------------
	inline woptr<FastGuiLabel> FastGuiProgressBar::getLabel()
	{
		return this->label;
	}
	//---------------------------------------------------------------
	inline woptr<FastGuiDecorWidget> FastGuiProgressBar::getDecor()
	{
		return this->decor;
	}
	//---------------------------------------------------------------
	inline float FastGuiProgressBar::getProgress()
	{
		return this->progress;
	}
}
#endif //__FASTGUIPROGRESSBAR__h__17_11_2010__16_04_34__ 