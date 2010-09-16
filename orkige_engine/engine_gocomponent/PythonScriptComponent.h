//created 2008/04/28
#ifndef __PYTHONSCRIPTCOMPONENT_H__
#define __PYTHONSCRIPTCOMPONENT_H__

#include <core_game/GameObject.h>
#ifndef ORKIGE_NOSCRIPT
namespace Orkige
{
	class ORKIGE_DLL PythonScriptComponent : public GameObjectComponent
	{
		OOBJECT(PythonScriptComponent,GameObjectComponent)
	// Attributes --------------------------------------------------------------
	public:
	protected:
		typedef std::map<String, bp::object> PyScriptInstanceRegistry;
		PyScriptInstanceRegistry scriptInstances;
		bp::object py_sys;
		bp::object py_stderr;
		bp::object py_stringio;
		bp::object py_module_stringio;
		bp::object py_self;
	private:
	// Methods -----------------------------------------------------------------
	public:
		PythonScriptComponent();
		virtual ~PythonScriptComponent();

		//! add a script to this component
		bool addScript(String const & scriptFileName);
		
		//! check if the specified script is attached to this component
		inline bool hasScriptInstance(String const & scriptFileName);

		//! retrieve a attached script instance
		inline bp::object const & getScriptInstance(String const & scriptFileName);

		//! call the specified function (with max 5 parameters) on all attached scripts
		inline void callFunction(String const & functionName);
		template<typename ParamType1>
		inline void callFunction(String const & functionName, typename ParamType1 const & param1);
		template<typename ParamType1,typename ParamType2>
		inline void callFunction(String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2);
		template<typename ParamType1, typename ParamType2, typename ParamType3>
		inline void callFunction(String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3);
		template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4>
		inline void callFunction(String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4);
		template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4, typename ParamType5>
		inline void callFunction(String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4, typename ParamType5 const & param5);

		//! call the specified function (with max 5 parameters) on the specified script
		inline bool callInstanceFunction(String const & scriptFileName, String const & functionName);
		template<typename ParamType1>
		inline bool callInstanceFunction(String const & scriptFileName, String const & functionName, typename ParamType1 const & param1);
		template<typename ParamType1, typename ParamType2>
		inline bool callInstanceFunction(String const & scriptFileName, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2);
		template<typename ParamType1, typename ParamType2, typename ParamType3>
		inline bool callInstanceFunction(String const & scriptFileName, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3);
		template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4>
		inline bool callInstanceFunction(String const & scriptFileName, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4);
		template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4, typename ParamType5>
		inline bool callInstanceFunction(String const & scriptFileName, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4, typename ParamType5 const & param5);

	protected:
		//! call the specified function (with max 5 parameters) on the given script instance
		inline bool callInstanceFunction(bp::object const & scriptInstance, String const & functionName);
		template<typename ParamType1>
		inline bool callInstanceFunction(bp::object const & scriptInstance, String const & functionName, typename ParamType1 const & param1);
		template<typename ParamType1, typename ParamType2>
		inline bool callInstanceFunction(bp::object const & scriptInstance, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2);
		template<typename ParamType1, typename ParamType2, typename ParamType3>
		inline bool callInstanceFunction(bp::object const & scriptInstance, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3);
		template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4>
		inline bool callInstanceFunction(bp::object const & scriptInstance, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4);
		template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4, typename ParamType5>
		inline bool callInstanceFunction(bp::object const & scriptInstance, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4, typename ParamType5 const & param5);

		//! some helpers for printing pythonerrors in the ide
		inline void prepareError();
		inline void printError(bp::object const & scriptInstance);
		inline void resetError();

		//! component override gets called after the component is attached to a gameobject
		virtual void onAdd();
		//! component override gets called before the component is removed from a gameobject
		virtual void onRemove();
		//! component override gets called if a new component gets added to the owner gameobject
		virtual void onComponentAdded(String const & componentTypeName);
		//! component override gets called if a component gets removed from the owner gameobject
		virtual void onComponentRemoved(String const & componentTypeName);

