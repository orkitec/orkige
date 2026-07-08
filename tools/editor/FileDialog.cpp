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
