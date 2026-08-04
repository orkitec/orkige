/**************************************************************
	created:	2026/08/03 at 10:00
	filename: 	MonetizationProvider.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_monetization/MonetizationProvider.h"

namespace Orkige
{
	//---------------------------------------------------------
	// the interfaces are pure; these anchor their vtables in one translation
	// unit instead of every consumer's
	StoreProvider::~StoreProvider()
	{
	}
	//---------------------------------------------------------
	AdProvider::~AdProvider()
	{
	}
}
