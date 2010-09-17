///////////////////////////////////////////////////////////////////////////////
// 
// ObjectFactory
// 
// The ObjectFactory class is a object factory implementation.  It allows users
// to register and unregister classes during run-time by specifying a
// user-defined unique identifier per class.  Instances of any registered class
// can then be instantiated simply by calling the create method and passing the
// proper unique identifier.
// 
// further explanation: http://www.gamedev.net/reference/articles/article2097.asp
///////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2004 Robert Geiman.
//
// Permission to copy, modify, and use this code for personal and commercial
// software is granted provided this copyright notice appears in all copies.
// This software is provided "as is" without any expressed or implied warranty.
//
// Any comments or questions can be sent to: rgeiman@buckeye-express.com
//
///////////////////////////////////////////////////////////////////////////////

#ifndef OBJECT_FACTORY_H
#define OBJECT_FACTORY_H


#include <map>
#include "core_util/MacroRepeat.h"


template <typename CtorSignature, typename UniqueIdType> class ObjectFactory;

#define OBJECT_FACTORY(num)																																\
                                                                                                  														\
                                                                                                            											\
   template<typename BaseClassType MACRO_LIST_APPEND(num, MACRO_TEMPLATE_PARAMETER), typename UniqueIdType> 											\
   class ObjectFactory<BaseClassType (MACRO_LIST(num, MACRO_TEMPLATE_ARGUMENT)), UniqueIdType>            												\
   {                                                                                                        											\
		template<typename InternalBaseClassType MACRO_LIST_APPEND(num, MACRO_TEMPLATE_PARAMETER_B), typename ClassType>    									\
		static InternalBaseClassType CreateObject(MACRO_LIST(num, MACRO_FUNCTION_PARAMETER_B))                                    							\
		{                                                                                                        											\
			return new ClassType MACRO_BEGIN_PAREN(num, MACRO_EMPTY_MACRO) MACRO_LIST(num, MACRO_FUNCTION_ARGUMENT_B) MACRO_END_PAREN(num, MACRO_EMPTY_MACRO);\
		}																																					\
   private:                                                                                                 											\
      typedef BaseClassType (*CreateObjectFunc)(MACRO_LIST(num, MACRO_FUNCTION_PARAMETER));                 											\
                                                                                                            											\
   public:                                                                                                  											\
	  typedef typename std::map<UniqueIdType, CreateObjectFunc> ObjectCreatorMap;                                            							\
	  typedef typename std::map<UniqueIdType, CreateObjectFunc>::value_type value_type;                                            						\
      typedef typename ObjectCreatorMap::const_iterator const_iterator;             																	\
      typedef typename ObjectCreatorMap::iterator iterator;                         																	\
                                                                                                            											\
                                                                                                            											\
      template<typename ClassType>                                                                          											\
      bool registerType(UniqueIdType const & unique_id)                                                             									\
      {                                                                                                     											\
         if (m_object_creator.find(unique_id) != m_object_creator.end())                                    											\
            return false;                                                                                   											\
                                                                                                            											\
         m_object_creator[unique_id] = &CreateObject<BaseClassType MACRO_LIST_APPEND(num, MACRO_TEMPLATE_ARGUMENT), ClassType>;							\
                                                                                                            											\
         return true;                                                                                       											\
      }                                                                                                     											\
                                                                                                            											\
      bool unRegister(UniqueIdType const & unique_id)                                                               									\
      {                                                                                                     											\
         return (m_object_creator.erase(unique_id) == 1);                                                   											\
      }                                                                                                     											\
                                                                                                            											\
      BaseClassType create(UniqueIdType const & unique_id MACRO_LIST_APPEND(num, MACRO_FUNCTION_PARAMETER))         									\
      {                                                                                                     											\
         iterator iter = m_object_creator.find(unique_id);                                                  											\
                                                                                                            											\
         if (iter == m_object_creator.end())                                                                											\
            return NULL;                                                                                    											\
                                                                                                            											\
         return ((*iter).second)(MACRO_LIST(num, MACRO_FUNCTION_ARGUMENT));                                 											\
      }                                                                                                     											\
																																						\
	  bool isRegistered(UniqueIdType const & unique_id)																									\
      {                                                                                                     											\
         return m_object_creator.find(unique_id) != m_object_creator.end();																				\
      }                                                                                                     											\
                                                                                                            											\
      const_iterator begin() const																														\
      {                                                                                                     											\
         return m_object_creator.begin();                                                                   											\
      }                                                                                                     											\
                                                                                                            											\
      iterator begin()																																	\
      {                                                                                                     											\
         return m_object_creator.begin();                                                                   											\
      }                                                                                                     											\
                                                                                                            											\
      const_iterator end() const																														\
      {                                                                                                     											\
         return m_object_creator.end();                                                                     											\
      }                                                                                                     											\
                                                                                                            											\
      iterator end()																																	\
      {                                                                                                     											\
         return m_object_creator.end();                                                                     											\
      }                                                                                                     											\
                                                                                                            											\
   protected:                                                                                               											\
      ObjectCreatorMap m_object_creator;                                            																	\
   };                                                                                                       
   
MACRO_REPEAT(16, OBJECT_FACTORY)
#undef OBJECT_FACTORY

#endif
