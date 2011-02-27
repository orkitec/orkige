/**************************************************************
	created:	2010/09/12 at 0:03
	filename: 	PythonInterpreter.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef ORKIGE_NOSCRIPT
#include "core_python/PythonInterpreter.h"
#include "core_python/PythonErrorHelper.h"

namespace Orkige 
{

	IMPL_OSINGLETON(PythonInterpreter)

	using namespace boost::python;
	object PythonInterpreter::init(int argc, char* argv[])
	{
		Py_Initialize();
		PySys_SetPath(".");

		if (argc > 0)
		{
			PySys_SetArgv(argc, argv);
		}
		else
		{
			char* no_args[] = { "", 0 };
			PySys_SetArgv(1, no_args);
		}
		/*handle<> main_module(borrowed( PyImport_AddModule("__main__") ));
		handle<> main_namespace(borrowed( PyModule_GetDict(main_module.get()) ));*/

		object main_module((handle<>(borrowed(PyImport_AddModule("__main__")))));

		object main_namespace = main_module.attr("__dict__");

		return object(main_namespace);
	}

	PythonInterpreter::PythonInterpreter()
		: m_main_namespace(init(0, 0))
	{
	}

	PythonInterpreter::PythonInterpreter(int argc, char* argv[])
		: m_main_namespace(init(argc, argv))
	{
	}

	PythonInterpreter::~PythonInterpreter()
	{
		//this is known to be somewhat buggy in boost
		//Py_Finalize();
	}

	object& PythonInterpreter::main_namespace() 
	{ 
		return m_main_namespace;
	}

	void PythonInterpreter::exec(String const &  code, object& globals)
	{
		exec(code, globals, globals);
	}

	void PythonInterpreter::exec(String const &  code)
	{
		exec(code, main_namespace());
	}

	object PythonInterpreter::get(String const &  code)
	{
		return m_main_namespace[code.c_str()];
	}

	void PythonInterpreter::execfile(String const &  filename, object& globals) 
	{ 
		execfile(filename, globals, globals); 
	}

	void PythonInterpreter::execfile(String const &  filename)
	{
		execfile(filename, main_namespace());
	}

	object PythonInterpreter::eval(String const &  code, object& globals) 
	{ 
		return eval(code, globals, globals); 
	}

	object PythonInterpreter::eval(String const &  code) 
	{ 
		return eval(code, main_namespace()); 
	}

	void PythonInterpreter::exec(String const &  code, object& globals, object& locals)
	{
		try
		{
			handle<> result( 
				PyRun_String(code.c_str(), Py_file_input, globals.ptr(), locals.ptr()) );
		}
		catch(error_already_set)
		{
			this->handlePythonException();
		}
	}

	void PythonInterpreter::execfile(String const &  filename, object& globals, object& locals)
	{
		try
		{
			FILE* file = 0;

			handle<> py_file( PyFile_FromString(const_cast<char*>(filename.c_str()), "r") );
			if (py_file)
				file = PyFile_AsFile(py_file.get());
			oAssert(file);

			handle<> ignored(PyRun_File(file, filename.c_str(), Py_file_input
				, m_main_namespace.ptr()
				, m_main_namespace.ptr()));
		}
		catch(error_already_set)
		{
			this->handlePythonException();
		}
	}

	object PythonInterpreter::eval(String const &  code, object& globals, object& locals)
	{
		try
		{
			handle<> result(
				PyRun_String(code.c_str(), Py_eval_input, globals.ptr(), locals.ptr()) );

			return object(result);
		}
		catch(error_already_set)
		{
			this->handlePythonException();
		}
		return boost::python::object();
	}
	// protected: --------------------------------------------------------------

	// private: ----------------------------------------------------------------
	void PythonInterpreter::handlePythonException()
	{
		std::string error = "PythonError\n";

		try
		{
			error += getPythonErrorString(false);
			oDebugError ("scriptengine",0,error);
		}
		catch(...)
		{
			PyErr_Print();

			oDebugMsg("scriptengine",0,"Unknown Python Error! see console Output!");
			oAssert(!"Unknown Python Error! see console Output!");
		}
		
	}
}
#endif ORKIGE_NOSCRIPT