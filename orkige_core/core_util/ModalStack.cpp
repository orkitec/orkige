/********************************************************************
	created:	Saturday 2026/07/11 at 18:00
	filename: 	ModalStack.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the pure modal-stack + z allocation (@see ModalStack.h)
*********************************************************************/

#include "core_util/ModalStack.h"

namespace Orkige
{
	//---------------------------------------------------------
	ModalStack::Entry ModalStack::push(String const & id)
	{
		Entry entry;
		entry.id = id;
		// the new modal climbs one zStep above whatever is on top; the scrim
		// sits on its base layer, the dialog widgets one layer higher so they
		// dispatch before (and draw over) their own scrim
		entry.scrimZ = this->baseZ +
			static_cast<unsigned int>(this->entries.size()) * this->zStep;
		entry.contentZ = entry.scrimZ + 1;
		this->entries.push_back(entry);
		return entry;
	}
	//---------------------------------------------------------
	String ModalStack::popTop()
	{
		if(this->entries.empty())
		{
			return String();
		}
		String id = this->entries.back().id;
		this->entries.pop_back();
		return id;
	}
	//---------------------------------------------------------
	bool ModalStack::remove(String const & id)
	{
		for(std::size_t i = 0; i < this->entries.size(); ++i)
		{
			if(this->entries[i].id == id)
			{
				this->entries.erase(this->entries.begin() + i);
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	String ModalStack::topId() const
	{
		if(this->entries.empty())
		{
			return String();
		}
		return this->entries.back().id;
	}
}
