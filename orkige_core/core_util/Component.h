/**************************************************************
	created:	2010/08/19 at 22:54
	filename: 	Component.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __Component_h__19_8_2010__22_54_36__
#define __Component_h__19_8_2010__22_54_36__

#include "core_util/ObjectFactory.h"
#include "core_base/Object.h"
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_base_of.hpp>

namespace Orkige
{
	//! Generic Component
	template<typename OwnerType>
	class ORKIGE_DLL Component : public Object
	{
		OOBJECT(Component<OwnerType>,Object)
		//--- Types -------------------------------------------
	public:
		typedef OwnerType ComponentOwnerType;								//!< definition of the ComponentOwner
		typedef ObjectFactory<Component<OwnerType> * (), TypeInfo> Factory;	//!< Factory definitions for this Component
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		OwnerType* owner;					//!< pointer to the owner of this component
		//--- Methods -----------------------------------------
	public:
		//! constructor
		explicit Component()								{	this->owner = NULL;		}
		//! destructor
		virtual ~Component(){};
		//! only for internal use! called by the owner on attachment
		inline void setComponentOwner(OwnerType* _owner)	{	this->owner = _owner;	}
		//! get the owner of this component
		inline OwnerType* getComponentOwner()				{	return this->owner;		}
		//! get list of component names this component depends on
		inline TypeInfoList const & getDependencies();

		//! called when another component is added to the owner
		virtual void onComponentAdded(TypeInfo const & componentType){};
		//! called when another component is removed from the owner
		virtual void onComponentRemoved(TypeInfo const & componentType){};
		//! called when this component is added
		virtual void onAdd(){};
		//! called when this component is removed
		virtual void onRemove(){};
	protected:
		//! add a dependency to the dependency list
		inline void addDependency(TypeInfo const & componentType);
		//! add a dependency to the dependency list
		template<typename ComponentType> 
		inline void addDependency(TypeInfo const & componentType = ComponentType::getClassTypeInfo(),
			typename boost::enable_if<boost::is_base_of<Component<OwnerType>, ComponentType> >::type * = 0)
		{
			this->addDependency(componentType);	
		}
	private:
	};
	//---------------------------------------------------------
	template<typename OwnerType>
	inline TypeInfoList const & Component<OwnerType>::getDependencies()
	{
		return OwnerType::getDependencies(this->getTypeInfo());
	}
	//---------------------------------------------------------
	template<typename OwnerType>
	inline void Component<OwnerType>::addDependency(TypeInfo const & componentType)
	{
		TypeInfoList & componentDependencies = OwnerType::getDependencies(this->getTypeInfo());
		if(std::find(componentDependencies.begin(), componentDependencies.end(), componentType) == componentDependencies.end())
		{
			componentDependencies.push_back(componentType);
		}
	}

	//! has to be called before exporting your own module
	//! you need also to OEXPORT(Component<OwnerType>)
#define IMPLEMENT_COMPONENT(OwnerType)			\
	OOBJECT_TEMPLATE_IMPL(Component,OwnerType)	\
	OOBJECT_END
}
#endif //__Component_h__19_8_2010__22_54_36__