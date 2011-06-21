/********************************************************************
	created:	Monday 2010/08/16 at 12:17
	filename: 	ProfileManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/

#include "core_debug/ProfileManager.h"
#include "core_util/Timer.h"
#include "core_util/StringUtil.h"
#include <limits>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
namespace Orkige
{
	IMPL_OSINGLETON_GETCREATE(ProfileManager);
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ProfileManager::ProfileManager() : root("root", NULL), frameCounter(0), currentNode(&root)
	{
		oInfo("\t...ProfileManager created!...");
	}
	//---------------------------------------------------------
	ProfileManager::~ProfileManager()
	{
		oInfo("\t...ProfileManager destroyed!...");
	}
	//---------------------------------------------------------
	void ProfileManager::startProfile(const char* name)
	{
		//if (strcmp(name, ProfileManager::currentNode->name) != 0)
		if (name != this->currentNode->name)
			this->currentNode = this->currentNode->getSubNode(name);

		this->currentNode->nodeCall();
	}
	//---------------------------------------------------------
	void ProfileManager::startProfile(String const & name)
	{
		if (this->currentNode->name != NULL || static_cast<StringProfileNode*>(this->currentNode)->stringName != name)
			this->currentNode = this->currentNode->getSubNode(name);

		this->currentNode->nodeCall();
	}
	//---------------------------------------------------------
	void ProfileManager::stopProfile()
	{
		// nodeReturn will indicate whether we should back up to our parent (we may
		// be profiling a recursive function)
		if (this->currentNode->nodeReturn())
			this->currentNode = this->currentNode->getParent();
	}
	//---------------------------------------------------------
	void ProfileManager::reset()
	{ 
		this->root.nodeReset(); 
		this->frameCounter = 0;
		this->root.startTime = Timer::getMilliseconds();
	}
	//---------------------------------------------------------
	unsigned long ProfileManager::getTimeSinceReset()
	{
		return Timer::getMilliseconds() - this->root.startTime;
	}
	//---------------------------------------------------------
	void ProfileManager::debugOutput()
	{
		oDebugMsg("profiler", 0," ProfileManager: hierarchical view");
		oDebugMsg("profiler", 0," %total  |%parent | ms/frame | ms/call | ms/call | ms/call  | calls/  | name");
		oDebugMsg("profiler", 0,"         |        |          |         | min     | max      | frame   |     ");
		oDebugMsg("profiler", 0,"---------+--------+----------+---------+---------+----------+---------+-----------------------------------------");

		this->root.totalTime = this->getTimeSinceReset();
		this->debugOutputInternal(this->root.getChild(), &this->root, 0);
	}
	//---------------------------------------------------------
	void ProfileManager::debugOutputFlat()
	{
		oDebugMsg("profiler", 0," ProfileManager: non-hierarchical view");
		oDebugMsg("profiler", 0," %total  |%parent | ms/frame | ms/call | ms/call | ms/call  | calls/  | name");
		oDebugMsg("profiler", 0,"         |        |          |         | min     | max      | frame   |     ");
		oDebugMsg("profiler", 0,"---------+--------+----------+---------+---------+----------+---------+-----------------------------------------");

		// build the flat view
		ProfileNode flatRoot("flat root", NULL);
		flatRoot.totalTime = this->getTimeSinceReset();
		this->buildFlatHierarchy(&flatRoot, this->root.getChild());
		this->debugOutputInternal(flatRoot.getChild(), &flatRoot, 0);
		flatRoot.nodeFree();
	}
	//---------------------------------------------------------
	void ProfileManager::shut()
	{
		this->root.nodeFree();
	}	
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
	void ProfileManager::debugOutputInternal(ProfileNode* node, ProfileNode* rootNode, int treeDepth)
	{
		int childNumber = this->getNumSiblings(node);

		String indent(treeDepth*4, ' ');


		while (childNumber > 0)
		{
			childNumber--;
			node = this->getSibling(node, childNumber);

			double f = 1000.0;//(double)getTimerFrequency();

			if (node->getMinTime() != UINT_MAX)
			{
				int frameCount = this->getFrameCountSinceReset();

				double outputParent = 100.0 * (double)node->getTotalTime() / (double)node->getParent()->getTotalTime();
				double outputTotal = 100.0 * (double)node->getTotalTime() / (double)rootNode->getTotalTime();
				double outputMsFrame = (frameCount != 0) ? (1000.0 * (double)node->getTotalTime() / (double)(frameCount * f)) : 0;
				double outputMsCall = 1000.0 * (double)node->getTotalTime() / (double)(node->getTotalCalls() * f);
				double outputMsMin = 1000.0 * (double)node->getMinTime() / f;
				double outputMsMax = 1000.0 * (double)node->getMaxTime() / f;
				double outputCallFrame = (frameCount != 0) ? ((double)node->getTotalCalls() / (double)frameCount) : 0;
				String outputName = indent + node->getName();

				oDebugMsg("profiler", 0, \
					StringUtil::doubleToString(outputTotal,		3,7,' ',std::ios::fixed)	<<" |"<<\
					StringUtil::doubleToString(outputParent,	3,7,' ',std::ios::fixed)	<<" |"<<\
					StringUtil::doubleToString(outputMsFrame,	3,9,' ',std::ios::fixed)	<<" |"<<\
					StringUtil::doubleToString(outputMsCall,	3,8,' ',std::ios::fixed)	<<" |"<<\
					StringUtil::doubleToString(outputMsMin,		3,8,' ',std::ios::fixed)	<<" |"<<\
					StringUtil::doubleToString(outputMsMax,		3,9,' ',std::ios::fixed)	<<" |"<<\
					StringUtil::doubleToString(outputCallFrame,	3,8,' ',std::ios::fixed)	<<" |"<<\
					outputName);

				this->debugOutputInternal(node->getChild(), rootNode, treeDepth + 1);
			}
		}
	}
	//---------------------------------------------------------
	void ProfileManager::buildFlatHierarchy(ProfileNode* outputNode, ProfileNode* inputNode)
	{
		int childNumber = this->getNumSiblings(inputNode);

		while (childNumber > 0)
		{
			--childNumber;
			inputNode = this->getSibling(inputNode, childNumber);

			//double f = (double)getTimerFrequency();

			if (inputNode->getMinTime() != UINT_MAX)
			{
				// get or create child node for flat hierarchy
				ProfileNode* outputChild = outputNode->getSubNode(inputNode->getName());

				outputChild->totalCalls += inputNode->getTotalCalls();
				outputChild->totalTime += inputNode->getTotalTime();
				outputChild->minTime = std::min(inputNode->getMinTime(), outputChild->minTime);
				outputChild->maxTime = std::max(inputNode->getMaxTime(), outputChild->maxTime );

				this->buildFlatHierarchy(outputNode, inputNode->getChild());
			}
		}
	}
	//---------------------------------------------------------
	int ProfileManager::getNumSiblings(ProfileNode* node)
	{
		if (node == NULL)
			return 0;

		ProfileNode* current = node->getParent()->getChild();
		int i = 0;
		while (current != NULL)
		{
			i++;
			current = current->getSibling();
		}
		return i;
	}
	//---------------------------------------------------------
	ProfileNode* ProfileManager::getSibling(ProfileNode* node, int index)
	{
		ProfileNode* current = node->getParent()->getChild();
		while (current != NULL && index != 0)
		{
			index--;
			current = current->getSibling();
		}
		return current;
	}
	//---------------------------------------------------------
}
