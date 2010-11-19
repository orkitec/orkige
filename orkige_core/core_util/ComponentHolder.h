/**************************************************************
	created:	2010/08/19 at 23:00
	filename: 	ComponentHolder.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#ifndef __ComponentHolder_h__19_8_2010__23_00_22__
#define __ComponentHolder_h__19_8_2010__23_00_22__

#include "core_util/Component.h"
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_base_of.hpp>
#include "core_util/foreach.h"

namespace Orkige
{
	//! generic component management
	template<typename BaseComponentType>
	class ORKIGE_DLL ComponentHolder : public Object
	{
		OOBJECT(ComponentHolder<BaseComponentType>,Object)
		//--- Types -------------------------------------------
	public:
		typedef BaseComponentType OwnedComponentType;						//!< definition of the owned Component Type
		typedef std::map<String, optr<BaseComponentType> > ComponentMap;	//!< map of Component's and their names
		typedef typename BaseComponentType::Factory OwnedComponentFactory;	//!< factory to create Components
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
	private:
		ComponentMap components;											//!< created Component's
		//--- Methods -----------------------------------------
	public:
		//! constructor
		explicit ComponentHolder(String const & id) : Object(id) {};
		//! destructor
		virtual ~ComponentHolder()
		{
			this->removeAllComponents();
		};

		//! register a component to the manager (don't add it)
		template<class ComponentType> 
		static bool registerComponent(typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			optr<OwnedComponentFactory> componentFactory = OSelf::getComponentFactory();
			oAssert(componentFactory);
			String const & componentTypeName = ComponentType::getClassTypeInfo().getName();
			bool success = componentFactory->template registerType < ComponentType > (componentTypeName);
			return success;
		}


		//! unregister a component to the manager (don't add it)
		template<class ComponentType> 
		static bool unregisterComponent(typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			optr<OwnedComponentFactory> componentFactory = OSelf::getComponentFactory();
			oAssert(componentFactory);
			String const & componentTypeName = ComponentType::getClassTypeInfo().getName();
			bool success = componentFactory->template unRegister < ComponentType > (componentTypeName);
			return success;
		}

		//! add a registered component by name
		inline bool addComponent(String const & componentTypeName);

		//! add a registered component by type
		template<typename ComponentType> 
		inline bool addComponent(typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			String const & componentTypeName = ComponentType::getClassTypeInfo().getName();
			return this->addComponent(componentTypeName);
		}

		//! add a list of components by names
		inline bool addComponents(StringList const & componentTypeNames);

		//! get attached component by name
		inline woptr<BaseComponentType> getComponent(String const & componentTypeName);

		//! get attached component by type
		template<typename ComponentType> 
		inline woptr<ComponentType> getComponent(String const & componentTypeName = ComponentType::getClassTypeInfo().getName(),
			typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			optr<BaseComponentType> baseComponent = this->getComponent(componentTypeName).lock();
			oAssert(baseComponent);
			optr<ComponentType> component = boost::dynamic_pointer_cast<ComponentType>(baseComponent);
			oAssert(component);
			return component;
		}

				//! get attached component by name
		inline BaseComponentType* getComponentPtr(String const & componentTypeName);

		//! get attached component by type
		template<typename ComponentType> 
		inline ComponentType* getComponentPtr(String const & componentTypeName = ComponentType::getClassTypeInfo().getName(),
			typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			BaseComponentType* baseComponent = this->getComponentPtr(componentTypeName);
			oAssert(baseComponent);
			ComponentType* component = dynamic_cast<ComponentType*>(baseComponent);
			oAssert(component);
			return component;
		}
		//! get all attached components
		inline ComponentMap const & getComponents();

		//! remove a component
		inline bool removeComponent(String const & componentTypeName);

		//! remove all attached components
		inline void removeAllComponents();

		//! is given component attached?
		inline bool hasComponent(String const & componentTypeNames);

		//! get names of all attached components
		inline StringList getAttachedComponentNames();

		//! is given component registered?
		static inline bool isComponentRegistered(String const & componentTypeName);

		//! get list of all registered components
		static inline StringList getRegisteredComponentNames();

		//--- SERIALIZATION ---
		virtual void save(optr<IArchive> const & ar);
		virtual void load(optr<IArchive> const & ar);
	protected:
		//! prevent construction
		ComponentHolder(){};
		//! get static componentfactory for this ComponentHolder
		static optr<OwnedComponentFactory> getComponentFactory();
	private:
	};
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline bool ComponentHolder<BaseComponentType>::addComponent(String const & componentTypeName)
	{
		if(this->components.find(componentTypeName) != this->components.end())
		{
			oDebugMsg("core",0,this->getTypeInfo().getName() << ": " <<this->getObjectID() << " has already a attached "<< componentTypeName << " Component!");
			return false;
		}

		optr<OwnedComponentFactory> componentFactory = OSelf::getComponentFactory();
		oAssert(componentFactory);

		if(!componentFactory->isRegistered(componentTypeName))
		{
			oDebugMsg("core",0,componentTypeName << " is not registered in Component::Factory!");
			return false;
		}

		Component<typename BaseComponentType::ComponentOwnerType> * componentBase = componentFactory->create(componentTypeName);
		oAssert(componentBase);
		BaseComponentType* component = static_cast<BaseComponentType*>(componentBase);
		oAssert(component);
		StringList const & dependencies = component->getDependencies();
		bool dependencyAdded = true;
		foreach(String const & dependency, dependencies)
		{
			if(!this->hasComponent(dependency))
				dependencyAdded = this->addComponent(dependency);

			if(!dependencyAdded)
			{
				oDebugMsg("core",0,this->getTypeInfo().getName() << ": " <<this->getObjectID() << " Error while adding Dependency: "<< dependency << " !");
				return false;
			}
		}
		typename BaseComponentType::ComponentOwnerType* componentOwner = static_cast<typename BaseComponentType::ComponentOwnerType*>(this);
		oAssert(componentOwner);
		component->setComponentOwner(componentOwner);
		optr<BaseComponentType> componentOptr = optr<BaseComponentType>(component);
		this->components[componentTypeName] = componentOptr;
		component->onAdd();
		foreach(typename ComponentMap::value_type const & current, components)
		{
			optr<BaseComponentType> currentComponent = current.second;
			if(currentComponent != componentOptr)
				currentComponent->onComponentAdded(component->getTypeInfo().getName());
		}
		return true;	
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline bool ComponentHolder<BaseComponentType>::addComponents(StringList const & componentTypeNames)
	{
		bool success = true;
		foreach(String const & componentTypeName, componentTypeNames)
		{
			bool componentAdded = this->addComponent(componentTypeName);
			if(success)
				success = componentAdded;
		}
		return success;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline woptr<BaseComponentType> ComponentHolder<BaseComponentType>::getComponent(String const & componentTypeName)
	{
		typename ComponentMap::iterator foundComponent = this->components.find(componentTypeName);
		if(foundComponent == this->components.end())
		{
			oDebugMsg("core",0,this->getObjectID() << " has no attached "<< componentTypeName << " Component!");
			return oNull<BaseComponentType>();
		}
		return foundComponent->second;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline BaseComponentType* ComponentHolder<BaseComponentType>::getComponentPtr(String const & componentTypeName)
	{
		typename ComponentMap::iterator foundComponent = this->components.find(componentTypeName);
		if(foundComponent == this->components.end())
		{
			oDebugMsg("core",0,this->getObjectID() << " has no attached "<< componentTypeName << " Component!");
			return NULL;
		}
		return foundComponent->second.get();
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline typename ComponentHolder<BaseComponentType>::ComponentMap const & ComponentHolder<BaseComponentType>::getComponents()
	{
		return this->components;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline bool ComponentHolder<BaseComponentType>::removeComponent(String const & componentTypeName)
	{
		typename ComponentMap::iterator foundComponent = this->components.find(componentTypeName);
		if(foundComponent == this->components.end())
		{
			oDebugMsg("core",0,this->getObjectID() << " has no attached "<< componentTypeName << " Component!");
			return false;
		}
		optr<BaseComponentType> component = foundComponent->second;

		StringList tobeRemovedComponents;
		foreach(typename ComponentMap::value_type const & current, components)
		{
			StringList const & dependencies = current.second->getDependencies();
			foreach(String const & dependency, dependencies)
			{
				if(dependency == componentTypeName)
				{
					tobeRemovedComponents.push_back(current.first);
					break;
				}
			}
		}
		foreach(String const & tobeRemovedComponent, tobeRemovedComponents)
		{
			this->removeComponent(tobeRemovedComponent);
		}

		this->components.erase(foundComponent);
		component->onRemove();
		foreach(typename ComponentMap::value_type const & current, components)
		{
			current.second->onComponentRemoved(component->getTypeInfo().getName());
		}
		component->setComponentOwner(NULL);
		component = oNull<BaseComponentType>();
		return true;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline void ComponentHolder<BaseComponentType>::removeAllComponents()
	{
		StringList componentNames = this->getAttachedComponentNames();
		while(!componentNames.empty())
		{
			this->removeComponent(componentNames.front());
			componentNames = this->getAttachedComponentNames();
		}
		this->components.clear();
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline bool ComponentHolder<BaseComponentType>::hasComponent(String const & componentTypeName)
	{
		bool isComponentAdded = this->components.find(componentTypeName) != this->components.end();
		return isComponentAdded; 
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline StringList ComponentHolder<BaseComponentType>::getAttachedComponentNames()
	{
		StringList componentNames;
		typename ComponentMap::const_iterator it = this->components.begin();
		typename ComponentMap::const_iterator itend = this->components.end();
		for(; it != itend; ++it)
		{
			componentNames.push_back(it->first);
		}
		return componentNames;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline bool ComponentHolder<BaseComponentType>::isComponentRegistered(String const & componentTypeName)
	{
		optr<OwnedComponentFactory> componentFactory = OSelf::getComponentFactory();
		oAssert(componentFactory);
		bool isRegistered = componentFactory->isRegistered(componentTypeName);
		return isRegistered; 
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline StringList ComponentHolder<BaseComponentType>::getRegisteredComponentNames()
	{
		StringList componentNames;
		optr<OwnedComponentFactory> componentFactory = OSelf::getComponentFactory();
		oAssert(componentFactory);
		typename OwnedComponentFactory::const_iterator it = componentFactory->begin();
		typename OwnedComponentFactory::const_iterator itend = componentFactory->end();
		for(; it != itend; ++it)
		{
			componentNames.push_back(it->first);
		}
		return componentNames;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline void ComponentHolder<BaseComponentType>::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		ar << this->components;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline void ComponentHolder<BaseComponentType>::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		ar >> this->components;
	}
	//---------------------------------------------------------
	//has to be called before exporting your own module
	//you need also to OEXPORT(ComponentHolder<BaseComponentType>)

#ifndef __WIN32__
#define IMPLEMENT_COMPONENTHOLDER(BaseComponentType)											\
	template <> optr<BaseComponentType::Factory> ComponentHolder<BaseComponentType>::getComponentFactory()	\
	{																							\
	static optr<BaseComponentType::Factory> staticComponentFactory##BaseComponentType;		\
	if(!staticComponentFactory##BaseComponentType)											\
	{																						\
	staticComponentFactory##BaseComponentType = onew(new BaseComponentType::Factory());	\
	oDebugMsg("core",0,#BaseComponentType <<"::Factory created!");						\
	}																						\
	return staticComponentFactory##BaseComponentType;										\
	}																							\
	OOBJECT_TEMPLATE_IMPL(ComponentHolder,BaseComponentType)									\
	OCONSTRUCTOR1(String)																	\
	/*OFUNC_OVERL1(bool, addComponent, String &)*/																		\
	OFUNC(addComponents)																	\
	/*OFUNC_OVERL1(optr<BaseComponentType>, getComponent, String &)*/																	\
	OFUNCCR(getComponents)																	\
	OFUNC(removeComponent)																	\
	OFUNC(hasComponent)																		\
	OFUNC(getAttachedComponentNames)														\
	OSTATICFUNC(isComponentRegistered)														\
	OSTATICFUNC(getRegisteredComponentNames)												\
	OOBJECT_END
