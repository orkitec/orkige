/**************************************************************
	created:	2010/08/19 at 21:56
	filename: 	AttributeHolder.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __AttributeHolder_h__19_8_2010__21_56_34__
#define __AttributeHolder_h__19_8_2010__21_56_34__

#include "core_serialization/ISerializeable.h"

#include <type_traits>

namespace Orkige
{
	//! class that holds attributes
	template<typename OwnedAttributeTypeIdType, class OwnedAttributeType>
	class AttributeHolder : public ISerializeable
	{
		typedef AttributeHolder<OwnedAttributeTypeIdType, OwnedAttributeType> AttributeHolder__OwnedAttributeTypeIdType_OwnedAttributeType;
		OOBJECT(AttributeHolder__OwnedAttributeTypeIdType_OwnedAttributeType, ISerializeable)
		//--- Types -------------------------------------------
	public:
		//! Helper class for adding Attributes that aren't derived from OwnedAttributeType 	
		template<typename WrappedType>
		class AttributeWrapper : public OwnedAttributeType
		{
			OOBJECT_TEMPLATE(AttributeWrapper,WrappedType,OwnedAttributeType)
			// Attributes --------------------------------------------------------------
		public:
			WrappedType value;		//!< wrapped value
		protected:
		private:
			// Methods -----------------------------------------------------------------
		public:
			//! constructor
			inline AttributeWrapper(WrappedType const & val)		{	this->value = val;			}
			//! destructor
			virtual ~AttributeWrapper()								{								}

			//! get the wrapped value
			inline WrappedType const & getValue()							
			{	
				return this->value;	
			}

			//! save wrapped value to archive
			virtual void save(optr<IArchive> const & ar)	
			{
				OParent::save(ar);
				ar << this->value;	
			}
			//! load wrapped value from archive
			virtual void load(optr<IArchive> const & ar)	
			{
				OParent::load(ar);
				ar >> this->value;	
			}

			AttributeWrapper()										{								}
		protected:

		private:
		};

		typedef typename std::map<OwnedAttributeTypeIdType, optr<OwnedAttributeType> > OwnedAttributeMap;	//!< map of owned attributes
		typedef typename OwnedAttributeMap::iterator OwnedAttributeMapIterator;								//!< iterator for OwnedAttributeMap
		typedef typename OwnedAttributeMap::const_iterator OwnedAttributeMapConstIterator;					//!< const_iterator for OwnedAttributeMap
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		OwnedAttributeMap attributes;																		//!< owned Attributes
	private:
		//--- Methods -----------------------------------------
	public:
		//! constructor
		AttributeHolder()
		{};
		
		//! copy constructor
		AttributeHolder(AttributeHolder const & ah) 
			: attributes(ah.attributes) 
		{};

		//! destructor
		virtual ~AttributeHolder() 
		{
			this->clearAttributes();	
		};

		//! @brief	set an attribute 
		//!			if it already exists it get replaced else it gets newly added 
		//!			
		//! @param	id the Attribute Key
		//! @param	attribute the Attribute to set as smart_ptr
		inline void setAttribute(OwnedAttributeTypeIdType const & id,optr<OwnedAttributeType> const & attribute)
		{
			this->attributes[id] = attribute;
			this->onAttributeSet(id, attribute);
		}

		//! @brief	set an attribute that isn't derived from OwnedAttributeType
		//!			type gets internally wrapped
		template<class Type> 
		inline void setAttribute(OwnedAttributeTypeIdType const & id, Type const & attribute,
			typename std::enable_if<!is_optr< Type>::value>::type * = 0,
			typename std::enable_if<!std::is_base_of< OwnedAttributeType, Type>::value>::type * = 0,
			typename std::enable_if<!std::is_same< OwnedAttributeType, Type>::value>::type * = 0)
		{
			optr<AttributeWrapper<Type> > wrappedAttribute = onew(new AttributeWrapper<Type>(attribute));
			oAssert(wrappedAttribute);
			this->setAttribute(id, wrappedAttribute);
		}

		//! @brief	retrieve a named Attribute by its id
		//! @note	if the Attribute doesn't exist this asserts in _DEBUG mode
		inline optr<OwnedAttributeType> getAttribute(OwnedAttributeTypeIdType const & id)
		{
			OwnedAttributeMapIterator findIt = this->attributes.find(id);
			oAssert(findIt != attributes.end());
			return findIt->second;
		}

		//! @brief	retrieve a named Attribute by its id with the specified type
		//! @note	if the Attribute doesn't exist or isn't convertible to the specified ReturnType this asserts in _DEBUG mode
		template<typename ReturnAttributeType> 
		inline optr<ReturnAttributeType> getAttribute(OwnedAttributeTypeIdType const & id,
			typename std::enable_if<!is_optr< ReturnAttributeType>::value>::type * = 0,
			typename std::enable_if<std::is_base_of< OwnedAttributeType, ReturnAttributeType>::value>::type * = 0)
		{
			optr<ReturnAttributeType> attribute = std::dynamic_pointer_cast<ReturnAttributeType>(this->getAttribute(id));
			oAssert(attribute);
			return attribute;
		}

		//! @brief	retrieve a wrapped attribute
		//! @note	if the Attribute doesn't exist or isn't convertible to the specified ReturnType this asserts in _DEBUG mode
		template<typename ReturnAttributeType> 
		inline ReturnAttributeType getAttribute(OwnedAttributeTypeIdType const & id,
			typename std::enable_if<!is_optr< ReturnAttributeType>::value>::type * = 0,
			typename std::enable_if<!std::is_base_of< AttributeHolder< OwnedAttributeTypeIdType, OwnedAttributeType>, ReturnAttributeType>::value>::type * = 0)
		{
			optr<AttributeWrapper<ReturnAttributeType> > attribute = this->getAttribute<AttributeWrapper<ReturnAttributeType> >(id);
			oAssert(attribute);
			return attribute->getValue();
		}

		//! @brief	delete the Attribute with specified id
		//! @retval	true if Object was successfully deleted
		//! @retval	false on error (Attribute with given id doesn't exist)
		bool delAttribute(OwnedAttributeTypeIdType const & id)
		{
			OwnedAttributeMapIterator findIt = this->attributes.find(id);
			if(findIt == this->attributes.end())
			{
				return false;
			}

			this->attributes.erase(findIt);
			return true;
		}

		//! @brief	 get the map with all attributes
		inline OwnedAttributeMap const & getAttributes() const
		{
			return this->attributes;
		}

		//! @brief	check if Attribute with given id exists
		inline bool hasAttribute(OwnedAttributeTypeIdType const & id)
		{
			bool attributeExists = this->attributes.find(id) != this->attributes.end();
			return attributeExists;
		}

		//! @brief	clear all Attributes
		inline void clearAttributes()
		{
			this->attributes.clear();
		}

		//! @brief overrideable that gets called after a attribute is set
		virtual void onAttributeSet(OwnedAttributeTypeIdType const & id, optr<OwnedAttributeType> const & attribute) {}

		//--- SERIALIZATION ---
		
		virtual void save(optr<IArchive> const & ar)
		{
			ar << this->attributes;
		}
		virtual void load(optr<IArchive> const & ar)
		{
			ar >> this->attributes;
		}
	protected:
	private:
	};
	//---------------------------------------------------------
#ifdef ORKIGE_NOSCRIPT
#define IMPLEMENT_WRAPPER_ATTRIBUTEHOLDER(OwnedAttributeTypeIdType, OwnedAttributeType, WrappedType)																																				\
	OSTRANGEGCC_TEMPLATE_TYPE_INFO_IMPL(AttributeHolder<OwnedAttributeTypeIdType OMACRO_COMMA_SEPERATOR OwnedAttributeType>::AttributeWrapper<WrappedType>,WrappedType##OwnedAttributeType##AttributeWrapper)															\
	template <> template <> void AttributeHolder<OwnedAttributeTypeIdType, OwnedAttributeType>::AttributeWrapper<WrappedType>::OrkigeMetaExport(const char * currentOrkigeModuleName) {																						\
	OOBJECT_END

#define IMPLEMENT_ATTRIBUTEHOLDER(OwnedAttributeTypeIdType, OwnedAttributeType)				\
	OOBJECT_TEMPLATE_IMPL2(AttributeHolder, OwnedAttributeTypeIdType, OwnedAttributeType)	\
	OOBJECT_END
#elif ORKIGE_LUA
#define IMPLEMENT_WRAPPER_ATTRIBUTEHOLDER(OwnedAttributeTypeIdType, OwnedAttributeType, WrappedType)																																				\
	OSTRANGEGCC_TEMPLATE_TYPE_INFO_IMPL(AttributeHolder<OwnedAttributeTypeIdType OMACRO_COMMA_SEPERATOR OwnedAttributeType>::AttributeWrapper<WrappedType>,WrappedType##OwnedAttributeType##AttributeWrapper)										\
	template <> template <> void AttributeHolder<OwnedAttributeTypeIdType, OwnedAttributeType>::AttributeWrapper<WrappedType>::OrkigeMetaExport(const char * currentOrkigeModuleName) {																\
	ORKIGE_LUA_USERTYPE_BASED((AttributeHolder<OwnedAttributeTypeIdType, OwnedAttributeType>::AttributeWrapper<WrappedType>::getClassTypeInfo().getName()), OwnedAttributeType, AttributeHolder<OwnedAttributeTypeIdType, OwnedAttributeType>::AttributeWrapper<WrappedType>)	\
	OCONSTRUCTOR1(WrappedType)																																																						\
	OVAR(value)																																																										\
	OFUNCCR(getValue)																																																								\
	OOBJECT_END

#define IMPLEMENT_ATTRIBUTEHOLDER(OwnedAttributeTypeIdType, OwnedAttributeType)				\
	OOBJECT_TEMPLATE_IMPL2(AttributeHolder, OwnedAttributeTypeIdType, OwnedAttributeType)	\
	/*OFUNC(getAttribute) and OFUNC(setAttribute) are template overload sets - not exposable generically*/	\
	/*OVAR(attributes) disabled: binding the OwnedAttributeMap trips a compile bug in vcpkg's sol2 3.3.0 associative container support*/	\
	OFUNC(hasAttribute)																	\
	OFUNC(clearAttributes)																\
	OFUNC(delAttribute)																	\
	OOBJECT_END
