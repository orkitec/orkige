/**************************************************************
	created:	2010/08/24 at 0:33
	filename: 	CollisionTools.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec

	Based on:
	MOC - Minimal Ogre Collision v 1.0
	The MIT License

	Copyright (c) 2008, 2009 MouseVolcano (Thomas Gradl, Karolina Sefyrin), Esa Kylli

	Thanks to Erik Biermann for the help with the Videos, SEO and Webwork

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.

	@TODO:		rewrite/cleanup without MOC & MIT code
***************************************************************/
#ifndef __CollisionTools_h__24_8_2010__0_33_41__
#define __CollisionTools_h__24_8_2010__0_33_41__

#include "engine_module/EnginePrerequisites.h"
#include <core_util/Singleton.h>

namespace Orkige
{
	//! basic collision utilities
	class ORKIGE_ENGINE_DLL CollisionTools : public Singleton<CollisionTools>
	{
		DECL_OSINGLETON(CollisionTools);
		//--- Types -------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		Ogre::RaySceneQuery *raySceneQuery;
		Ogre::RaySceneQuery *terrainRaySceneQuery;
		Ogre::SceneManager	*sceneManager;
		//--- Methods -----------------------------------------
	public:
		//! constructor
		CollisionTools();
		//! destructor
		~CollisionTools();
		//! check if there is a entity collision between to points
		bool collidesWithEntity(const Ogre::Vector3& fromPoint, const Ogre::Vector3& toPoint, const float collisionRadius = 2.5f, const float rayHeightLevel = 0.0f, const Ogre::uint32 queryMask = 0xFFFFFFFF, bool checkAnimatedMesh = true);
		//! collidesWithEntity
		bool collidesWithEntity(const Ogre::Vector3& fromPoint, const Ogre::Vector3& toPoint, Ogre::MovableObject* &target, const float collisionRadius = 2.5f, const float rayHeightLevel = 0.0f, const Ogre::uint32 queryMask = 0xFFFFFFFF, bool checkAnimatedMesh = true);
		//! set SceneNode on ground
		void setOnGround(Ogre::SceneNode *n, const bool doTerrainCheck = true, const bool doGridCheck = true, const float gridWidth = 1.0f, Ogre::Real heightAdjust = 0.f, const Ogre::uint32 queryMask = 0xFFFFFFFF);
		//! get Terrain height at given Position
		float getTerrainHeightAt(const float x, const float z);
		//! @brief scene RayCast using Ogre::Ray
		//! @return true on hit
		//! on success the point is returned in the result.
		bool raycast(const Ogre::Ray &ray, Ogre::Vector3 &result, Ogre::MovableObject* &target,float &closest_distance, const Ogre::uint32 queryMask = 0xFFFFFFFF, bool checkAnimatedMesh = true);
		//! raycast convenience wrapper with Ogre::Entity to it:
		bool raycast(const Ogre::Ray &ray, Ogre::Vector3 &result, Ogre::Entity* &target,float &closest_distance, const Ogre::uint32 queryMask = 0xFFFFFFFF, bool checkAnimatedMesh = true);
		//! @brief raycast from a point in to the scene.
		//! @return true on hit
		//! on success the point is returned in the result.
		bool raycastFromPoint(const Ogre::Vector3 &point, const Ogre::Vector3 &normal, Ogre::Vector3 &result,Ogre::MovableObject* &target,float &closest_distance, const Ogre::uint32 queryMask = 0xFFFFFFFF, bool checkAnimatedMesh = true);
		//! raycastFromPoint convenience wrapper with Ogre::Entity to it:
		bool raycastFromPoint(const Ogre::Vector3 &point, const Ogre::Vector3 &normal, Ogre::Vector3 &result,Ogre::Entity* &target,float &closest_distance, const Ogre::uint32 queryMask = 0xFFFFFFFF, bool checkAnimatedMesh = true);
		//! raycast from given Ogre::Camera and Ogre::RenderWindow
		bool raycastFromCamera(Ogre::RenderWindow* rw, Ogre::Camera* camera, const Ogre::Vector2 &mousecoords, Ogre::Vector3 &result, Ogre::MovableObject* &target,float &closest_distance, const Ogre::uint32 queryMask = 0xFFFFFFFF, bool checkAnimatedMesh = true);
		//! raycastFromCamera convenience wrapper with Ogre::Entity to it:
		bool raycastFromCamera(Ogre::RenderWindow* rw, Ogre::Camera* camera, const Ogre::Vector2 &mousecoords, Ogre::Vector3 &result, Ogre::Entity* &target,float &closest_distance, const Ogre::uint32 queryMask = 0xFFFFFFFF, bool checkAnimatedMesh = true);
		
	protected:
	private:
	};
	//---------------------------------------------------------
}

#endif //__CollisionTools_h__24_8_2010__0_33_41__
