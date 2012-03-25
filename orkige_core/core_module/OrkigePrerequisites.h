/**************************************************************
	created:	2010/09/08 at 20:41
	filename: 	OrkigePrerequisites.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __OrkigePrerequisites_h__8_9_2010__20_41_21__
#define __OrkigePrerequisites_h__8_9_2010__20_41_21__


namespace Orkige 
{
	typedef unsigned int uint; //!< unsigned int shortcut

#ifdef WIN32
#	if defined( ORKIGE_STATIC )
#   	define ORKIGE_CORE_DLL
#   else
#      if defined( __MINGW32__ )
#			define ORKIGE_CORE_DLL
#		else
#			pragma warning( disable : 4251)
#			ifdef orkige_core_EXPORTS
#				define ORKIGE_CORE_DLL __declspec( dllexport )
#			else
#				define ORKIGE_CORE_DLL __declspec( dllimport )
#			endif
#		endif
#	endif
#else // Linux / Mac OSX etc
#	define ORKIGE_CORE_DLL
#endif


}
void ORKIGE_CORE_DLL init_module_orkige_core(void);

#endif //__OrkigePrerequisites_h__8_9_2010__20_41_21__
