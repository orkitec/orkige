/**************************************************************
	created:	2026/07/24 at 12:00
	filename: 	EditorTabActions.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "EditorTabActions.h"

namespace OrkigeEditor
{
	std::vector<bool> computeTabsToClose(std::size_t count,
		std::size_t targetIndex, TabAction action)
	{
		std::vector<bool> close(count, false);
		if (targetIndex >= count)
		{
			return close;
		}
		switch (action)
		{
		case TabAction::Close:
			close[targetIndex] = true;
			break;
		case TabAction::CloseOthers:
			for (std::size_t i = 0; i < count; ++i)
			{
				close[i] = (i != targetIndex);
			}
			break;
		case TabAction::CloseRight:
			for (std::size_t i = targetIndex + 1; i < count; ++i)
			{
				close[i] = true;
			}
			break;
		case TabAction::CloseAll:
			close.assign(count, true);
			break;
		case TabAction::None:
			break;
		}
		return close;
	}
}
