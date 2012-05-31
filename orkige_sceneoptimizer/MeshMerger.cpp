#include <MeshMerger.h>
#include <engine_util/NodeUtil.h>
#include <engine_util/StringUtil.h>
#include <engine_util/MeshUtil.h>
#include "FileUtils.h"
#include "boost/foreach.hpp"

using namespace Orkige;

namespace SceneOptimizer
{
	MeshMerger::MeshMerger () : levelNode (0), serializer (0)
	{
		this->loader = new DotSceneLoader ();
	}

	MeshMerger::~MeshMerger()
	{
	}

	void MeshMerger::loadScene (const Ogre::String &sceneName)
	{
		this->reset ();
		this->levelNode = Engine::getSingleton ().getSceneManager ()->getRootSceneNode ()->createChildSceneNode ("Level");
		this->sceneName = sceneName;
		this->loader->parseDotScene (this->sceneName, "General", Engine::getSingleton ().getSceneManager (), this->levelNode);
	}

	void MeshMerger::prepareMerge ()
	{
		Ogre::SceneManager::MovableObjectIterator iterator = Engine::getSingleton ().getSceneManager ()->getMovableObjectIterator (Ogre::EntityFactory::FACTORY_TYPE_NAME);
		while (iterator.hasMoreElements ())
		{
			Ogre::Entity* entity = static_cast<Ogre::Entity*> (iterator.getNext ());
			oAssert (entity);
			Ogre::SceneNode* sceneNode = entity->getParentSceneNode ();
			oAssert (sceneNode);
			entityNodes.push_back (sceneNode);
		}

		foreach(Ogre::SceneNode* sceneNode, entityNodes)
		{
			for (unsigned short i = 0; i < sceneNode->numAttachedObjects (); i++)
			{
				Ogre::MovableObject* movableObject = sceneNode->getAttachedObject (i);
				if (movableObject->getMovableType () == Ogre::EntityFactory::FACTORY_TYPE_NAME)
				{
					Ogre::Entity* e = static_cast<Ogre::Entity*> (movableObject);
					oAssert (e);		
					Orkige::String materialName = e->getSubEntity(0)->getMaterialName();
					//getMesh ()->getSubMesh (0)->getMaterialName ();

					meshObjects.insert (std::pair <Ogre::String, Ogre::Entity*> (materialName, e));
					materialList.insert (materialName);
				}
			}
		}
	}

	void MeshMerger::merge ()
	{
		this->prepareMerge ();
		std::string outputFolder = FileUtils::DialogBrowseFolder (("Choose output directory")).c_str();

		// go through every key / material name
		for (std::set<const Ogre::String>::iterator it = materialList.begin (); it != materialList.end (); it++)
		{
			//if there is only one mesh with the same material, ignore it and continue with the next material
			if (meshObjects.count (*it) <= 1)
				continue;

			// get every entity with the same material name
			std::pair<std::multimap<const Ogre::String, Ogre::Entity*>::iterator, std::multimap<const Ogre::String, Ogre::Entity*>::iterator> range;
			range = meshObjects.equal_range (*it);

			

			//create new manual object for coming vertices, etc.
			Ogre::ManualObject* manualObject = Engine::getSingleton ().getSceneManager ()->createManualObject (*it);
			manualObject->begin (*it, Ogre::RenderOperation::OT_TRIANGLE_LIST);
			int face = 0;

			for (std::multimap<const Ogre::String, Ogre::Entity*>::iterator jt = range.first; jt != range.second; jt++)
			{
				size_t vertex_count;
				size_t index_count;
				Ogre::Vector3 *vertices;
				Ogre::Vector3 *normals;
				Ogre::Vector3 *texcoords;
				Ogre::RGBA *vertexColors;
				Ogre::Vector3* tangents;
				Ogre::uint32 *indices;

				Orkige::MeshUtil::getMeshInformationWithColors((*jt).second->getMesh(), 
					vertex_count, vertices, normals, texcoords, vertexColors, tangents, index_count, indices, 
					(*jt).second->getParentNode()->_getDerivedPosition(),
					(*jt).second->getParentNode()->_getDerivedOrientation(),
					(*jt).second->getParentNode()->_getDerivedScale());

				size_t trianglesNeeded = index_count/3;

				int i =0;
				Ogre::ColourValue color;
				Ogre::Vector3 tangent;

				for (size_t x=0; x<trianglesNeeded; x++)
				{
					manualObject->triangle(face, face+1, face+2);

					manualObject->position(vertices[indices[i]]);
					manualObject->normal(normals[indices[i]]);
					manualObject->textureCoord(texcoords[indices[i]]);

					if (vertexColors[i])
					{
						color.setAsABGR (vertexColors[indices[i]]);
						manualObject->colour(color);
					}

					tangent = tangents[i];
						manualObject->tangent(tangent);
					

					manualObject->position(vertices[indices[i+1]]);
					manualObject->normal(normals[indices[i+1]]);
					manualObject->textureCoord(texcoords[indices[i+1]]);
					if (vertexColors[i+1])
					{
						color.setAsABGR (vertexColors[indices[i+1]]);
						manualObject->colour(color);
					}

					tangent = tangents[i+1];
					manualObject->tangent(tangent);

					manualObject->position(vertices[indices[i+2]]);
					manualObject->normal(normals[indices[i+2]]);
					manualObject->textureCoord(texcoords[indices[i+2]]);
					if (vertexColors[i+2])
					{
						color.setAsABGR (vertexColors[indices[i+2]]);
						manualObject->colour(color);
					}

					if(!tangents[i+2].isNaN())
					{
						tangent = tangents[i+2];
						manualObject->tangent(tangent);
					}
					
									
					i += 3;
					face += 3;
				}
			}

			manualObject->end();
			Ogre::MeshPtr mesh = manualObject->convertToMesh (*it);
			std::string fileName = FileUtils::RemoveExtensionFromFile (this->sceneName);
			this->serializer = new Ogre::MeshSerializer ();
			this->serializer->exportMesh(mesh.get(), outputFolder + "\\" + fileName + "_" + (*it) + ".mesh");
		}
	}

	void MeshMerger::reset ()
	{
		Engine::getSingleton().getSceneManager()->destroyAllLights();
		foreach (Ogre::Camera* camera, this->loader->cameras)
		{
			Engine::getSingleton().getSceneManager()->destroyCamera(camera);
		}
		this->loader->cameras.clear();

		if(this->levelNode)
		{
			Orkige::NodeUtil::wipeSceneNode(this->levelNode);
		}

		// delete every member for being sure nothing is left from previous loaded scenes
		materialList.clear ();
		meshObjects.clear ();
		entityNodes.clear ();

		if (this->serializer)
			delete this->serializer;

		Engine::getSingleton().getSceneManager()->destroyAllManualObjects ();
		Engine::getSingleton().getSceneManager()->destroyAllMovableObjects ();
		Engine::getSingleton().getSceneManager()->destroyAllEntities ();
	}

	/*void meshmerger::optimize (ogre::stringstream meshpath)
	{
		std::stringstream command;
		command << "start meshmagick.exe optimise " << ;

		std::cout << "cmd: " << command.str().c_str() << std::endl;
		int returnvalue = system(command.str().c_str());
		std::cout << "cmd returned: " << returnvalue << std::endl;

		if (returnvalue != 0)
		{
			std::cerr << "error optimizing mesh with associated program or no program associated with mesh files" << std::endl;
			return -1;
		}

		return 0;
	}*/
}
