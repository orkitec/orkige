/**************************************************************
	created:	2010/08/31 at 0:31
	filename: 	StringConverter.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __StringConverter_h__31_8_2010__0_31_04__
#define __StringConverter_h__31_8_2010__0_31_04__

#include "engine_module/EnginePrerequisites.h"
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
			//! parse Ogre::Vector4 from a String
			static Ogre::Vector4 parseVector4(const String& val)
			{
				// Split on space
				Ogre::StringVector vec = Ogre::StringUtil::split(val);

				if (vec.size() != 4)
				{
					return Ogre::Vector4::ZERO;
				}
				else
				{
					return Ogre::Vector4(parseReal(vec[0]),parseReal(vec[1]),parseReal(vec[2]),parseReal(vec[3]));
				}
			}

			//! convert String to Ogre::Real
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::Real>::value>::type * = 0)
			{
				return parseReal(val);
			}
#if OGRE_DOUBLE_PRECISION == 0
			//! convert String to Ogre::Real
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, double>::value>::type * = 0)
			{
				// Use istringstream for direct correspondence with toString
				Ogre::StringStream str(val);
				double ret = 0;
				str >> ret;

				return ret;
			}
#endif
			//! convert String to Ogre::Radian
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::Radian>::value>::type * = 0)
			{
				return parseAngle(val);
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
			//! convert String to long
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, long>::value>::type * = 0)
			{
				// OGRE 14 deprecated parseLong
				return std::strtol(val.c_str(), 0, 10);
			}
			//! convert String to unsigned long
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, unsigned long>::value>::type * = 0)
			{
				// OGRE 14 deprecated parseUnsignedLong
				return std::strtoul(val.c_str(), 0, 10);
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
			//! convert String to Ogre::Vector3
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::Vector3>::value>::type * = 0)
			{
				return parseVector3(val);
			}
			//! convert String to Ogre::Vector4
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::Vector4>::value>::type * = 0)
			{
				return parseVector4(val);
			}
			//! convert String to Ogre::Matrix3
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::Matrix3>::value>::type * = 0)
			{
				return parseMatrix3(val);
			}
			//! convert String to Ogre::Matrix4
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::Matrix4>::value>::type * = 0)
			{
				return parseMatrix4(val);
			}
			//! convert String to Ogre::Quaternion
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::Quaternion>::value>::type * = 0)
			{
				return parseQuaternion(val);
			}
			//! convert String to Ogre::ColourValue
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::ColourValue>::value>::type * = 0)
			{
				return parseColourValue(val);
			}
			//! convert String to Ogre::StringVector
			template<class Type>
			static Type fromString(const String& val,
				typename std::enable_if<std::is_same<Type, Ogre::StringVector>::value>::type * = 0)
			{
				// OGRE 14 deprecated parseStringVector in favour of split
				return Ogre::StringUtil::split(val);
			}
		protected:
		private:
		};
	}
	//---------------------------------------------------------
}

#endif //__StringConverter_h__31_8_2010__0_31_04__
