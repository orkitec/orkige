/**************************************************************
	created:	2010/09/07 at 2:09
	filename: 	ColoredBoundingBox.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __ColoredBoundingBox_h__7_9_2010__2_09_24__
#define __ColoredBoundingBox_h__7_9_2010__2_09_24__

#include <core_base/Meta.h>
#include "engine_graphic/DynamicLines.h"

namespace Orkige
{
	//! colored AxisAlignedBoundingBox for Ogre::SceneNode and Ogre::Entity's
	class ORKIGE_DLL ColoredBoundingBox
	{
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		optr<DynamicLines> lines;
		Ogre::SceneNode *node;
		Ogre::Entity * entity;
		Ogre::SceneNode *boxNode;
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------
	public:
		//! construct colored Entity BoundingBox
		ColoredBoundingBox(Ogre::Entity * entity,Ogre::ColourValue colour);
		//! construct colored SceneNode BoundingBox
		ColoredBoundingBox(Ogre::SceneNode * sn,Ogre::ColourValue colour);
		//! construct colored BoundingBox from extents
		ColoredBoundingBox(Ogre::Vector3 const & nearLeftBottom, Ogre::Vector3 const & farRightTop ,Ogre::ColourValue colour);
		//! destructor
		virtual ~ColoredBoundingBox();
		//! change the line colour 
		void setColour(Ogre::ColourValue colour); 
		//! Call this to update the hardware buffer after making changes.  
		void update(Ogre::Vector3 const & nearLeftBottom = Ogre::Vector3::ZERO, Ogre::Vector3 const & farRightTop = Ogre::Vector3::ZERO); 
	protected:
	private:
		void setup(Ogre::Vector3 const & nearLeftBottom = Ogre::Vector3::ZERO, Ogre::Vector3 const & farRightTop = Ogre::Vector3::ZERO);
	};
	//---------------------------------------------------------
}

#endif //__ColoredBoundingBox_h__7_9_2010__2_09_24__
