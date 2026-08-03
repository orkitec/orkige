/**************************************************************
	created:	2026/08/03 at 18:00
	filename: 	PlaySimulatorPick.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "PlaySimulatorPick.h"

namespace OrkigeEditor
{
	//---------------------------------------------------------
	bool pickShutdownSimulator(
		std::vector<SimulatorPickCandidate> const& devices,
		std::string const& warmUdid, SimulatorShutdownPick& out)
	{
		// the designated warm device wins outright, whatever state a prior
		// run left it in (see the header for why a Booted leftover is still
		// the right pick)
		if (!warmUdid.empty())
		{
			for (SimulatorPickCandidate const& device : devices)
			{
				if (device.udid == warmUdid)
				{
					out.name = device.name;
					out.udid = device.udid;
					out.shutdownFirst = device.booted;
					return true;
				}
			}
		}
		// otherwise: the first shutdown device; Booted strangers are never
		// touched
		for (SimulatorPickCandidate const& device : devices)
		{
			if (!device.booted)
			{
				out.name = device.name;
				out.udid = device.udid;
				out.shutdownFirst = false;
				return true;
			}
		}
		return false;
	}

} // namespace OrkigeEditor
