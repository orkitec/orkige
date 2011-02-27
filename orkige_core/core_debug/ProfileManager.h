/********************************************************************
	created:	Monday 2010/08/16 at 12:02
	filename: 	ProfileManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
	note:		Using code from
				Real-Time Hierarchical Profiling for Game Programming Gems 3
				by Greg Hjelstrom & Byon Garrabrant	
*********************************************************************/
#ifndef __ProfileManager_h__16_8_2010__12_02_16__
#define __ProfileManager_h__16_8_2010__12_02_16__

#include "core_util/Singleton.h"
#include "core_debug/ProfileNode.h"

namespace Orkige
{
	/** \addtogroup Debug
	*  @{ */
	//! The Manager for the Profile system
	class ProfileManager : public Singleton<ProfileManager>
	{
		DECL_OSINGLETON(ProfileManager);
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
	private:
		ProfileNode root;
		ProfileNode* currentNode;
		std::size_t frameCounter;
		//--- Methods -----------------------------------------------
	public:
		//! constructor
		ProfileManager();
		//! destructor
		~ProfileManager();
		//! @brief Begin a named profile.
		//! 
		//! Steps one level deeper into the tree, if a child already exists with the specified name 
		//! then it accumulates the profiling; otherwise a new child node is added to the profile tree.
		//! 
		//! @param name name of this profiling record                                                        *
		//! 
		//! @warning
		//! The string used is assumed to be a static string; pointer compares are used throughout
		//! the profiling code for efficiency.
		void startProfile(const char* name);
		//! @brief Begin a named profile.
		//! 
		//! Steps one level deeper into the tree, if a child already exists with the specified name 
		//! then it accumulates the profiling; otherwise a new child node is added to the profile tree.
		//! 
		//! @param name name of this profiling record as String
		void startProfile(String const & name);
		//! @brief used by Profile to stop timing and record the results for the active label.
		void stopProfile();

		//! @brief reset the contents of the profiling system.
		//! This resets everything except for the tree structure.  All of the timing data is reset.  
		void reset();
		//! increment the frame counter
		inline void incrementFrameCounter();
		//! get the frame count
		inline std::size_t getFrameCountSinceReset();
		//! returns the elapsed time since last reset 
		unsigned long getTimeSinceReset();

		//! @brief outputs the profiler data to the debug channel
		void debugOutput();

		//! @brief outputs the profiler data to the debug channel without hierarchy, i.e.
		//! nodes that have the same names are combined
		void debugOutputFlat();

		//! @brief shutdown
		void shut();
	protected:
	private:
		//! debug output helper function
		void debugOutputInternal(ProfileNode* node, ProfileNode* rootNode, int treeDepth);

		//! debug output flat helper function: builds a flat hierarchy of all nodes
		void buildFlatHierarchy(ProfileNode* outputNode, ProfileNode* inputNode);

		int getNumSiblings(ProfileNode* node);
		ProfileNode* getSibling(ProfileNode* node, int index);
	};
	/** @} End of "addtogroup Debug"*/
	//---------------------------------------------------------------
	void ProfileManager::incrementFrameCounter()
	{
		this->frameCounter++;
	}
	//---------------------------------------------------------------
	inline std::size_t ProfileManager::getFrameCountSinceReset() 
	{ 
		return this->frameCounter; 
	}
	//---------------------------------------------------------------
}

#endif //__ProfileManager_h__16_8_2010__12_02_16__
