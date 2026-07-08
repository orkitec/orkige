/**************************************************************
	created:	2010/08/16 at 23:05
	filename: 	StreamOperators.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __StreamOperators_h__16_8_2010__23_05_35__
#define __StreamOperators_h__16_8_2010__23_05_35__

#include <list>
#include <set>
#include <deque>
#include <map>
#include <vector>

namespace Orkige
{
	//! overridden stream operators for all kinds of types including most of stl collections
	template<typename StreamType>
	class StreamOperators
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
		//! write std::map to Stream
		template<typename KeyType, typename ValueType>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, std::map<KeyType, ValueType> const & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::map<KeyType, ValueType>::const_iterator  TempMapIterator;
			TempMapIterator it = t.begin();
			TempMapIterator itend = t.end();

			for(; it != itend; ++it)
			{
				ar << it->first << it->second;
			}
			return ar;
		}
		//! write std::map to Stream
		template<typename KeyType, typename ValueType>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, std::map<KeyType, ValueType> & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::map<KeyType, ValueType>::iterator  TempMapIterator;
			TempMapIterator it = t.begin();
			TempMapIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << it->first << it->second;
			}
			return ar;
		}
		//! read std::map from Stream
		template<typename KeyType, typename ValueType>
		friend optr<StreamType> const & operator>>(optr<StreamType> const & ar, std::map<KeyType, ValueType> & t )
		{
			unsigned int size;
			ar >> size;
			for(unsigned int i=0; i < size; i++)
			{
				KeyType first;
				ValueType second;
				ar >> first >> second;
				t[first] = second;
			}
			return ar;
		}
		//! write std::pair to Stream
		template<typename KeyType, typename ValueType>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, std::pair<KeyType, ValueType> const & t )
		{
			return ar << t.first << t.second;
		}
		//! write std::pair to Stream
		template<typename KeyType, typename ValueType>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, std::pair<KeyType, ValueType> & t )
		{
			return ar << t.first << t.second;
		}
		//! read std::pair from Stream
		template<typename KeyType, typename ValueType>
		friend optr<StreamType> const & operator>>(optr<StreamType> const & ar, std::pair<KeyType, ValueType> & t )
		{
			return ar >> t.first >> t.second;
		}
		//! write std::vector to Stream
		template<typename ValueType>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, std::vector<ValueType> const & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::vector<ValueType>::const_iterator  TempVectorIterator;
			TempVectorIterator it = t.begin();
			TempVectorIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! write std::vector to Stream
		template<typename ValueType>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, std::vector<ValueType> & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::vector<ValueType>::iterator  TempVectorIterator;
			TempVectorIterator it = t.begin();
			TempVectorIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! read std::vector from Stream
		template<typename ValueType>
		friend optr<StreamType> const & operator>>(optr<StreamType> const & ar, std::vector<ValueType> & t )
		{
			unsigned int size;
			ar >> size;
			for(unsigned int i=0; i < size; i++)
			{
				ValueType value;
				ar >> value;
				t.push_back(value);
			}
			return ar;
		}
		//! write std::list to Stream
		template<typename ValueType>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, std::list<ValueType> const & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::list<ValueType>::const_iterator  TempListIterator;
			TempListIterator it = t.begin();
			TempListIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! write std::list to Stream
		template<typename ValueType>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, std::list<ValueType> & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::list<ValueType>::iterator  TempListIterator;
			TempListIterator it = t.begin();
			TempListIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! read std::list from Stream
		template<typename ValueType>
		friend optr<StreamType> const & operator>>(optr<StreamType> const & ar, std::list<ValueType> & t )
		{
			unsigned int size;
			ar >> size;
			for(unsigned int i=0; i < size; i++)
			{
				ValueType value;
				ar >> value;
				t.push_back(value);
			}
			return ar;
		}
		//! write std::deque to Stream
		template<typename ValueType>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, std::deque<ValueType> const & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::list<ValueType>::const_iterator  TempDequeIterator;
			TempDequeIterator it = t.begin();
			TempDequeIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! write std::deque to Stream
		template<typename ValueType>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, std::deque<ValueType> & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::list<ValueType>::iterator  TempDequeIterator;
			TempDequeIterator it = t.begin();
			TempDequeIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! read std::deque from Stream
		template<typename ValueType>
		friend optr<StreamType> const & operator>>(optr<StreamType> const & ar, std::deque<ValueType> & t )
		{
			unsigned int size;
			ar >> size;
			for(unsigned int i=0; i < size; i++)
			{
				ValueType value;
				ar >> value;
				t.push_back(value);
			}
			return ar;
		}	
		//! write value to Stream
		template<typename Type>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, Type const & t)
		{
			ar->write(t);
			return ar;
		}
		//! write value to Stream
		template<typename Type>
		friend optr<StreamType> const & operator<<(optr<StreamType> const & ar, Type & t)
		{
			ar->write(t);
			return ar;
		}
		//! read value from Stream
		template<typename Type>
		friend optr<StreamType> const & operator>>(optr<StreamType> const & ar, Type & t)
		{
			ar->read(t);
			return ar;
		}
		//---------------------------------------------------------------------------------
		//! write std::map to Stream
		template<typename KeyType, typename ValueType>
		friend StreamType & operator<<(StreamType & ar, std::map<KeyType, ValueType> const & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::map<KeyType, ValueType>::const_iterator  TempMapIterator;
			TempMapIterator it = t.begin();
			TempMapIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << it->first << it->second;
			}
			return ar;
		}
		//! write std::map to Stream
		template<typename KeyType, typename ValueType>
		friend StreamType & operator<<(StreamType & ar, std::map<KeyType, ValueType> & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::map<KeyType, ValueType>::iterator  TempMapIterator;
			TempMapIterator it = t.begin();
			TempMapIterator itend = t.end();

			for(; it != itend; ++it)
			{
				ar << it->first << it->second;
			}
			return ar;
		}
		//! read std::map from Stream
		template<typename KeyType, typename ValueType>
		friend StreamType & operator>>(StreamType & ar, std::map<KeyType, ValueType> & t )
		{
			unsigned int size;
			ar >> size;
			for(unsigned int i=0; i < size; i++)
			{
				KeyType first;
				ValueType second;
				ar >> first >> second;
				t[first] = second;
			}
			return ar;
		}
		//! write std::pair to Stream
		template<typename KeyType, typename ValueType>
		friend StreamType & operator<<(StreamType & ar, std::pair<KeyType, ValueType> const & t )
		{
			return ar << t.first << t.second;
		}
		//! write std::pair to Stream
		template<typename KeyType, typename ValueType>
		friend StreamType & operator<<(StreamType & ar, std::pair<KeyType, ValueType> & t )
		{
			return ar << t.first << t.second;
		}
		//! read std::pair from Stream
		template<typename KeyType, typename ValueType>
		friend StreamType & operator>>(StreamType & ar, std::pair<KeyType, ValueType> & t )
		{
			return ar >> t.first >> t.second;
		}
		//! write std::vector to Stream
		template<typename ValueType>
		friend StreamType & operator<<(StreamType & ar, std::vector<ValueType> const & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::vector< ValueType > ::const_iterator  TempVectorIterator;
			TempVectorIterator it = t.begin();
			TempVectorIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! write std::vector to Stream
		template<typename ValueType>
		friend StreamType & operator<<(StreamType & ar, std::vector<ValueType> & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::vector< ValueType > ::iterator  TempVectorIterator;
			TempVectorIterator it = t.begin();
			TempVectorIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! read std::vector from Stream
		template<typename ValueType>
		friend StreamType & operator>>(StreamType & ar, std::vector<ValueType> & t )
		{
			unsigned int size;
			ar >> size;
			for(unsigned int i=0; i < size; i++)
			{
				ValueType value;
				ar >> value;
				t.push_back(value);
			}
			return ar;
		}
		//! write std::list to Stream
		template<typename ValueType>
		friend StreamType & operator<<(StreamType & ar, std::list<ValueType> const & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::list< ValueType > ::const_iterator  TempListIterator;
			TempListIterator it = t.begin();
			TempListIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! write std::list to Stream
		template<typename ValueType>
		friend StreamType & operator<<(StreamType & ar, std::list<ValueType> & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::list< ValueType > ::iterator  TempListIterator;
			TempListIterator it = t.begin();
			TempListIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! read std::list from Stream
		template<typename ValueType>
		friend StreamType & operator>>(StreamType & ar, std::list<ValueType> & t )
		{
			unsigned int size;
			ar >> size;
			for(unsigned int i=0; i < size; i++)
			{
				ValueType value;
				ar >> value;
				t.push_back(value);
			}
			return ar;
		}
		//! write std::deque to Stream
		template<typename ValueType>
		friend StreamType & operator<<(StreamType & ar, std::deque<ValueType> const & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::list<ValueType>::const_iterator  TempDequeIterator;
			TempDequeIterator it = t.begin();
			TempDequeIterator itend = t.end();
			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! write std::deque to Stream
		template<typename ValueType>
		friend StreamType & operator<<(StreamType & ar, std::deque<ValueType> & t )
		{
			ar << static_cast<unsigned int>(t.size());
			typedef typename std::list<ValueType>::iterator  TempDequeIterator;
			TempDequeIterator it = t.begin();
			TempDequeIterator itend = t.end();

			for(; it != itend; ++it)
			{
				ar << *it;
			}
			return ar;
		}
		//! read std::deque from Stream
		template<typename ValueType>
		friend StreamType & operator>>(StreamType & ar, std::deque<ValueType> & t )
		{
			unsigned int size;
			ar >> size;
			for(unsigned int i=0; i < size; i++)
			{
				ValueType value;
				ar >> value;
				t.push_back(value);
			}
			return ar;
		}	
		//! write value to Stream
		template<typename Type>
		friend StreamType & operator<<(StreamType & ar, Type const & t)
		{
			ar.write(t);
			return ar;
		}
		//! write value to Stream
		template<typename Type>
		friend StreamType & operator<<(StreamType & ar, Type & t)
		{
			ar.write(t);
			return ar;
		}
		//! read value from Stream
		template<typename Type>
		friend StreamType & operator>>(StreamType & ar, Type & t)
		{
			ar.read(t);
			return ar;
		}
	protected:
	private:
	};
	//---------------------------------------------------------
}

#endif //__StreamOperators_h__16_8_2010__23_05_35__
