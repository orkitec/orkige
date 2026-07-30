/**************************************************************
	created:	2026/07/30 at 16:00
	filename: 	HttpAndroid.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_http/HttpAndroid.h"

#include <atomic>

namespace Orkige
{
	namespace
	{
		//! the registered JavaVM*, written once at app boot and read by the
		//! transport's worker threads - atomic so the handover needs no lock
		std::atomic<void *> gJavaVm(NULL);
	}
	//---------------------------------------------------------
	void HttpAndroid::setJavaVM(void * javaVm)
	{
		gJavaVm.store(javaVm, std::memory_order_release);
	}
	//---------------------------------------------------------
	void * HttpAndroid::getJavaVM()
	{
		return gJavaVm.load(std::memory_order_acquire);
	}
}
