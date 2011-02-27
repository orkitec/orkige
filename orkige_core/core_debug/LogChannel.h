/********************************************************************
	created:	Tuesday 2010/08/10 at 16:44
	filename: 	LogChannel.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __LogChannel_h__10_8_2010__16_44_09__
#define __LogChannel_h__10_8_2010__16_44_09__

#include "core_base/Meta.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Debug
	*  @{ */
	//! LogManager channel
	class ORKIGE_DLL LogChannel
	{
		//--- Types -------------------------------------------------
	public:
	protected:
	private:
		//--- Variables ---------------------------------------------
	public:
	protected:
		String tag;		//!< name of the channel
		int level;		//!< priority level
		bool enabled;	//!< is this channel enabled?
	private:
		//--- Methods -----------------------------------------------
	public:
		//! cosntructor takes tag, level and enabled setting
		LogChannel(String const & tag, int level, bool enabled=false);
		//! get name of this channel
		inline String const & getTag();
		//! get level of this channel
		inline int getLevel();
		//! is this channel enabled?
		inline bool isEnabled();
	protected:
	private:
	};
	/** @} End of "addtogroup Debug"*/
	//---------------------------------------------------------------
	inline String const & LogChannel::getTag()
	{
		return this->tag;
	}
	//---------------------------------------------------------------
	inline int LogChannel::getLevel()
	{
		return this->level;
	}
	//---------------------------------------------------------------
	inline bool LogChannel::isEnabled()
	{
		return this->enabled;
	}
	//---------------------------------------------------------------
}

#endif //__LogChannel_h__10_8_2010__16_44_09__
