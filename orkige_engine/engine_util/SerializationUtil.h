/**************************************************************
	created:	2010/08/31 at 0:43
	filename: 	SerializationUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __SerializationUtil_h__31_8_2010__0_43_47__
#define __SerializationUtil_h__31_8_2010__0_43_47__

#include "engine_module/EnginePrerequisites.h"
#include <core_util/optr.h>
#include <core_serialization/IArchive.h>

namespace Orkige
{
	//! stream Ogre::Vector3 to IArchive
	inline optr<IArchive> const & operator << ( optr<IArchive> const & ar, Ogre::Vector3 & vec ) 
	{
		ar << vec.x;
		ar << vec.y;
		ar << vec.z;
		return ar;
	}
	//---------------------------------------------------------
	//! stream Ogre::Quaternion to IArchive
	inline optr<IArchive> const & operator << ( optr<IArchive> const & ar, Ogre::Quaternion & q ) 
	{
		ar << q.w;
		ar << q.x;
		ar << q.y;
		ar << q.z;
		return ar;
	}
	//---------------------------------------------------------
	//! stream Ogre::Vector3 from IArchive
	inline optr<IArchive> const & operator >> ( optr<IArchive> const & ar, Ogre::Vector3 & vec ) 
	{
		ar >> vec.x;
		ar >> vec.y;
		ar >> vec.z;
		return ar;
	}
	//---------------------------------------------------------
	//! stream Ogre::Quaternion from IArchive
	inline optr<IArchive> const & operator >> ( optr<IArchive> const & ar, Ogre::Quaternion & q ) 
	{
		ar >> q.w;
		ar >> q.x;
		ar >> q.y;
		ar >> q.z;
		return ar;
	}
	//---------------------------------------------------------
	//! Serialization utilities
	namespace SerializationUtil
	{
		//! save a Ogre::SceneNode to IArchive
		void saveSceneNode(Ogre::SceneNode * node, optr<IArchive> const & ar, const unsigned int file_version);
		//! save a Ogre::MovableObject to IArchive
		void saveMoveAbleObject(Ogre::MovableObject * object, optr<IArchive> const & ar, const unsigned int file_version);
		//! save a Ogre::Entity to IArchive
		void saveEntity(Ogre::Entity * entity, optr<IArchive> const & ar, const unsigned int file_version);
		//! save a Ogre::Light to IArchive
		void saveLight(Ogre::Light * light, optr<IArchive> const & ar, const unsigned int file_version);

		//! load a Ogre::SceneNode from IArchive
		Ogre::SceneNode * loadSceneNode(Ogre::SceneNode * parentSceneNode, optr<IArchive> const & ar, const unsigned int file_version, String const & userAnyId = "", Ogre::Any * userObject = NULL);
		//! load a Ogre::MovableObject from IArchive
		Ogre::MovableObject* loadMoveAbleObject(Ogre::SceneNode * parentSceneNode, optr<IArchive> const & ar, const unsigned int file_version);
		//! load a Ogre::Entity from IArchive
		Ogre::Entity* loadEntity(Ogre::SceneNode * parentSceneNode, optr<IArchive> const & ar, const unsigned int file_version);
		//! load a Ogre::Light from IArchive
		Ogre::Light* loadLight(Ogre::SceneNode * parentSceneNode, optr<IArchive> const & ar, const unsigned int file_version);
	}
	//---------------------------------------------------------
}

#endif //__SerializationUtil_h__31_8_2010__0_43_47__
