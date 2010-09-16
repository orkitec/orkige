//created 2008/05/17
#include "engine_gocomponent/PythonScriptComponent.h"
#ifndef ORKIGE_NOSCRIPT
#include <boost/algorithm/string.hpp>
#include <boost/python.hpp>

namespace Orkige
{
	PythonScriptComponent::PythonScriptComponent()
	{
		this->py_sys = bp::import ("sys");
		this->py_stderr = this->py_sys.attr("stderr");
		this->py_module_stringio = bp::import("cStringIO");
	}
	//--------------------------------------------------------------------------------------------
	PythonScriptComponent::~PythonScriptComponent()
	{
	}
	//--------------------------------------------------------------------------------------------
	void PythonScriptComponent::onAdd()
	{
		// create a proxy for the owner so we can set python attributes on it
		// using a bad pointer so that it does not get accidentally destroyed from the python side
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		this->py_self = bp::object(oBadPointer(componentOwner)); 
	}
	//--------------------------------------------------------------------------------------------
	void PythonScriptComponent::onRemove()
	{
		this->callFunction("OnDestroy");
		this->scriptInstances.clear();
		this->py_self = bp::object();
	}
	//--------------------------------------------------------------------------------------------
	void PythonScriptComponent::onComponentAdded(String const & componentTypeName)
	{
		this->callFunction("OnComponentAdded",componentTypeName);
	}
	//--------------------------------------------------------------------------------------------
	void PythonScriptComponent::onComponentRemoved(String const & componentTypeName)
	{
		this->callFunction("OnComponentRemoved",componentTypeName);
	}
	//--------------------------------------------------------------------------------------------
	void PythonScriptComponent::prepareError()
	{
		//create a cStringIO instance and link it to the stderr
		this->py_stringio = bp::call_method<bp::object>(this->py_module_stringio.ptr(),"StringIO");
		this->py_sys.attr("stderr") = this->py_stringio;
	}
	//--------------------------------------------------------------------------------------------
	void PythonScriptComponent::printError(bp::object  const & scriptInstance)
	{
		PyErr_Print();
		String err_text = bp::call_method<String>(this->py_stringio.ptr(),"getvalue");
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		foreach(PyScriptInstanceRegistry::value_type const & registry, scriptInstances)
		{
			if(registry.second == scriptInstance)
			{
				oDebugMsg("python",0,componentOwner->getTypeInfo().getName() <<": "<< componentOwner->getObjectID() << " -> " <<registry.first<<": "<<err_text);
#ifdef _DEBUG
				bp::call_method<void>(this->py_stderr.ptr(),"write",_orkige_debug_msg_stringstream.str());
#endif
				break;
			}
		}
	}
	//--------------------------------------------------------------------------------------------
	void PythonScriptComponent::resetError()
	{
		//close the stringio instance and set stderr back to default output
		bp::call_method<bp::object>(this->py_stringio.ptr(),"close");
		this->py_sys.attr("stderr") = this->py_stderr;
	}
	//--------------------------------------------------------------------------------------------
	bool PythonScriptComponent::addScript(String const & scriptFileName)
	{
		bool retvalue = false;
		if(scriptFileName.find('/') != String::npos || scriptFileName.find('\\') != String::npos)
		{
			oDebugMsg("python",0,"Couldn't add script:"<<scriptFileName<<" Path prefixes are not allowed!");
			return retvalue;
		}
		if(scriptFileName.find(".py") == String::npos)
		{
			oDebugMsg("python",0,scriptFileName<<" doesn't seem to be a python script!");
			return retvalue;
		}
		if(this->hasScriptInstance(scriptFileName))
		{
			oDebugMsg("python",0,scriptFileName<<" is already attached!");
			return retvalue;
		}

		std::vector<String> splittedFileName;
		boost::split(splittedFileName, scriptFileName, boost::is_punct());
		GameObject* componentOwner = this->getComponentOwner();
		oAssert(componentOwner);
		try {
			this->prepareError();
			bp::object const & scriptInstance = bp::import(splittedFileName[0].c_str());
			this->scriptInstances[scriptFileName] = scriptInstance;
			retvalue = true;
			bp::call_method<void>(scriptInstance.ptr(), "OnCreate", this->py_self);
		} catch (bp::error_already_set) {
			PyErr_Print();
			String err_text = bp::call_method<String>(this->py_stringio.ptr(),"getvalue");//bp::extract<String>(this->py_stringio.attr("getvalue")());
			oDebugMsg("python",0,componentOwner->getTypeInfo().getName() <<": "<< componentOwner->getObjectID() << " -> " <<scriptFileName<<": "<<err_text);
#ifdef _DEBUG
			bp::call_method<void>(this->py_stderr.ptr(),"write",_orkige_debug_msg_stringstream.str());
#endif
		}
		this->resetError();
		return retvalue;
	}



	//--------------------------------------------------------------------------------------------
	OOBJECT_IMPL(PythonScriptComponent)
		GAMEOBJECTCOMPONENT()
		OFUNC(addScript)
	OOBJECT_END
};
#endif ORKIGE_NOSCRIPT