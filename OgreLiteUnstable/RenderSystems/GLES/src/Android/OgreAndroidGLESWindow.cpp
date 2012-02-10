/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2009 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "OgreRoot.h"
#include "OgreException.h"
#include "OgreLogManager.h"
#include "OgreStringConverter.h"
#include "OgreWindowEventUtilities.h"

#include "OgreGLESPrerequisites.h"
#include "OgreGLESRenderSystem.h"

#include "OgreAndroidGLESSupport.h"
#include "OgreAndroidGLESWindow.h"
#include "OgreAndroidGLESContext.h"

#include <iostream>
#include <algorithm>
#include <climits>

namespace Ogre {
	AndroidGLESWindow::AndroidGLESWindow(AndroidGLESSupport *glsupport)
		: mGLSupport(glsupport), mClosed(false), mContext(0), mHandle(0), mDelegate(0)
	{
	}

	AndroidGLESWindow::~AndroidGLESWindow()
	{
		if(mContext)
			delete mContext;
	}

	void AndroidGLESWindow::getCustomAttribute( const String& name, void* pData )
	{
		if(name == "HANDLE")
		{
			*(int*)pData = mHandle;
			return;
		}
		else if(name == "GLCONTEXT")
		{
			*static_cast<AndroidGLESContext**>(pData) = mContext;
            return;
		}
	}

	AndroidGLESContext * AndroidGLESWindow::createGLContext(int handle) const
	{
		return new AndroidGLESContext(mGLSupport, handle);
	}

	void AndroidGLESWindow::getLeftAndTopFromNativeWindow( int & left, int & top, uint width, uint height )
	{
		// We don't have a native window.... but I think all android windows are origined
		left = top = 0;
	}

	void AndroidGLESWindow::initNativeCreatedWindow(const NameValuePairList *miscParams)
	{
		LogManager::getSingleton().logMessage("\tinitNativeCreatedWindow called");
		if (miscParams)
		{
			NameValuePairList::const_iterator opt;
			NameValuePairList::const_iterator end = miscParams->end();

			opt = miscParams->find("externalWindowHandle");
			if(opt != end)
			{
				mHandle = Ogre::StringConverter::parseInt(opt->second);
			}
			
			int ctxHandle = -1;
			opt = miscParams->find("externalGLContext");
			if(opt != end)
			{
				ctxHandle = Ogre::StringConverter::parseInt(opt->second);
			}
			
			mHandle = 0;
			ctxHandle = 0;
			
			if(ctxHandle != -1)
			{
				mContext = new AndroidGLESContext(mGLSupport, ctxHandle);
			}
			else
			{
				OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,
					"externalGLContext parameter required for Android windows.",
					"AndroidGLESWindow::initNativeCreatedWindow" );
			}
		}
	}

	void AndroidGLESWindow::createNativeWindow( int &left, int &top, uint &width, uint &height, String &title )
	{
		LogManager::getSingleton().logMessage("\tcreateNativeWindow called");
        mLeft = left;
        mTop = top;
        mWidth = width;
        mHeight = height;
	}

	void AndroidGLESWindow::reposition( int left, int top )
	{
		LogManager::getSingleton().logMessage("\treposition called");
        mLeft = left;
        mTop = top;
	}

	void AndroidGLESWindow::resize(uint width, uint height)
	{
		LogManager::getSingleton().logMessage("\tresize called");
        mWidth = width;
        mHeight = height;
	}

	void AndroidGLESWindow::windowMovedOrResized()
	{
		LogManager::getSingleton().logMessage("\twindowMovedOrResized called");
	}
	
	void AndroidGLESWindow::copyContentsToMemory(const PixelBox &dst, FrameBuffer buffer)
	{
	
	}
		
	bool AndroidGLESWindow::requiresTextureFlipping() const
	{
		return false;
	}
		
	void AndroidGLESWindow::destroy(void)
	{
		LogManager::getSingleton().logMessage("\tdestroy called");
	}
		
	bool AndroidGLESWindow::isClosed(void) const
	{
		return mClosed;
	}

    void AndroidGLESWindow::create(const String& name, uint width, uint height,
                           bool fullScreen, const NameValuePairList *miscParams)
    {
        LogManager::getSingleton().logMessage("\tcreate called");
		
		initNativeCreatedWindow(miscParams);
		
		mName = name;
        mWidth = width;
        mHeight = height;
        mLeft = 0;
        mTop = 0;
        mActive = true;
		//mVisible = true;

        mClosed = false;
	}



}
