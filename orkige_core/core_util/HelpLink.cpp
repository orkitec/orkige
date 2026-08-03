/**************************************************************
	created:	2026/08/03 at 14:00
	filename: 	HelpLink.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_util/HelpLink.h"

namespace Orkige
{
	String helpUrl(String const & page)
	{
		// the generator renders one .html per doc directly under the portal
		// root, so the stem IS the page
		return String(HELP_PORTAL_URL) + page + ".html";
	}
}
