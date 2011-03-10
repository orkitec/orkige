/********************************************************************
created:    Tuesday 2010/11/23 at 17:12
filename:   CcFastGuiCoolDownButton.h
company:    kunst-stoff GmbH
author:     hicham.allaoui  
purpose:	cooldown button 
*********************************************************************/

#ifndef __CC__CCFASTGUICOOLDOWNBUTTON__h__23_11_2010__17_12_44__
#define __CC__CCFASTGUICOOLDOWNBUTTON__h__23_11_2010__17_12_44__

#include "engine_fastgui/FastGuiDragDropButton.h"
#include "core_event/EventType.h"
#include "core_event/EventListener.h"
#include "core_event/EventHandler.h"

namespace CC
{
	class CcFastGuiCoolDownButton : public Orkige::FastGuiDragDropButton
    {
        //-Types--------------------------------------------
    public:
		DECL_EVENTTYPE(FreezDragDropEvent);
		DECL_EVENTTYPE(UnfreezDragDropEvent);

		 typedef Orkige::FastGuiDragDropButton super;
    protected:
    private:
        //-Variables----------------------------------------
    public:
    protected:
		optr<Orkige::FastGuiDecorWidget>			decorCoolDown;			//!< current cooldown layer
		float								refillTime;			//!< cooldownDuration
		float								refillTimer;			//!< cooldownDuration
		bool								needUpdate;
    private:
        //-Methods------------------------------------------
    public:
		CcFastGuiCoolDownButton(Orkige::String const & id,
			Orkige::String const & spriteName,
			Orkige::uint defaultGlyphIndex, 
			Orkige::String const & text, 
			Ogre::Vector2 const & position, 
			Orkige::FastGuiLabel::LabelAlignment textAlignment,
			Ogre::Vector2 const & size,
			Orkige::String const & atlas,
			Orkige::uint z);
        virtual ~CcFastGuiCoolDownButton();

		inline void setRefillTime(float _refillTime);
		inline bool isNeedUpdate();
		inline void resetCoolDown();
		void update(float deltaTime);
		void setProgress(float _progress);
		
		
    protected:
    private:
    };
    
	//----------------------------------------------------
	inline void CcFastGuiCoolDownButton::setRefillTime( float _refillTime )
	{
		this->refillTime = _refillTime;
		//this->refillTimer = 0.0f;
		this->needUpdate = true;
		this->isEnabled = false;
	}
	inline bool CcFastGuiCoolDownButton::isNeedUpdate()
	{
		return this->needUpdate ;
	}
	inline void CcFastGuiCoolDownButton::resetCoolDown()
	{
		this->refillTimer = 0.0f;
		this->needUpdate = true;
		this->isEnabled = false;
	}
}
#endif //__CCFASTGUICOOLDOWNBUTTON__h__23_11_2010__17_12_44__ 