#else
#define IMPLEMENT_WRAPPER_ATTRIBUTEHOLDER(OwnedAttributeTypeIdType, OwnedAttributeType, WrappedType)																																				\
	OTYPE_INFO_IMPL(AttributeHolder<OwnedAttributeTypeIdType OMACRO_COMMA_SEPERATOR OwnedAttributeType>::AttributeWrapper<WrappedType>,WrappedType##OwnedAttributeType##AttributeWrapper)															\
	void AttributeHolder<OwnedAttributeTypeIdType, OwnedAttributeType>::AttributeWrapper<WrappedType>::OrkigeMetaExport(const char * currentOrkigeModuleName) {																						\
	typedef AttributeHolder<OwnedAttributeTypeIdType, OwnedAttributeType>::AttributeWrapper<WrappedType> ExposedClassType;																															\
	bp::class_<ExposedClassType, bp::bases<OParent>,std::shared_ptr<ExposedClassType> > py_class(AttributeHolder<OwnedAttributeTypeIdType, OwnedAttributeType>::AttributeWrapper<WrappedType>::getClassTypeInfo().getName().c_str(),bp::no_init);	\
	OCONSTRUCTOR1(WrappedType)																																																						\
	OVAR(value)																																																										\
	OFUNCCR(getValue)																																																								\
	OOBJECT_END

#define IMPLEMENT_ATTRIBUTEHOLDER(OwnedAttributeTypeIdType, OwnedAttributeType)				\
	OOBJECT_TEMPLATE_IMPL2(AttributeHolder, OwnedAttributeTypeIdType, OwnedAttributeType)	\
	OFUNC(getAttribute)																		\
	OFUNC(setAttribute)																	\
	OFUNC(hasAttribute)																	\
	OFUNC(clearAttributes)																\
	OFUNC(delAttribute)																	\
	OVAR(attributes)																	\
	OOBJECT_END
#endif
}

#endif //__AttributeHolder_h__19_8_2010__21_56_34__
