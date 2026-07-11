/********************************************************************
	created:	Saturday 2026/07/11 at 22:10
	filename: 	ScreenStack.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "core_util/ScreenStack.h"

#include <algorithm>

namespace Orkige
{
	//---------------------------------------------------------
	void ScreenStack::push(String const & name)
	{
		this->entries.push_back(name);
	}
	//---------------------------------------------------------
	void ScreenStack::replace(String const & name)
	{
		if(this->entries.empty())
		{
			this->entries.push_back(name);	// nothing to replace - a plain push
			return;
		}
		this->entries.back() = name;
	}
	//---------------------------------------------------------
	String ScreenStack::pop()
	{
		if(this->entries.empty())
		{
			return "";
		}
		const String top = this->entries.back();
		this->entries.pop_back();
		return top;
	}
	//---------------------------------------------------------
	String ScreenStack::current() const
	{
		if(this->entries.empty())
		{
			return "";
		}
		return this->entries.back();
	}
	//---------------------------------------------------------
	bool ScreenStack::contains(String const & name) const
	{
		return std::find(this->entries.begin(), this->entries.end(), name) !=
			this->entries.end();
	}
	//---------------------------------------------------------
}
