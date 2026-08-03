/**************************************************************
	created:	2026/08/03 at 18:00
	filename: 	PlaySimulatorPick.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PlaySimulatorPick_h__3_8_2026__18_00_00__
#define __PlaySimulatorPick_h__3_8_2026__18_00_00__

#include <string>
#include <vector>

namespace OrkigeEditor
{
	//! @brief one iOS simulator as the boot-path picker sees it: display
	//! name, udid and whether the device is currently Booted. Pure data -
	//! the caller converts from whatever device listing it holds.
	struct SimulatorPickCandidate
	{
		std::string name;
		std::string udid;
		bool booted = false;
	};

	//! @brief the picker's answer: which device a boot-from-shutdown Play
	//! should use, and whether it must be SHUT DOWN first before the boot
	//! flow can treat it as a shutdown device.
	struct SimulatorShutdownPick
	{
		std::string name;
		std::string udid;
		//! true when the picked device is the DESIGNATED warm device but is
		//! currently Booted - a prior failed run boots it and exits (or is
		//! killed) without the shutdown its passing path performs. The
		//! caller shuts it down and then boots it again: the flow exercised
		//! is identical, and a warm re-boot takes seconds where an
		//! arbitrary never-booted device's cold first boot can outlast the
		//! whole preparation budget on a loaded machine.
		bool shutdownFirst = false;
	};

	//! @brief pick the simulator for a boot-from-shutdown Play. Pure.
	//!
	//! `warmUdid` (may be "") names a PRE-WARMED device (one already booted
	//! and shut down once, so its next boot is warm). The warm device wins
	//! whenever it is in the list - EVEN when it is currently Booted, in
	//! which case the pick carries shutdownFirst so the caller restores the
	//! shutdown state and keeps the warm boot instead of falling back to a
	//! cold stranger. Without a warm match the first shutdown device is
	//! taken; a Booted non-warm device is never picked (shutting down a
	//! device this run does not own is not the picker's call). False when
	//! nothing qualifies (out untouched).
	bool pickShutdownSimulator(
		std::vector<SimulatorPickCandidate> const& devices,
		std::string const& warmUdid, SimulatorShutdownPick& out);

} // namespace OrkigeEditor

#endif // __PlaySimulatorPick_h__3_8_2026__18_00_00__
