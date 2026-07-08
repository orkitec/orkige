/********************************************************************
	created:	Monday 2010/08/16 at 12:14
	filename: 	ProfileNode.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/

#include "core_debug/ProfileNode.h"
#include "core_util/Timer.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <limits>
#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	//--- public: ---------------------------------------------
	//---------------------------------------------------------
	ProfileNode::ProfileNode(const char* name, ProfileNode* parent) 
		: name(name),
		totalCalls(0),
		startTime(0),
		totalTime(0),
		recursionCounter(0),
		minTime(UINT_MAX),
		maxTime(0),
		parent(parent),
		child(NULL),
		sibling(NULL)
	{
		this->nodeReset();
	}
	//---------------------------------------------------------
	ProfileNode::~ProfileNode()
	{
		delete this->child;
		delete this->sibling;
	}
	//---------------------------------------------------------
	ProfileNode* ProfileNode::getSubNode(const char* name)
	{
		// Try to find this sub node
		ProfileNode* c = this->child;
		while (c)
		{
			//if (strcmp(c->name, name) == 0)
			if (c->name == name)
				return c;

			c = c->sibling;
		}

		// We didn't find it, so add it
		ProfileNode* node = new ProfileNode(name, this);
		node->sibling = this->child;
		this->child = node;
		return node;
	}
	//---------------------------------------------------------
	ProfileNode* ProfileNode::getSubNode(String const & name)
	{
		// Try to find this sub node
		ProfileNode* c = this->child;
		while (c)
		{
			if (c->name == NULL && static_cast<StringProfileNode*>(c)->stringName == name)
				return c;

			c = c->sibling;
		}

		// We didn't find it, so add it
		ProfileNode* node = new StringProfileNode(name, this);
		node->sibling = this->child;
		this->child = node;
		return node;
	}
	//---------------------------------------------------------
	void ProfileNode::nodeReset()
	{
		totalCalls = 0;
		totalTime = 0;
		minTime = UINT_MAX;
		maxTime = 0;

		if (child) {
			child->nodeReset();
		}
		if (sibling) {
			sibling->nodeReset();
		}
	}
	//---------------------------------------------------------
	void ProfileNode::nodeFree()
	{
		if (this->child) 
		{
			delete this->child;
			this->child = NULL;
		}
		if (this->sibling) 
		{
			delete this->sibling;
			this->sibling = NULL;
		}
	}
	//---------------------------------------------------------
	void ProfileNode::nodeCall()
	{
		this->totalCalls++;

		if (this->recursionCounter == 0)
			this->startTime = Timer::getMilliseconds();

		this->recursionCounter++;
	}
	//---------------------------------------------------------
	bool ProfileNode::nodeReturn()
	{
		if (--this->recursionCounter == 0 && this->totalCalls != 0)
		{
			unsigned long time = Timer::getMilliseconds() - this->startTime;

			//float callTime = (float) time / profileGetTickRate();

			this->totalTime += time;
			this->minTime = std::min(minTime, time);
			this->maxTime = std::max(maxTime, time);
		}
		return (this->recursionCounter == 0);
	}
	//---------------------------------------------------------
	String ProfileNode::getName()
	{
		return this->name != NULL ? String(this->name) : static_cast<StringProfileNode*>(this)->stringName;
	}
	//---------------------------------------------------------
	//--- protected: ------------------------------------------
	//---------------------------------------------------------

	//---------------------------------------------------------
	//--- private: --------------------------------------------
	//---------------------------------------------------------
}