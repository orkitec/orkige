/********************************************************************
	created:	Monday 2010/08/16 at 12:05
	filename: 	ProfileNode.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
	note:		Using code from
				Real-Time Hierarchical Profiling for Game Programming Gems 3
				by Greg Hjelstrom & Byon Garrabrant	
*********************************************************************/
#ifndef __ProfileNode_h__16_8_2010__12_05_06__
#define __ProfileNode_h__16_8_2010__12_05_06__

#include "core_base/Meta.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Debug
	*  @{ */
	//! A node in the Profile Hierarchy Tree
	class ORKIGE_CORE_DLL ProfileNode
	{
		friend class ProfileManager;
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		const char* name;
		int totalCalls;
		unsigned long startTime;
		unsigned long totalTime;
		int recursionCounter;
		unsigned long minTime;
		unsigned long maxTime;

		ProfileNode* parent;
		ProfileNode* child;
		ProfileNode* sibling;
		//--- Methods -----------------------------------------------
	public:
		
		//! @param name pointer to a static string which is the name of this profile node.
		//! @param parent parent pointer.
		//! @warning The name is assumed to be a static pointer, only the pointer is stored and compared for efficiency reasons.
		ProfileNode(const char* name, ProfileNode* parent);
		//! destructor
		virtual ~ProfileNode();

		//! @param name static string pointer to the name of the node we are searching for.
		//! @warning All profile names are assumed to be static strings so this function uses pointer compares to find the named node.  
		ProfileNode* getSubNode(const char* name);
		//! @param name static string pointer to the name of the node we are searching for.
		ProfileNode* getSubNode(String const & name);
		//! get parent node
		inline ProfileNode* getParent();
		//! get sibling node
		inline ProfileNode* getSibling();
		//! get child 
		inline ProfileNode* getChild();

		//! reset this node
		void nodeReset();
		//! call this node
		void nodeCall();
		//! return from this node
		bool nodeReturn();
		//! free this node
		void nodeFree();

		//! get name of this node
		String getName();
		//! get number of total calls
		inline int getTotalCalls();
		//! get total time
		inline unsigned long getTotalTime();
		//! get min time from this node
		inline unsigned long getMinTime();
		//! get max time from this node
		inline unsigned long getMaxTime();
	protected:
	private:
	};
	/** @} End of "addtogroup Debug"*/
	//---------------------------------------------------------------
	inline ProfileNode* ProfileNode::getParent() 
	{ 
		return this->parent; 
	}
	//---------------------------------------------------------------
	inline ProfileNode* ProfileNode::getSibling() 
	{ 
		return this->sibling; 
	}
	//---------------------------------------------------------------
	inline ProfileNode* ProfileNode::getChild() 
	{ 
		return this->child; 
	}
	//---------------------------------------------------------------
	inline int ProfileNode::getTotalCalls() 
	{ 
		return this->totalCalls; 
	}
	//---------------------------------------------------------------
	inline unsigned long ProfileNode::getTotalTime() 
	{ 
		return this->totalTime; 
	}
	//---------------------------------------------------------------
	inline unsigned long ProfileNode::getMinTime() 
	{ 
		return this->minTime; 
	}
	//---------------------------------------------------------------
	inline unsigned long ProfileNode::getMaxTime() 
	{ 
		return this->maxTime; 
	}
	//---------------------------------------------------------------
	//! A node in the Profile Hierarchy Tree that can be constructed from a String
	class ORKIGE_CORE_DLL StringProfileNode : public ProfileNode
	{
		friend class ProfileNode;
		friend class ProfileManager;
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		inline StringProfileNode(String const & name, ProfileNode* parent);
	protected:
	private:
		String stringName;
	};
	//---------------------------------------------------------------
	inline StringProfileNode::StringProfileNode(String const & name, ProfileNode* parent) : ProfileNode(NULL, parent), stringName(name) 
	{

	}
	//---------------------------------------------------------------
}

#endif //__ProfileNode_h__16_8_2010__12_05_06__