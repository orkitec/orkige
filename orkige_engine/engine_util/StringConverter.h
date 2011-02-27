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
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_same.hpp>

namespace Orkige
{
	namespace StringUtil
	{
		//! @brief converter for several types into String's and vice versa
		//! @see Ogre::StringConverter
		class Converter : public Ogre::StringConverter
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
				Ogre::vector<String>::type vec = Ogre::StringUtil::split(val);

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
				Ogre::vector<String>::type vec = Ogre::StringUtil::split(val);

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
				typename boost::enable_if<boost::is_same<Type, Ogre::Real>  >::type * = 0)
			{
				return parseReal(val);
			}
			//! convert String to Ogre::Radian
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, Ogre::Radian>  >::type * = 0)
			{
				return parseAngle(val);
			}
			//! convert String to int
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, int>  >::type * = 0)
			{
				return parseInt(val);
			}
			//! convert String to unsigned int
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, unsigned int>  >::type * = 0)
			{
				return parseUnsignedInt(val);
			}
			//! convert String to long
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, long>  >::type * = 0)
			{
				return parseLong(val);
			}
			//! convert String to unsigned long
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, unsigned long>  >::type * = 0)
			{
				return parseUnsignedLong(val);
			}
			//! convert String to bool
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, bool>  >::type * = 0)
			{
				return parseBool(val);
			}
			//! convert String to Ogre::Vector2
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, Ogre::Vector2>  >::type * = 0)
			{
				return parseVector2(val);
			}
			//! convert String to Ogre::Vector3
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, Ogre::Vector3>  >::type * = 0)
			{
				return parseVector3(val);
			}
			//! convert String to Ogre::Vector4
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, Ogre::Vector4>  >::type * = 0)
			{
				return parseVector4(val);
			}
			//! convert String to Ogre::Matrix3
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, Ogre::Matrix3>  >::type * = 0)
			{
				return parseMatrix3(val);
			}
			//! convert String to Ogre::Matrix4
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, Ogre::Matrix4>  >::type * = 0)
			{
				return parseMatrix4(val);
			}
			//! convert String to Ogre::Quaternion
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, Ogre::Quaternion>  >::type * = 0)
			{
				return parseQuaternion(val);
			}
			//! convert String to Ogre::ColourValue
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, Ogre::ColourValue>  >::type * = 0)
			{
				return parseColourValue(val);
			}
			//! convert String to Ogre::StringVector
			template<class Type>
			static Type fromString(const String& val,
				typename boost::enable_if<boost::is_same<Type, Ogre::StringVector>  >::type * = 0)
			{
				return parseStringVector(val);
			}
		protected:
		private:
		};
	}
	//---------------------------------------------------------
}

#endif //__StringConverter_h__31_8_2010__0_31_04__
