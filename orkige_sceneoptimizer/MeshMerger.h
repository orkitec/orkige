#include "DotSceneLoader.h"
#include <engine_graphic/Engine.h>
#include <map>
#include <set>

namespace SceneOptimizer
{
	class MeshMerger
	{
	public:
		MeshMerger ();
		~MeshMerger ();

		void loadScene (const Ogre::String &sceneName);
		void prepareMerge ();
		void merge ();
		void writeMeshFile (const Ogre::String &meshFileName, Ogre::ManualObject* manualObject);
		void reset ();
		//void optimize (Ogre::String meshPath);

	protected:
	private:
		std::set <const Ogre::String> materialList;
		std::multimap <const Ogre::String, Ogre::Entity*> meshObjects;
		DotSceneLoader* loader;
		Ogre::SceneNode* levelNode;
		std::vector <Ogre::SceneNode*> entityNodes;
		Ogre::MeshSerializer* serializer;
		Ogre::String sceneName;
	};
}
