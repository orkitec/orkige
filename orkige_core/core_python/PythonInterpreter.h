/**************************************************************
	created:	2010/09/12 at 0:02
	filename: 	PythonInterpreter.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __PythonInterpreter_h__12_9_2010__0_02_42__
#define __PythonInterpreter_h__12_9_2010__0_02_42__

#ifndef ORKIGE_NOSCRIPT
#include "core_base/Meta.h"
#include "core_util/Singleton.h"
#include "core_util/String.h"
namespace Orkige
{
	class ORKIGE_CORE_DLL PythonInterpreter : public Singleton<PythonInterpreter>
	{
		DECL_OSINGLETON(PythonInterpreter)
	// Attributes --------------------------------------------------------------
	public:
	protected:
	private:
		boost::python::api::object m_main_namespace;
	// Methods -----------------------------------------------------------------
	public:

		PythonInterpreter();
		PythonInterpreter(int argc, char* argv[]);
		~PythonInterpreter();
		
		void exec(String const & code, boost::python::api::object& globals, boost::python::api::object& locals);

		void exec(String const &  code, boost::python::api::object& globals);

		void exec(String const &  code);

		void execfile(String const &  filename, boost::python::api::object& globals, boost::python::api::object& locals);

		void execfile(String const &  filename, boost::python::api::object& globals);

		void execfile(String const &  filename);

		boost::python::api::object eval(String const &  code, boost::python::api::object& globals, boost::python::api::object& locals);

		boost::python::api::object eval(String const &  code, boost::python::api::object& globals);

		boost::python::api::object eval(String const &  code);

		boost::python::api::object get(String const &  code);

		template<typename ReturnValueType>
		ReturnValueType eval(String const &  code) 
		{ 
			return boost::python::extract<ReturnValueType>(this->eval(code));
		}

		template<typename ReturnValueType>
		ReturnValueType get(String const &  code) 
		{ 
			return boost::python::extract<ReturnValueType>(this->get(code));
		}
	protected:
	private:
		boost::python::api::object init(int argc, char* argv[]);

		boost::python::api::object& main_namespace();

		void handlePythonException();
	};
	//template<> PythonInterpreter* Singleton<PythonInterpreter>::singleton = 0;
};
/*template<> Orkige::PythonInterpreter* Ogre::Singleton<Orkige::PythonInterpreter>::ms_Singleton = 0;*/
#endif //ORKIGE_NOSCRIPT
#endif //__PythonInterpreter_h__12_9_2010__0_02_42__
