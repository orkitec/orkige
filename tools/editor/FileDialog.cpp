/********************************************************************
	created:	Wednesday 2026/07/08 at 12:00
	filename: 	FileDialog.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// FileDialog - native-file-dialog result mailbox (see header).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "FileDialog.h"

namespace Orkige
{
	//---------------------------------------------------------
	void FileDialogResultQueue::deliver(FileDialogResult const& result)
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mPending = result;
		mHasPending = true;
	}
	//---------------------------------------------------------
	bool FileDialogResultQueue::consume(FileDialogResult& outResult)
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mHasPending)
		{
			return false;
		}
		outResult = mPending;
		mPending = FileDialogResult();
		mHasPending = false;
		return true;
	}
}
