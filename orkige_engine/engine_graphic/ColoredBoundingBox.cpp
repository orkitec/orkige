/**************************************************************
	created:	2010/09/07 at 2:15
	filename: 	ColoredBoundingBox.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/

#include "engine_graphic/ColoredBoundingBox.h"
#include "engine_module/EnginePrerequisites.h"
#include "engine_graphic/Engine.h"

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ColoredBoundingBox::ColoredBoundingBox(Ogre::Entity * entity,Ogre::ColourValue colour)
	{
		this->entity = entity;
		this->boxNode = NULL;
		this->lines = onew(new DynamicLines(colour));

		this->setup();
	}
	//---------------------------------------------------------
	ColoredBoundingBox::ColoredBoundingBox(Ogre::SceneNode * sn,Ogre::ColourValue colour)
	{
		this->entity = NULL;
		this->boxNode = sn;
		this->lines = onew(new DynamicLines(colour));

		this->setup();
	}
	//---------------------------------------------------------
	ColoredBoundingBox::~ColoredBoundingBox()
	{
	}
	//---------------------------------------------------------
	void ColoredBoundingBox::setColour(Ogre::ColourValue colour)
	{
		this->lines->setColour(colour);
		this->lines->update();
		this->node->needUpdate();
	}
	//---------------------------------------------------------
	void ColoredBoundingBox::update()
	{
		Ogre::AxisAlignedBox bb;
		if(this->entity != NULL)
		{
			bb = entity->getWorldBoundingBox();
		}
		else if(this->boxNode != NULL)
		{
			this->boxNode->_update(true,false);
			bb = this->boxNode->_getWorldAABB();
		}
		else
		{
			oAssert(!"No Valid Entity or Node for BoundingBox given!");
		}

		//     6_______ 7
		//    /|	  /|
		// 	1/_|____3/ | 
		//	|  |_ _ |_ |
		//	| /4	| /5		
		//	|/______|/
		//  0       2
		//
		//		01235746045205167317//51 crossed middle line
		//		01235746045205761371//57 line drawed twice
		/*
		lines->setPoint(0,bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_BOTTOM ));		//0
				lines->setPoint(1,bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_TOP  ));			//1
				lines->setPoint(2,bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));		//2
				lines->setPoint(3,bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_TOP  ));		//3
				lines->setPoint(4,bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_BOTTOM ));		//5
				lines->setPoint(5,bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_TOP  ));			//7
				lines->setPoint(6,bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_BOTTOM ));		//4
				lines->setPoint(7,bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_TOP  ));			//6
				lines->setPoint(8,bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_BOTTOM ));		//0
				lines->setPoint(9,bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_BOTTOM ));		//4
				lines->setPoint(10,bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_BOTTOM ));		//5
				lines->setPoint(11,bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));		//2
				lines->setPoint(12,bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_BOTTOM ));		//0
				lines->setPoint(13,bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_BOTTOM ));		//5
				lines->setPoint(14,bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_TOP  ));		//7
				lines->setPoint(15,bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_TOP  ));			//6
				lines->setPoint(16,bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_TOP  ));		//1
				lines->setPoint(17,bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_TOP  ));		//3
				lines->setPoint(18,bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_TOP  ));		//7
				lines->setPoint(19,bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_TOP  ));		//1*/
		
		this->lines->setPoint(0,bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));	//2
		this->lines->setPoint(1,bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_TOP  ));		//1
		this->lines->setPoint(2,bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_BOTTOM ));	//0
		this->lines->setPoint(3,bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));	//2
		this->lines->setPoint(4,bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_BOTTOM ));	//4
		this->lines->setPoint(5,bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_BOTTOM ));	//0
		this->lines->setPoint(6,bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_TOP  ));		//6
		this->lines->setPoint(7,bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_TOP  ));		//1
		this->lines->setPoint(8,bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_TOP  ));	//3
		this->lines->setPoint(9,bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_TOP  ));		//6
		this->lines->setPoint(10,bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_TOP  ));	//7
		this->lines->setPoint(11,bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_BOTTOM ));	//5
		this->lines->setPoint(12,bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_TOP  ));		//6
		this->lines->setPoint(13,bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_BOTTOM ));	//4
		this->lines->setPoint(14,bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_BOTTOM ));	//5
		this->lines->setPoint(15,bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));	//2
		this->lines->setPoint(16,bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_TOP  ));	//3
		this->lines->setPoint(17,bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_TOP  ));	//7
		this->lines->setPoint(18,bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));	//2

		this->lines->update();
		node->needUpdate();
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	void ColoredBoundingBox::setup()
	{
		
		this->node = Engine::getSingleton().getSceneManager()->getRootSceneNode()->createChildSceneNode();
		this->node->attachObject(lines.get());

		Ogre::AxisAlignedBox bb;
		if(this->entity != NULL)
		{
			bb = entity->getWorldBoundingBox();
		}
		else if(boxNode != NULL)
		{
			this->boxNode->_update(true,false);
			bb = this->boxNode->_getWorldAABB();
		}
		else
		{
			oAssert(!"No Valid Entity or Node for BoundingBox given!");
		}

		//     6_______ 7
		//    /|	  /|
		// 	1/_|____3/ | 
		//	|  |_ _ |_ |
		//	| /4	| /5		
		//	|/______|/
		//  0       2
		//
		//		01235746045205167317//51 crossed middle line
		//		01235746045205761371//57 line drawed twice
		//		2102406136756452372
		/*
		lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_BOTTOM ));		//0
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_TOP  ));		//1
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));	//2
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_TOP  ));		//3
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_BOTTOM ));		//5
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_TOP  ));		//7
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_BOTTOM ));		//4
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_TOP  ));		//6
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_BOTTOM ));		//0
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_BOTTOM ));		//4
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_BOTTOM ));		//5
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));	//2
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_BOTTOM ));		//0
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_BOTTOM ));		//5
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_TOP  ));		//7
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_TOP  ));		//6
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_TOP  ));		//1
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_TOP  ));		//3
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_TOP  ));		//7
				lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_TOP  ));		//1*/
		
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));	//2
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_TOP  ));		//1
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_BOTTOM ));		//0
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));	//2
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_BOTTOM ));		//4
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_BOTTOM ));		//0
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_TOP  ));		//6
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_LEFT_TOP  ));		//1
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_TOP  ));		//3
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_TOP  ));		//6
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_TOP  ));		//7
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_BOTTOM ));		//5
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_TOP  ));		//6
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_LEFT_BOTTOM ));		//4
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_BOTTOM ));		//5
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));	//2
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_TOP  ));		//3
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::FAR_RIGHT_TOP  ));		//7
		this->lines->addPoint(bb.getCorner(Ogre::AxisAlignedBox::NEAR_RIGHT_BOTTOM ));	//2

		this->lines->update();
		this->node->needUpdate();
	}
	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
