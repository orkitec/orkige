/**************************************************************
	created:	2010/09/08 at 20:41
	filename: 	OrkigePrerequisites.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __OrkigePrerequisites_h__8_9_2010__20_41_21__
#define __OrkigePrerequisites_h__8_9_2010__20_41_21__


namespace Orkige 
{
	typedef unsigned int uint; //!< unsigned int shortcut

#ifdef __WIN32__
#	ifndef NOPRAGMAS
#		pragma once
#		pragma warning(disable:4275)
#		pragma warning(disable:4311)//reinterpret cast
		// disable: "no suitable definition provided for explicit template
		// instantiation request" Occurs in VC7 for no justifiable reason on all
		// #includes of Singleton
#		pragma warning( disable: 4661)

		// disable: "<type> needs to have dll-interface to be used by clients'
		// Happens on STL member variables which are not public therefore is ok
#		pragma warning (disable : 4251)
#	endif
	// Export control
#	ifdef ORKIGE_EXPORTS
#		define ORKIGE_DLL __declspec( dllexport )
#	else
#		define ORKIGE_DLL //__declspec( dllimport )
#	endif
#else // Linux / Mac OSX etc
#	define ORKIGE_DLL
#endif

}

#endif //__OrkigePrerequisites_h__8_9_2010__20_41_21__
