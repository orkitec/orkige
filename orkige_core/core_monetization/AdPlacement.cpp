/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	AdPlacement.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_monetization/AdPlacement.h"

namespace Orkige
{
	//---------------------------------------------------------
	AdPlacement::AdPlacement()
		: mFormat(AF_BANNER)
		, mState(AS_IDLE)
		, mLastLoad(ALR_ERROR)
	{
	}
	//---------------------------------------------------------
	AdPlacement::AdPlacement(AdFormat format, String const & placement)
		: mFormat(format)
		, mPlacement(placement)
		, mState(AS_IDLE)
		, mLastLoad(ALR_ERROR)
	{
	}
	//---------------------------------------------------------
	bool AdPlacement::beginLoad(AdLoadResult & outRefusal, String & outReason)
	{
		if(this->mState == AS_LOADING)
		{
			// a second request would strand the first one's callback
			outRefusal = ALR_BUSY;
			outReason = "a load for this placement is already in flight";
			return false;
		}
		if(this->mState == AS_READY)
		{
			// inventory already held: re-loading discards an advert the
			// network has already committed to us
			outRefusal = ALR_BUSY;
			outReason = "this placement already holds inventory";
			return false;
		}
		if(this->mState == AS_SHOWING)
		{
			outRefusal = ALR_BUSY;
			outReason = "this placement is on screen";
			return false;
		}

		// AS_IDLE and AS_FAILED both load: a failure - no fill above all - is
		// meant to be retried
		this->mState = AS_LOADING;
		this->mReason.clear();
		return true;
	}
	//---------------------------------------------------------
	void AdPlacement::completeLoad(AdLoadResult result, String const & reason)
	{
		if(this->mState != AS_LOADING)
		{
			// a late or duplicate answer for a load nobody is waiting on
			return;
		}
		this->mLastLoad = result;
		this->mReason = reason;
		this->mState = (result == ALR_LOADED) ? AS_READY : AS_FAILED;
	}
	//---------------------------------------------------------
	bool AdPlacement::beginShow()
	{
		// SHOW BEFORE READY IS AN ERROR, never undefined: the unit does not
		// move and the caller reports ASR_NOT_READY
		if(this->mState != AS_READY) { return false; }
		this->mState = AS_SHOWING;
		this->mReason.clear();
		return true;
	}
	//---------------------------------------------------------
	void AdPlacement::completeShow(AdShowResult result, String const & reason)
	{
		if(this->mState != AS_SHOWING)
		{
			// a late or duplicate answer for a show nobody is waiting on
			return;
		}
		this->mReason = reason;

		if(this->isTakeover())
		{
			// a fullscreen unit is CONSUMED by being watched: the inventory is
			// spent and the next show needs a new load
			this->mState = AS_IDLE;
			this->mLastLoad = ALR_ERROR;
			return;
		}

		// a banner that failed to present is not on screen; one that presented
		// stays up until hide()
		this->mState = (result == ASR_ERROR) ? AS_FAILED : AS_SHOWING;
	}
	//---------------------------------------------------------
	void AdPlacement::hide()
	{
		if(this->mState != AS_SHOWING) { return; }
		if(this->isTakeover())
		{
			// a fullscreen unit is dismissed by the player, not hidden by the
			// game - its own result takes it down
			return;
		}
		this->mState = AS_IDLE;
		this->mLastLoad = ALR_ERROR;
		this->mReason.clear();
	}
	//---------------------------------------------------------
	void AdPlacement::reset()
	{
		this->mState = AS_IDLE;
		this->mLastLoad = ALR_ERROR;
		this->mReason.clear();
	}
}
