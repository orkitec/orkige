/**************************************************************
	created:	2010/08/19 at 23:00
	filename: 	ComponentHolder.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
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
		typedef std::map<TypeInfo, optr<BaseComponentType> > ComponentMap;	//!< map of Components and their Types
		typedef typename BaseComponentType::Factory OwnedComponentFactory;	//!< factory to create Components
		typedef std::map<TypeInfo, TypeInfoList> TypeInfoListMap;			//!< maps list of TypeInfos to a TypeInfo for registering dependecies
	protected:
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		ComponentMap components;											//!< created Components
	private:
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
			TypeInfo const & componentType = ComponentType::getClassTypeInfo();
			bool success = componentFactory->template registerType < ComponentType > (componentType);
			return success;
		}

		//! unregister a component to the manager (don't add it)
		template<class ComponentType> 
		static bool unregisterComponent(typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			optr<OwnedComponentFactory> componentFactory = OSelf::getComponentFactory();
			oAssert(componentFactory);
			TypeInfo const & componentType = ComponentType::getClassTypeInfo();
			bool success = componentFactory->template unRegister < ComponentType > (componentType);
			return success;
		}

		//! add a registered component by name
		inline bool addComponent(TypeInfo const & componentType);

		//! add a registered component by type
		template<typename ComponentType> 
		inline bool addComponent(typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			TypeInfo const & componentType = ComponentType::getClassTypeInfo();
			return this->addComponent(componentType);
		}

		//! add a list of components by Types
		inline bool addComponents(TypeInfoList const & componentTypes);

		//! get attached component by name
		inline woptr<BaseComponentType> getComponent(TypeInfo const & componentType);

		//! get attached component by type
		template<typename ComponentType> 
		inline woptr<ComponentType> getComponent(TypeInfo const & componentType = ComponentType::getClassTypeInfo(),
			typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			optr<BaseComponentType> baseComponent = this->getComponent(componentType).lock();
			oAssert(baseComponent);
			optr<ComponentType> component = boost::dynamic_pointer_cast<ComponentType>(baseComponent);
			oAssert(component);
			return component;
		}

		//! get attached component by name
		inline BaseComponentType* getComponentPtr(TypeInfo const & componentType);

		//! get attached component by type
		template<typename ComponentType> 
		inline ComponentType* getComponentPtr(TypeInfo const & componentType = ComponentType::getClassTypeInfo(),
			typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			BaseComponentType* baseComponent = this->getComponentPtr(componentType);
			oAssert(baseComponent);
			ComponentType* component = dynamic_cast<ComponentType*>(baseComponent);
			oAssert(component);
			return component;
		}

		//! get all attached components
		inline ComponentMap const & getComponents();

		//! remove a component
		inline bool removeComponent(TypeInfo const & componentType);

		//! remove a component
		template<typename ComponentType> 
		inline bool removeComponent(TypeInfo const & componentType = ComponentType::getClassTypeInfo(),
			typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			return this->removeComponent(componentType);
		}

		//! remove all attached components
		inline void removeAllComponents();

		//! is given component attached?
		inline bool hasComponent(TypeInfo const & componentTypes);

		//! is given component attached?
		template<typename ComponentType> 
		inline bool hasComponent(TypeInfo const & componentType = ComponentType::getClassTypeInfo(),
			typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			return this->hasComponent(componentType);
		}

		//! get Types of all attached components
		inline TypeInfoList getAttachedComponentTypes();

		//! is given component registered?
		static inline bool isComponentRegistered(TypeInfo const & componentType);

		//! is given component registered?
		template<typename ComponentType> 
		static inline bool isComponentRegistered(TypeInfo const & componentType = ComponentType::getClassTypeInfo(),
			typename boost::enable_if<boost::is_base_of<BaseComponentType, ComponentType> >::type * = 0)
		{
			return isComponentRegistered(componentType);
		}

		//! get list of all registered components
		static inline TypeInfoList getRegisteredComponentTypes();

		//! get depencies for this component type
		static inline TypeInfoList & getDependencies(TypeInfo const & componentType);
		//--- SERIALIZATION ---
		virtual void save(optr<IArchive> const & ar);
		virtual void load(optr<IArchive> const & ar);
	protected:
		//! called when a component is added
		virtual void onComponentAdded(TypeInfo const & componentType)	{};
		//! called when a component is removed
		virtual void onComponentRemoved(TypeInfo const & componentType)	{};
		//! prevent construction
		ComponentHolder() {};
		//! get static ComponentFactory for this ComponentHolder
		static optr<OwnedComponentFactory> getComponentFactory();
		//! get static dependencies map for this ComponentHolder
		static optr<TypeInfoListMap> getAllDependencies();
	private:
	};
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline bool ComponentHolder<BaseComponentType>::addComponent(TypeInfo const & componentType)
	{
		if(this->components.find(componentType) != this->components.end())
		{
			oDebugMsg("core",0,this->getTypeInfo().getName() << ": " <<this->getObjectID() << " has already a attached "<< componentType.getName() << " Component!");
			return false;
		}

		optr<OwnedComponentFactory> componentFactory = OSelf::getComponentFactory();
		oAssert(componentFactory);

		if(!componentFactory->isRegistered(componentType))
		{
			oDebugMsg("core",0,componentType.getName() << " is not registered in Component::Factory!");
			return false;
		}

		Component<typename BaseComponentType::ComponentOwnerType> * componentBase = componentFactory->create(componentType);
		oAssert(componentBase);
		BaseComponentType* component = static_cast<BaseComponentType*>(componentBase);
		oAssert(component);
		TypeInfoList const & dependencies = component->getDependencies();
		bool dependencyAdded = true;
		foreach(TypeInfo const & dependency, dependencies)
		{
			if(!this->hasComponent(dependency))
				dependencyAdded = this->addComponent(dependency);

			if(!dependencyAdded)
			{
				oDebugMsg("core",0,this->getTypeInfo().getName() << ": " <<this->getObjectID() << " Error while adding Dependency: "<< dependency.getName() << " !");
				return false;
			}
		}
		typename BaseComponentType::ComponentOwnerType* componentOwner = static_cast<typename BaseComponentType::ComponentOwnerType*>(this);
		oAssert(componentOwner);
		component->setComponentOwner(componentOwner);
		optr<BaseComponentType> componentOptr = optr<BaseComponentType>(component);
		this->components[componentType] = componentOptr;
		component->onAdd();
		foreach(typename ComponentMap::value_type const & current, components)
		{
			optr<BaseComponentType> currentComponent = current.second;
			if(currentComponent != componentOptr)
				currentComponent->onComponentAdded(component->getTypeInfo());
		}
		this->onComponentAdded(component->getTypeInfo());
		return true;	
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline bool ComponentHolder<BaseComponentType>::addComponents(TypeInfoList const & componentTypes)
	{
		bool success = true;
		foreach(TypeInfo const & componentType, componentTypes)
		{
			bool componentAdded = this->addComponent(componentType);
			if(success)
				success = componentAdded;
		}
		return success;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline woptr<BaseComponentType> ComponentHolder<BaseComponentType>::getComponent(TypeInfo const & componentType)
	{
		typename ComponentMap::iterator foundComponent = this->components.find(componentType);
		if(foundComponent == this->components.end())
		{
			oDebugMsg("core",0,this->getObjectID() << " has no attached "<< componentType.getName() << " Component!");
			return oNull<BaseComponentType>();
		}
		return foundComponent->second;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline BaseComponentType* ComponentHolder<BaseComponentType>::getComponentPtr(TypeInfo const & componentType)
	{
		typename ComponentMap::iterator foundComponent = this->components.find(componentType);
		if(foundComponent == this->components.end())
		{
			oDebugMsg("core",0,this->getObjectID() << " has no attached "<< componentType.getName() << " Component!");
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
	inline bool ComponentHolder<BaseComponentType>::removeComponent(TypeInfo const & componentType)
	{
		typename ComponentMap::iterator foundComponent = this->components.find(componentType);
		if(foundComponent == this->components.end())
		{
			oDebugMsg("core",0,this->getObjectID() << " has no attached "<< componentType.getName() << " Component!");
			return false;
		}
		optr<BaseComponentType> component = foundComponent->second;

		TypeInfoList tobeRemovedComponents;
		foreach(typename ComponentMap::value_type const & current, components)
		{
			TypeInfoList const & dependencies = current.second->getDependencies();
			foreach(TypeInfo const & dependency, dependencies)
			{
				if(dependency == componentType)
				{
					tobeRemovedComponents.push_back(current.first);
					break;
				}
			}
		}
		foreach(TypeInfo const & tobeRemovedComponent, tobeRemovedComponents)
		{
			this->removeComponent(tobeRemovedComponent);
		}

		//needs to be called before this->components.erase(foundComponent) to still have acces to the component
		this->onComponentRemoved(component->getTypeInfo());

		this->components.erase(foundComponent);
		component->onRemove();
		foreach(typename ComponentMap::value_type const & current, components)
		{
			current.second->onComponentRemoved(component->getTypeInfo());
		}
		component->setComponentOwner(NULL);
		component = oNull<BaseComponentType>();
		return true;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline void ComponentHolder<BaseComponentType>::removeAllComponents()
	{
		TypeInfoList componentTypes = this->getAttachedComponentTypes();
		while(!componentTypes.empty())
		{
			this->removeComponent(componentTypes.front());
			componentTypes = this->getAttachedComponentTypes();
		}
		this->components.clear();
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline bool ComponentHolder<BaseComponentType>::hasComponent(TypeInfo const & componentType)
	{
		bool isComponentAdded = this->components.find(componentType) != this->components.end();
		return isComponentAdded; 
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline TypeInfoList ComponentHolder<BaseComponentType>::getAttachedComponentTypes()
	{
		TypeInfoList componentTypes;
		typename ComponentMap::const_iterator it = this->components.begin();
		typename ComponentMap::const_iterator itend = this->components.end();
		for(; it != itend; ++it)
		{
			componentTypes.push_back(it->first);
		}
		return componentTypes;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline bool ComponentHolder<BaseComponentType>::isComponentRegistered(TypeInfo const & componentType)
	{
		optr<OwnedComponentFactory> componentFactory = OSelf::getComponentFactory();
		oAssert(componentFactory);
		bool isRegistered = componentFactory->isRegistered(componentType);
		return isRegistered; 
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline TypeInfoList ComponentHolder<BaseComponentType>::getRegisteredComponentTypes()
	{
		TypeInfoList componentTypes;
		optr<OwnedComponentFactory> componentFactory = OSelf::getComponentFactory();
		oAssert(componentFactory);
		typename OwnedComponentFactory::const_iterator it = componentFactory->begin();
		typename OwnedComponentFactory::const_iterator itend = componentFactory->end();
		for(; it != itend; ++it)
		{
			componentTypes.push_back(it->first);
		}
		return componentTypes;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline TypeInfoList & ComponentHolder<BaseComponentType>::getDependencies(TypeInfo const & componentType)
	{
		optr<TypeInfoListMap> allDependencies = OSelf::getAllDependencies();
		typename TypeInfoListMap::iterator it = allDependencies->find(componentType);
		if(it == allDependencies->end())
		{
			(*allDependencies)[componentType] = TypeInfoList();
		}
		it = allDependencies->find(componentType);
		return it->second;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline void ComponentHolder<BaseComponentType>::save(optr<IArchive> const & ar)
	{
		OParent::save(ar);
		//@TODO: fix serialisation for TypeInfo
		//ar << this->components;
	}
	//---------------------------------------------------------
	template<class BaseComponentType>
	inline void ComponentHolder<BaseComponentType>::load(optr<IArchive> const & ar)
	{
		OParent::load(ar);
		//@TODO: fix serialisation for TypeInfo
		//ar >> this->components;
	}
	//---------------------------------------------------------
	//has to be called before exporting your own module
	//you need also to OEXPORT(ComponentHolder<BaseComponentType>)


#define IMPLEMENT_COMPONENTHOLDER(BaseComponentType)																			\
	template <> optr<BaseComponentType::Factory> ComponentHolder<BaseComponentType>::getComponentFactory()						\
	{																															\
	static optr<BaseComponentType::Factory> staticComponentFactory##BaseComponentType;											\
	if(!staticComponentFactory##BaseComponentType)																				\
	{																															\
	staticComponentFactory##BaseComponentType = onew(new BaseComponentType::Factory());											\
	oDebugMsg("core",0,#BaseComponentType <<"::Factory created!");																\
	}																															\
	return staticComponentFactory##BaseComponentType;																			\
	}																															\
	template <> optr<ComponentHolder<BaseComponentType>::TypeInfoListMap> ComponentHolder<BaseComponentType>::getAllDependencies()	\
	{																															\
	static optr<ComponentHolder<BaseComponentType>::TypeInfoListMap> staticDependencies##BaseComponentType;						\
	if(!staticDependencies##BaseComponentType)																					\
	{																															\
	staticDependencies##BaseComponentType = onew(new ComponentHolder<BaseComponentType>::TypeInfoListMap());						\
	}																															\
	return staticDependencies##BaseComponentType;																				\
	}																															\
	OOBJECT_TEMPLATE_IMPL(ComponentHolder,BaseComponentType)																	\
	OCONSTRUCTOR1(String)																										\
	OFUNC(addComponents)																										\
	OFUNCCR(getComponents)																										\
	OFUNC(removeComponent)																										\
	OFUNC(hasComponent)																											\
	OFUNC(getAttachedComponentTypes)																							\
	OSTATICFUNC(isComponentRegistered)																							\
	OSTATICFUNC(getRegisteredComponentTypes)																					\
	OOBJECT_END

}

#endif //__ComponentHolder_h__19_8_2010__23_00_22__
