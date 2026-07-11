/********************************************************************
	created:	Saturday 2026/07/11 at 18:00
	filename: 	ModalStack.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __ModalStack_h__11_7_2026__18_00_00__
#define __ModalStack_h__11_7_2026__18_00_00__

//! @file ModalStack.h
//! @brief backend-neutral modal-dialog stack + z-layer allocation. A modal is
//! a consuming scrim plus the dialog widgets that sit one layer above it; each
//! newly pushed modal is raised above the previous one so the top modal is
//! dispatched first (the gui dispatch loop is highest-z first) and wins input.
//! No renderer, no widget - the gui GuiManager keeps one of these and a unit
//! test shares the z arithmetic.

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <vector>

namespace Orkige
{
	//! @brief the stack of active modals. Each entry owns two z layers: the
	//! scrim layer (the consuming full-window backdrop) and the content layer
	//! one above it (the dialog's own widgets). Layers climb by `zStep` per
	//! stacked modal so a later modal covers an earlier one.
	struct ORKIGE_CORE_DLL ModalStack
	{
		//! @brief one active modal's identity + its two allocated z layers
		struct Entry
		{
			String			id;			//!< caller-chosen modal id
			unsigned int	scrimZ = 0;	//!< the consuming backdrop layer
			unsigned int	contentZ = 0;	//!< the dialog widgets' layer (scrimZ+1)
		};

		std::vector<Entry>	entries;			//!< bottom-to-top order
		unsigned int		baseZ = 1000;		//!< first modal's scrim layer
		unsigned int		zStep = 100;		//!< layer climb per stacked modal

		//! @brief push a new modal above all existing ones and allocate its two
		//! layers. @return the pushed entry (scrimZ / contentZ to author into).
		Entry push(String const & id);
		//! @brief pop the topmost modal. @return its id, or "" when empty.
		String popTop();
		//! @brief remove a modal by id wherever it sits (a dialog dismissed out
		//! of order). @return true when it was present.
		bool remove(String const & id);
		//! @brief the topmost modal's id, or "" when empty
		String topId() const;
		bool empty() const { return this->entries.empty(); }
		std::size_t size() const { return this->entries.size(); }
	};
}

#endif //__ModalStack_h__11_7_2026__18_00_00__