	private:
	};
	//--------------------------------------------------------------------------------------------
	inline bp::object const & PythonScriptComponent::getScriptInstance(String const & scriptFileName)
	{
		oAssert(this->hasScriptInstance(scriptFileName));
		return this->scriptInstances[scriptFileName];
	}
	//--------------------------------------------------------------------------------------------
	inline bool PythonScriptComponent::hasScriptInstance(String const & scriptFileName)
	{
		return this->scriptInstances.find(scriptFileName) != this->scriptInstances.end();
	}
	//--------------------------------------------------------------------------------------------
	inline void PythonScriptComponent::callFunction(String const & functionName)
	{
		foreach(PyScriptInstanceRegistry::value_type const & registry, scriptInstances)
		{
			this->callInstanceFunction(registry.second,functionName);
		}
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1>
	inline void PythonScriptComponent::callFunction(String const & functionName, typename ParamType1 const & param1)
	{
		foreach(PyScriptInstanceRegistry::value_type const & registry, scriptInstances)
		{
			this->callInstanceFunction(registry.second, functionName, param1);
		}
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1,typename ParamType2>
	inline void PythonScriptComponent::callFunction(String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2)
	{
		foreach(PyScriptInstanceRegistry::value_type const & registry, scriptInstances)
		{
			this->callInstanceFunction(registry.second, functionName, param1, param2);
		}
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1, typename ParamType2, typename ParamType3>
	inline void PythonScriptComponent::callFunction(String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3)
	{
		foreach(PyScriptInstanceRegistry::value_type const & registry, scriptInstances)
		{
			this->callInstanceFunction(registry.second,functionName, param1, param2, param3);
		}
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4>
	inline void PythonScriptComponent::callFunction(String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4)
	{
		foreach(PyScriptInstanceRegistry::value_type const & registry, scriptInstances)
		{
			this->callInstanceFunction(registry.second,functionName, param1, param2, param3, param4);
		}
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4, typename ParamType5>
	inline void PythonScriptComponent::callFunction(String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4, typename ParamType5 const & param5)
	{
		foreach(PyScriptInstanceRegistry::value_type const & registry, scriptInstances)
		{
			this->callInstanceFunction(registry.second,functionName, param1, param2, param3, param4, param5);
		}
	}
	//--------------------------------------------------------------------------------------------
	inline bool PythonScriptComponent::callInstanceFunction(bp::object  const & scriptInstance, String const & functionName)
	{
		bool retvalue = false;
		try {
			this->prepareError();
			bp::call_method<void>(scriptInstance.ptr(), functionName.c_str(), this->py_self);
			retvalue = true;
		} catch (bp::error_already_set) {
			this->printError(scriptInstance);
		}
		this->resetError();
		return retvalue;
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1>
	inline bool PythonScriptComponent::callInstanceFunction(bp::object  const & scriptInstance, String const & functionName, typename ParamType1 const & param1)
	{
		bool retvalue = false;
		try {
			this->prepareError();
			bp::call_method<void>(scriptInstance.ptr(), functionName.c_str(), this->py_self, param1);
			retvalue = true;
		} catch (bp::error_already_set) {
			this->printError(scriptInstance);
		}
		this->resetError();
		return retvalue;
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1,typename ParamType2>
	inline bool PythonScriptComponent::callInstanceFunction(bp::object  const & scriptInstance, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2)
	{
		bool retvalue = false;
		try {
			this->prepareError();
			bp::call_method<void>(scriptInstance.ptr(), functionName.c_str(), this->py_self, param1, param2);
			retvalue = true;
		} catch (bp::error_already_set) {
			this->printError(scriptInstance);
		}
		this->resetError();
		return retvalue;
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1, typename ParamType2, typename ParamType3>
	inline bool PythonScriptComponent::callInstanceFunction(bp::object  const & scriptInstance, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3)
	{
		bool retvalue = false;
		try {
			this->prepareError();
			bp::call_method<void>(scriptInstance.ptr(), functionName.c_str(), this->py_self, param1, param2, param3);
			retvalue = true;
		} catch (bp::error_already_set) {
			this->printError(scriptInstance);
		}
		this->resetError();
		return retvalue;
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4>
	inline bool PythonScriptComponent::callInstanceFunction(bp::object  const & scriptInstance, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4)
	{
		bool retvalue = false;
		try {
			this->prepareError();
			bp::call_method<void>(scriptInstance.ptr(), functionName.c_str(), this->py_self, param1, param2, param3, param4);
			retvalue = true;
		} catch (bp::error_already_set) {
			this->printError(scriptInstance);
		}
		this->resetError();
		return retvalue;
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4, typename ParamType5>
	inline bool PythonScriptComponent::callInstanceFunction(bp::object  const & scriptInstance, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4, typename ParamType5 const & param5)
	{
		bool retvalue = false;
		try {
			this->prepareError();
			bp::call_method<void>(scriptInstance.ptr(), functionName.c_str(), this->py_self, param1, param2, param3, param4, param5);
			retvalue = true;
		} catch (bp::error_already_set) {
			this->printError(scriptInstance);
		}
		this->resetError();
		return retvalue;
	}
	//--------------------------------------------------------------------------------------------
	inline bool PythonScriptComponent::callInstanceFunction(String const & scriptFileName, String const & functionName)
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		if(this->hasScriptInstance(scriptFileName))
		{
			bp::object const & scriptInstance = this->getScriptInstance(scriptFileName);
			return this->callInstanceFunction(scriptInstance,functionName);
		}
		else
		{
			oDebugMsg("python",0,componentOwner->getTypeInfo().getName() <<": "<< componentOwner->getObjectID() << " has no attached Script: "<<scriptFileName);
			return false;
		}
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1>
	inline bool PythonScriptComponent::callInstanceFunction(String const & scriptFileName, String const & functionName, typename ParamType1 const & param1)
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		if(this->hasScriptInstance(scriptFileName))
		{
			bp::object const & scriptInstance = this->getScriptInstance(scriptFileName);
			return this->callInstanceFunction(scriptInstance, functionName, param1);
		}
		else
		{
			oDebugMsg("python",0,componentOwner->getTypeInfo().getName() <<": "<< componentOwner->getObjectID() << " has no attached Script: "<<scriptFileName);
			return false;
		}
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1,typename ParamType2>
	inline bool PythonScriptComponent::callInstanceFunction(String const & scriptFileName, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2)
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		if(this->hasScriptInstance(scriptFileName))
		{
			bp::object const & scriptInstance = this->getScriptInstance(scriptFileName);
			return this->callInstanceFunction(scriptInstance, functionName, param1, param2);
		}
		else
		{
			oDebugMsg("python",0,componentOwner->getTypeInfo().getName() <<": "<< componentOwner->getObjectID() << " has no attached Script: "<<scriptFileName);
			return false;
		}
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1, typename ParamType2, typename ParamType3>
	inline bool PythonScriptComponent::callInstanceFunction(String const & scriptFileName, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3)
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		if(this->hasScriptInstance(scriptFileName))
		{
			bp::object const & scriptInstance = this->getScriptInstance(scriptFileName);
			return this->callInstanceFunction(scriptInstance, functionName, param1, param2, param3);
		}
		else
		{
			oDebugMsg("python",0,componentOwner->getTypeInfo().getName() <<": "<< componentOwner->getObjectID() << " has no attached Script: "<<scriptFileName);
			return false;
		}
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4>
	inline bool PythonScriptComponent::callInstanceFunction(String const & scriptFileName, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4)
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		if(this->hasScriptInstance(scriptFileName))
		{
			bp::object const & scriptInstance = this->getScriptInstance(scriptFileName);
			return this->callInstanceFunction(scriptInstance, functionName, param1, param2, param3, param4);
		}
		else
		{
			oDebugMsg("python",0,componentOwner->getTypeInfo().getName() <<": "<< componentOwner->getObjectID() << " has no attached Script: "<<scriptFileName);
			return false;
		}
	}
	//--------------------------------------------------------------------------------------------
	template<typename ParamType1, typename ParamType2, typename ParamType3, typename ParamType4, typename ParamType5>
	inline bool PythonScriptComponent::callInstanceFunction(String const & scriptFileName, String const & functionName, typename ParamType1 const & param1, typename ParamType2 const & param2, typename ParamType3 const & param3, typename ParamType4 const & param4, typename ParamType5 const & param5)
	{
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		if(this->hasScriptInstance(scriptFileName))
		{
			bp::object const & scriptInstance = this->getScriptInstance(scriptFileName);
			return this->callInstanceFunction(scriptInstance, functionName, param1, param2, param3, param4, param5);
		}
		else
		{
			oDebugMsg("python",0,componentOwner->getTypeInfo().getName() <<": "<< componentOwner->getObjectID() << " has no attached Script: "<<scriptFileName);
			return false;
		}
	}
};
#endif ORKIGE_NOSCRIPT
#endif //__PYTHONSCRIPTCOMPONENT_H__