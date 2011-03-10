/********************************************************************
created:    Tuesday 2010/11/23 at 17:13
filename:   CcFastGuiCoolDownButton.cpp
company:    kunst-stoff GmbH
author:     hicham.allaoui
purpose:	cooldown button
*********************************************************************/

#include "cc_gui/CcFastGuiCoolDownButton.h"
using namespace Orkige;
namespace CC
{
	IMPL_OWNED_EVENTTYPE(CcFastGuiCoolDownButton, FreezDragDropEvent);
	IMPL_OWNED_EVENTTYPE(CcFastGuiCoolDownButton, UnfreezDragDropEvent);
    //----------------------------------------------------
    //- public: ------------------------------------------
    //----------------------------------------------------
	CcFastGuiCoolDownButton::CcFastGuiCoolDownButton(String const & id,
													 String const & spriteName,
													 unsigned int defaultGlyphIndex, 
													 String const & text, 
													 Ogre::Vector2 const & position, 
													 FastGuiLabel::LabelAlignment textAlignment,
													 Ogre::Vector2 const & size,
													 String const & atlas,
													 unsigned int z)
		:
		FastGuiDragDropButton(id,spriteName,defaultGlyphIndex,text,position,textAlignment,size,atlas,z)
	{
		this->decorCoolDown = onew(new FastGuiDecorWidget(id + ".decorT", "cooldown_transparent", position, this->decor->getSize(), atlas, z));


		this->refillTimer = 60.0f;
		this->refillTime = 6.0f;

		this->needUpdate = true;
		this->isEnabled = false;

	}
	//----------------------------------------------------
    CcFastGuiCoolDownButton::~CcFastGuiCoolDownButton()
    {
    }   

	void CcFastGuiCoolDownButton::update( float deltaTime )
	{
		this->refillTimer += deltaTime ;

		this->refillTimer = Ogre::Math::Clamp(this->refillTimer,0.0f,this->refillTime);

		this->setProgress(this->refillTimer/this->refillTime);
		if (refillTimer >= refillTime)
		{
			this->needUpdate = false;
			refillTimer = 0.0f;
			this->isEnabled = true;
		}
	}

	void CcFastGuiCoolDownButton::setProgress( float _progress )
	{
		float progress =  Ogre::Math::Clamp(_progress,0.0f,100.0f);

		//float yyy = this->decor->getSize().y * (100-progress)/100 ;

		this->decorCoolDown->setSize(this->decor->getSize().x ,this->decor->getSize().y * (1-progress) );
	}
	//----------------------------------------------------
    //- protected: ---------------------------------------
    //----------------------------------------------------

    //----------------------------------------------------
    //- private: -----------------------------------------
    //----------------------------------------------------
} 