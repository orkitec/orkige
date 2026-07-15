/**************************************************************
	created:	2010/08/31 at 0:31
	filename: 	StringConverter.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __StringConverter_h__31_8_2010__0_31_04__
#define __StringConverter_h__31_8_2010__0_31_04__

#include "engine_module/EnginePrerequisites.h"
// explicit (the neutral umbrella carries math only): the Ogre
// string helpers exist identically in classic OGRE 14 and Ogre-Next
#include <OgreString.h>
#include <OgreStringConverter.h>
#include <OgreStringVector.h>
#include <cstdlib>
#include <type_traits>

namespace Orkige
{
	namespace StringUtil
	{
		//! @brief converter for several types into String's and vice versa
		//! @see Ogre::StringConverter
		class ORKIGE_ENGINE_DLL Converter : public Ogre::StringConverter
		{
			//--- Types -------------------------------------------
		public:
		protected:
		private:
			//--- Variables ---------------------------------------
		public:
		protected:
		private:
			//--- Methods -----------------------------------------
		public:
			//! parse Ogre::Vector2 from a String
			static Ogre::Vector2 parseVector2(const String& val)
			{
				// Split on space
				Ogre::StringVector vec = Ogre::StringUtil::split(val);

				if (vec.size() != 2)
				{
					return Ogre::Vector2::ZERO;
				}
				else
				{
					return Ogre::Vector2(parseReal(vec[0]),parseReal(vec[1]));
				}

			}

			//! convert String to Ogre::Real
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::Real>::value>::type * = 0)
			{
				return parseReal(val);
			}
			//! convert String to int
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, int>::value>::type * = 0)
			{
				return parseInt(val);
			}
			//! convert String to unsigned int
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, unsigned int>::value>::type * = 0)
			{
				return parseUnsignedInt(val);
			}
			//! convert String to bool
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, bool>::value>::type * = 0)
			{
				return parseBool(val);
			}
			//! convert String to Ogre::Vector2
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::Vector2>::value>::type * = 0)
			{
				return parseVector2(val);
			}
		protected:
		private:
		};
	}
	//---------------------------------------------------------
}

#endif //__StringConverter_h__31_8_2010__0_31_04__