#else
#define IMPLEMENT_COMPONENTHOLDER(BaseComponentType)											\
	optr<BaseComponentType::Factory> ComponentHolder<BaseComponentType>::getComponentFactory()	\
	{																							\
	static optr<BaseComponentType::Factory> staticComponentFactory##BaseComponentType;		\
	if(!staticComponentFactory##BaseComponentType)											\
	{																						\
	staticComponentFactory##BaseComponentType = onew(new BaseComponentType::Factory());	\
	oDebugMsg("core",0,#BaseComponentType <<"::Factory created!");						\
	}																						\
	return staticComponentFactory##BaseComponentType;										\
	}																							\
	OOBJECT_TEMPLATE_IMPL(ComponentHolder,BaseComponentType)									\
	OCONSTRUCTOR1(String)																	\
	/*OFUNC(addComponent)*/																		\
	OFUNC(addComponents)																	\
	/*OFUNC(getComponent)	*/																	\
	OFUNCCR(getComponents)																	\
	OFUNC(removeComponent)																	\
	OFUNC(hasComponent)																		\
	OFUNC(getAttachedComponentNames)														\
	OSTATICFUNC(isComponentRegistered)														\
	OSTATICFUNC(getRegisteredComponentNames)												\
	OOBJECT_END
#endif
}

#endif //__ComponentHolder_h__19_8_2010__23_00_22__
