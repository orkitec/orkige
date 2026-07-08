/**************************************************************
	created:	2010/08/24 at 0:41
	filename: 	CollisionTools.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "engine_physic/CollisionTools.h"
#include "engine_graphic/Engine.h"
#include "engine_util/MeshUtil.h"
#include <core_util/foreach.h>

namespace Orkige
{
	IMPL_OSINGLETON(CollisionTools);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	CollisionTools::CollisionTools()
	{
		this->sceneManager = Engine::getSingleton().getSceneManager();

		this->raySceneQuery = this->sceneManager->createRayQuery(Ogre::Ray());
		oAssert(this->raySceneQuery);
		this->raySceneQuery->setSortByDistance(true);

		this->terrainRaySceneQuery = this->sceneManager->createRayQuery(Ogre::Ray());
		oAssert(this->terrainRaySceneQuery);
	}
	//---------------------------------------------------------
	CollisionTools::~CollisionTools()
	{
		if (this->raySceneQuery != NULL)
			delete this->raySceneQuery;

		if (this->terrainRaySceneQuery != NULL)
			delete this->terrainRaySceneQuery;
	}
	//---------------------------------------------------------
	bool CollisionTools::raycastFromCamera(Ogre::RenderWindow* rw, Ogre::Camera* camera, const Ogre::Vector2 &mousecoords, Ogre::Vector3 &result, Ogre::Entity* &target,float &closest_distance, const Ogre::uint32 queryMask, bool checkAnimatedMesh)
	{
		return this->raycastFromCamera(rw, camera, mousecoords, result, (Ogre::MovableObject*&) target, closest_distance, queryMask, checkAnimatedMesh);
	}
	//---------------------------------------------------------
	bool CollisionTools::raycastFromCamera(Ogre::RenderWindow* rw, Ogre::Camera* camera, const Ogre::Vector2 &mousecoords, Ogre::Vector3 &result, Ogre::MovableObject* &target,float &closest_distance, const Ogre::uint32 queryMask, bool checkAnimatedMesh)
	{
        Ogre::Real tx = mousecoords.x / (Ogre::Real) rw->getWidth();
		Ogre::Real ty = mousecoords.y / (Ogre::Real) rw->getHeight();
		Ogre::Ray ray = camera->getCameraToViewportRay(tx, ty);

		return this->raycast(ray, result, target, closest_distance, queryMask, checkAnimatedMesh);
	}
	//---------------------------------------------------------
	bool CollisionTools::collidesWithEntity(const Ogre::Vector3& fromPoint, const Ogre::Vector3& toPoint, const float collisionRadius, const float rayHeightLevel, const Ogre::uint32 queryMask, bool checkAnimatedMesh)
	{
		Ogre::MovableObject* myObject = NULL;
		return this->collidesWithEntity(fromPoint, toPoint, myObject, collisionRadius, rayHeightLevel, queryMask);
	}
	//---------------------------------------------------------
	bool CollisionTools::collidesWithEntity(const Ogre::Vector3& fromPoint, const Ogre::Vector3& toPoint, Ogre::MovableObject* &target, const float collisionRadius, const float rayHeightLevel, const Ogre::uint32 queryMask, bool checkAnimatedMesh)
	{
		Ogre::Vector3 fromPointAdj(fromPoint.x, fromPoint.y + rayHeightLevel, fromPoint.z);
		Ogre::Vector3 toPointAdj(toPoint.x, toPoint.y + rayHeightLevel, toPoint.z);
		Ogre::Vector3 normal = toPointAdj - fromPointAdj;
		float distToDest = normal.normalise();

		Ogre::Vector3 myResult(0, 0, 0);

		float distToColl = 0.0f;

		if (this->raycastFromPoint(fromPointAdj, normal, myResult, target, distToColl, queryMask))
		{
			distToColl -= collisionRadius;
			return (distToColl <= distToDest);
		}
		else
		{
			return false;
		}
	}
	//---------------------------------------------------------
	float CollisionTools::getTerrainHeightAt(const float x, const float z) 
	{
		float y = 0.0f;

		// OGRE 14 removed world geometry scene queries (WorldFragment is only a
		// forward declaration now), so the old TSM terrain height lookup is gone.
		// Successor: cast a downward ray through PhysicsWorld::castRay (Jolt)
		// against the physics representation of the ground instead.
		(void)x;
		(void)z;
		return y;
	}
	//---------------------------------------------------------
	void CollisionTools::setOnGround(Ogre::SceneNode *n, const bool doTerrainCheck, const bool doGridCheck, const float gridWidth, Ogre::Real heightAdjust, const Ogre::uint32 queryMask)
	{
		Ogre::Vector3 pos = n->getPosition();

		float x = pos.x;
		float z = pos.z;
		float y = pos.y;

		Ogre::Vector3 myResult(0,0,0);
		Ogre::MovableObject *myObject = NULL;
		float distToColl = 0.0f;

		float terrY = 0, colY = 0, colY2 = 0;

		if( this->raycastFromPoint(Ogre::Vector3(x,y,z), Ogre::Vector3::NEGATIVE_UNIT_Y, myResult, myObject, distToColl, queryMask))
		{
			if (myObject != NULL) 
			{
				colY = myResult.y;
			} 
			else 
			{
				colY = -99999;
			}
		}

		//if doGridCheck is on, repeat not to fall through small holes for example when crossing a hangbridge
		if (doGridCheck) 
		{
			if( this->raycastFromPoint(Ogre::Vector3(x,y,z)+(n->getOrientation()*Ogre::Vector3(0,0,gridWidth)),Ogre::Vector3::NEGATIVE_UNIT_Y,myResult, myObject, distToColl, queryMask))
			{
				if (myObject != NULL) 
				{
					colY = myResult.y;
				} 
				else 
				{
					colY = -99999;
				}
			}
			if (colY<colY2) colY = colY2;
		}

		// set the parameter to false if you are not using ETM or TSM
		if (doTerrainCheck) 
		{

			// TSM height value
			terrY = this->getTerrainHeightAt(x,z);

			if(terrY < colY ) 
			{
				n->setPosition(x,colY+heightAdjust,z);
			} 
			else 
			{
				n->setPosition(x,terrY+heightAdjust,z);
			}
		} 
		else 
		{
			if (!doTerrainCheck && colY == -99999) 
			{
				colY = y;
			}
			n->setPosition(x,colY+heightAdjust,z);
		}
	}
	//---------------------------------------------------------
	bool CollisionTools::raycastFromPoint(const Ogre::Vector3 &point, const Ogre::Vector3 &normal, Ogre::Vector3 &result,Ogre::Entity* &target, float &closest_distance, const Ogre::uint32 queryMask, bool checkAnimatedMesh) 
	{
		return this->raycastFromPoint(point, normal, result, (Ogre::MovableObject*&) target, closest_distance, queryMask, checkAnimatedMesh);
	}		
	//---------------------------------------------------------
	bool CollisionTools::raycastFromPoint(const Ogre::Vector3 &point, const Ogre::Vector3 &normal, Ogre::Vector3 &result,Ogre::MovableObject* &target, float &closest_distance, const Ogre::uint32 queryMask, bool checkAnimatedMesh)
	{
		// create the ray to test
		static Ogre::Ray ray;
		ray.setOrigin(point);
		ray.setDirection(normal);
		return this->raycast(ray, result, target, closest_distance, queryMask, checkAnimatedMesh);
	}
	//---------------------------------------------------------
	bool CollisionTools::raycast(const Ogre::Ray &ray, Ogre::Vector3 &result,Ogre::Entity* &target,float &closest_distance, const Ogre::uint32 queryMask, bool checkAnimatedMesh) 
	{
		return this->raycast(ray, result, (Ogre::MovableObject*&)target, closest_distance, queryMask, checkAnimatedMesh);
	}
	//---------------------------------------------------------
	bool CollisionTools::raycast(const Ogre::Ray &ray, Ogre::Vector3 &result,Ogre::MovableObject* &target,float &closest_distance, const Ogre::uint32 queryMask, bool checkAnimatedMesh)
	{
		OPROFILEFUNC();
		target = NULL;

		// check we are initialised
		if (this->raySceneQuery != NULL)
		{
			// create a query object
			this->raySceneQuery->setRay(ray);
			this->raySceneQuery->setSortByDistance(true);
			this->raySceneQuery->setQueryMask(queryMask);
			// execute the query, returns a vector of hits
			if (this->raySceneQuery->execute().size() <= 0)
			{
				// raycast did not hit an objects bounding box
				return false;
			}
		}
		else
		{
			//LOG_ERROR << "Cannot raycast without RaySceneQuery instance" << ENDLOG;
			return false;
		}

		// at this point we have raycast to a series of different objects bounding boxes.
		// we need to test these different objects to see which is the first polygon hit.
		// there are some minor optimizations (distance based) that mean we wont have to
		// check all of the objects most of the time, but the worst case scenario is that
		// we need to test every triangle of every object.
		//Ogre::Ogre::Real closest_distance = -1.0f;
		closest_distance = -1.0f;
		Ogre::Vector3 closest_result;
		// OGRE 14: getLastResults() returns a const reference
		Ogre::RaySceneQueryResult const &query_result = this->raySceneQuery->getLastResults();
		foreach (Ogre::RaySceneQueryResultEntry const & res, query_result)
		{
			// stop checking if we have found a raycast hit that is closer
			// than all remaining entities
			if ((closest_distance >= 0.0f) && (closest_distance < res.distance))
			{
				break;
			}

			// only check this result if its a hit against an entity
			if ((res.movable != NULL) && (res.movable->getQueryFlags() == queryMask) &&(res.movable->getMovableType().compare("Entity") == 0) )
			{
				// get the entity to check
				Ogre::Entity *pentity = static_cast<Ogre::Entity*>(res.movable);

				// mesh data to retrieve
				size_t vertex_count;
				size_t index_count;
				Ogre::Vector3 *vertices;
				Ogre::uint32 *indices;

				if (checkAnimatedMesh)
				{
					MeshUtil::getMeshInformationWithAnimation(pentity, vertex_count, vertices, index_count, indices,
						pentity->getParentNode()->_getDerivedPosition(),
						pentity->getParentNode()->_getDerivedOrientation(),
						pentity->getParentNode()->_getDerivedScale());
				}
				else
				{
					MeshUtil::getMeshInformation(pentity->getMesh(), vertex_count, vertices, index_count, indices,
						 pentity->getParentNode()->_getDerivedPosition(),
						 pentity->getParentNode()->_getDerivedOrientation(),
						 pentity->getParentNode()->_getDerivedScale());
				}

				// test for hitting individual triangles on the mesh
				bool new_closest_found = false;
				for (size_t i = 0; i < index_count; i += 3)
				{
					// check for a hit against this triangle
					std::pair<bool, Ogre::Real> hit = Ogre::Math::intersects(ray, vertices[indices[i]], vertices[indices[i+1]], vertices[indices[i+2]], true, false);

					// if it was a hit check if its the closest
					if (hit.first)
					{
						if ((closest_distance < 0.0f) || (hit.second < closest_distance))
						{
							// this is the closest so far, save it off
							closest_distance = hit.second;
							new_closest_found = true;
						}
					}
				}

				// free the vertices and indices memory
				delete[] vertices;
				delete[] indices;

				// if we found a new closest raycast for this object, update the
				// closest_result before moving on to the next object.
				if (new_closest_found)
				{
					target = pentity;
					closest_result = ray.getPoint(closest_distance);
				}
			}
		}

		// return the result
		if (closest_distance >= 0.0f)
		{
			// raycast success
			result = closest_result;
			return (true);
		}
		else
		{
			// raycast failed
			return (false);
		}
	}

	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------
	

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}
