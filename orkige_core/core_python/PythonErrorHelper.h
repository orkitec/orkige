/**************************************************************
	created:	2010/09/12 at 0:12
	filename: 	PythonErrorHelper.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __PythonErrorHelper_h__12_9_2010__0_12_35__
#define __PythonErrorHelper_h__12_9_2010__0_12_35__

#ifndef ORKIGE_NOSCRIPT
#include "core_base/Meta.h"
#include <direct.h>

namespace Orkige
{
	using namespace boost::python;

	/*#if defined(_WIN32)
		#ifdef _MSC_VER
			#pragma warning(push)
			#pragma warning(disable:4297)
			#pragma warning(disable:4535)
			extern "C" void straight_to_debugger(unsigned int, EXCEPTION_POINTERS*)
			{
				throw;
			}
			extern "C" void (*old_translator)(unsigned, EXCEPTION_POINTERS*) = _set_se_translator(straight_to_debugger);
			#pragma warning(pop)
		#endif
	#endif // _WIN32
	*/
	
	/* 
	taken from http://lxr.mozilla.org/seamonkey/source/extensions/python/xpcom/src/ErrorUtils.cpp?raw=1
	Obtains a string from a Python traceback.
	This is the exact same string as "traceback.print_exc" would return.

	Pass in a Python traceback object (probably obtained from PyErr_Fetch())
	Result is a string which must be free'd using PyMem_Free()
	*/
	#define TRACEBACK_FETCH_ERROR(what) {errMsg = what; goto done;}

	char *PyTraceback_AsString(PyObject *exc_tb)
	{
		char* lala = new char();

		char *errMsg = NULL; /* holds a local error message */
		char *result = NULL; /* a valid, allocated result. */
		PyObject *modStringIO = NULL;
		PyObject *modTB = NULL;
		PyObject *obFuncStringIO = NULL;
		PyObject *obStringIO = NULL;
		PyObject *obFuncTB = NULL;
		PyObject *argsTB = NULL;
		PyObject *obResult = NULL;

		/* Import the modules we need - cStringIO and traceback */
		modStringIO = PyImport_ImportModule("cStringIO");
		if (modStringIO==NULL)
			TRACEBACK_FETCH_ERROR("cant import cStringIO\n");

		modTB = PyImport_ImportModule("traceback");
		if (modTB==NULL)
			TRACEBACK_FETCH_ERROR("cant import traceback\n");
		/* Construct a cStringIO object */
		obFuncStringIO = PyObject_GetAttrString(modStringIO, "StringIO");
		if (obFuncStringIO==NULL)
			TRACEBACK_FETCH_ERROR("cant find cStringIO.StringIO\n");
		obStringIO = PyObject_CallObject(obFuncStringIO, NULL);
		if (obStringIO==NULL)
			TRACEBACK_FETCH_ERROR("cStringIO.StringIO() failed\n");
		/* Get the traceback.print_exception function, and call it. */
		obFuncTB = PyObject_GetAttrString(modTB, "print_tb");
		if (obFuncTB==NULL)
			TRACEBACK_FETCH_ERROR("cant find traceback.print_tb\n");

		argsTB = Py_BuildValue("OOO", 
			exc_tb  ? exc_tb  : Py_None,
			Py_None, 
			obStringIO);
		if (argsTB==NULL) 
			TRACEBACK_FETCH_ERROR("cant make print_tb arguments\n");

		obResult = PyObject_CallObject(obFuncTB, argsTB);
		if (obResult==NULL) 
			TRACEBACK_FETCH_ERROR("traceback.print_tb() failed\n");
		/* Now call the getvalue() method in the StringIO instance */
		Py_DECREF(obFuncStringIO);
		obFuncStringIO = PyObject_GetAttrString(obStringIO, "getvalue");
		if (obFuncStringIO==NULL)
			TRACEBACK_FETCH_ERROR("cant find getvalue function\n");
		Py_DECREF(obResult);
		obResult = PyObject_CallObject(obFuncStringIO, NULL);
		if (obResult==NULL) 
			TRACEBACK_FETCH_ERROR("getvalue() failed.\n");

		/* And it should be a string all ready to go - duplicate it. */
		if (!PyString_Check(obResult))
			TRACEBACK_FETCH_ERROR("getvalue() did not return a string\n");

		{ // a temp scope so I can use temp locals.
			char *tempResult = PyString_AsString(obResult);
			result = (char *)PyMem_Malloc(strlen(tempResult)+1);
			if (result==NULL)
				TRACEBACK_FETCH_ERROR("memory error duplicating the traceback string\n");

			strcpy(result, tempResult);
		} // end of temp scope.
done:
		/* All finished - first see if we encountered an error */
		if (result==NULL && errMsg != NULL) {
			result = (char *)PyMem_Malloc(strlen(errMsg)+1);
			if (result != NULL)
				/* if it does, not much we can do! */
				strcpy(result, errMsg);
		}
		Py_XDECREF(modStringIO);
		Py_XDECREF(modTB);
		Py_XDECREF(obFuncStringIO);
		Py_XDECREF(obStringIO);
		Py_XDECREF(obFuncTB);
		Py_XDECREF(argsTB);
		Py_XDECREF(obResult);
		return result;
	}

	//convert python exception
	std::string getPythonErrorString(bool convertToTraceable = true,bool showOriginalException=false,std::string prefix="") 
	{
		// Extra paranoia...
		if (!PyErr_Occurred()) 
		{
			return "No Python error";
		}

		PyObject *type, *value, *traceback;
		PyErr_Fetch(&type, &value, &traceback);
		PyErr_Clear();
		
		std::string message = "";
		if (traceback)
		{
			//convert python traceback
			//not very nice but works
			char* ctb = PyTraceback_AsString(traceback);
			message += ctb;
			PyMem_Free(ctb);

			if(convertToTraceable)
			{
				
				
				std::string tbline1=message.substr(0,message.find('\n'));
				std::string::size_type pos1 = tbline1.find('"');
				std::string::size_type pos2 = tbline1.find('"',pos1);
				std::string filename = tbline1.substr((pos1+1),(pos2+1));

				pos1 = tbline1.find(',');
				std::string lineno = tbline1.substr((pos1+7));
				pos2 = lineno.find(',');

				std::string traceable="";
			
				traceable += prefix;
				traceable += filename;
				traceable += " (";
				traceable += lineno.substr(0,pos2);
				traceable += ")";


				if(!showOriginalException && traceable!="")
				{
					char currentpath[256];
					getcwd( currentpath, 256);
					message = "PythonError:\n";
					message += currentpath;
					message += "\\";
				}	
				
				message += traceable;
			}
		}
		if (type) 
		{
			if(message == "")
				message += "PythonError";
			type = PyObject_Str(type);
			message += ": ";
			message += PyString_AsString(type);
		}
		if (value) 
		{
			value = PyObject_Str(value);
			message += ": ";
			message += PyString_AsString(value);
			//message += "\n";
		}

		Py_XDECREF(type);
		Py_XDECREF(value);
		Py_XDECREF(traceback);

		return message;
	}
}
#endif //ORKIGE_NOSCRIPT
#endif //__PythonErrorHelper_h__12_9_2010__0_12_35__
