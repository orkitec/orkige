/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	AdPlacement.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __AdPlacement_h__3_8_2026__10_00_00__
#define __AdPlacement_h__3_8_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_monetization/MonetizationTypes.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Monetization
	*  @{ */

	//! @brief ONE ad unit's lifecycle as pure logic: load -> ready -> show ->
	//! result, with every refusal an explicit state rather than undefined
	//! behaviour.
	//!
	//! THE CONTRACT, in one place:
	//!  - beginLoad() is REFUSED while a load is in flight (a second request
	//!    would strand the first one's callback) and while inventory is already
	//!    held or on screen (re-loading throws away an advert already paid for).
	//!    A previous failure is retryable - that is what AS_FAILED is for.
	//!  - completeLoad() settles it: ALR_LOADED gives AS_READY, and EVERY other
	//!    verdict gives AS_FAILED with its reason kept. NO FILL lands here like
	//!    any other non-load, because a game must treat "there was no advert"
	//!    as an ordinary answer.
	//!  - beginShow() is REFUSED unless the unit is AS_READY. SHOW BEFORE READY
	//!    IS AN ERROR STATE: the caller reports ASR_NOT_READY and the unit does
	//!    not move, so a game can never present a unit that holds nothing.
	//!  - completeShow() consumes a FULLSCREEN unit (back to AS_IDLE - it must
	//!    be loaded again before the next show), while a BANNER stays in
	//!    AS_SHOWING until hide() takes it down. The two formats genuinely
	//!    behave differently and collapsing them loses the difference.
	//!
	//! @remarks Renderer-, platform- and provider-free on purpose, so the whole
	//! lifecycle including its error transitions unit-tests headlessly
	//! (tests/core/AdPlacementTests.cpp). MonetizationService owns one of these
	//! per (format, placement) pair and drives it from provider events.
	class ORKIGE_CORE_DLL AdPlacement
	{
		//--- Variables ---------------------------------------
	private:
		AdFormat		mFormat;	//!< which shape this unit is
		String			mPlacement;	//!< the game's own name for the slot
		AdState			mState;		//!< where the unit stands
		AdLoadResult	mLastLoad;	//!< the last load's verdict
		String			mReason;	//!< the last non-success reason ("" = none)
		//--- Methods -----------------------------------------
	public:
		//! a default banner unit (needed by the container holding them)
		AdPlacement();
		//! a unit of @p format for the slot @p placement ("" = the default slot)
		AdPlacement(AdFormat format, String const & placement);

		//! which shape this unit is
		AdFormat format() const { return this->mFormat; }
		//! the game's own name for the slot
		String const & placement() const { return this->mPlacement; }
		//! where the unit stands
		AdState state() const { return this->mState; }
		//! the last load's verdict
		AdLoadResult lastLoad() const { return this->mLastLoad; }
		//! the last non-success reason ("" when there was none)
		String const & reason() const { return this->mReason; }

		//! is inventory held and showable
		bool isReady() const { return this->mState == AS_READY; }
		//! is the unit on screen
		bool isShowing() const { return this->mState == AS_SHOWING; }
		//! is a load in flight
		bool isLoading() const { return this->mState == AS_LOADING; }
		//! is this a fullscreen takeover rather than a banner
		bool isTakeover() const { return adFormatIsTakeover(this->mFormat); }

		//! @brief begin a load.
		//! @param outRefusal filled with the verdict to report when the
		//! transition is refused (ALR_BUSY), untouched otherwise
		//! @return false when the transition is refused
		bool beginLoad(AdLoadResult & outRefusal, String & outReason);
		//! settle a load in flight (@see the class contract)
		void completeLoad(AdLoadResult result, String const & reason = String());

		//! @brief begin a show.
		//! @return false when the unit is not AS_READY - the caller reports
		//! ASR_NOT_READY and nothing moves
		bool beginShow();
		//! @brief settle a show: a fullscreen unit is CONSUMED back to AS_IDLE,
		//! a banner stays in AS_SHOWING until hide()
		void completeShow(AdShowResult result, String const & reason = String());

		//! @brief take a banner down. A no-op on a fullscreen unit (which is
		//! consumed by its own result) and on a banner that is not on screen.
		void hide();

		//! back to the never-loaded state (provider teardown, consent revoked)
		void reset();
	};

	/** @} */
}

#endif //__AdPlacement_h__3_8_2026__10_00_00__
