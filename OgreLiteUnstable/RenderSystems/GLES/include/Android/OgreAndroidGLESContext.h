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

#ifndef __AndroidGLESContext_H__
#define __AndroidGLESContext_H__

#include "OgreGLESContext.h"

namespace Ogre {
    class AndroidGLESSupport;
	
	/// Implements the callback mechanisms for external context calls
	class _OgreExport AndroidGLESContextDelegate
	{
	public:
		virtual void setCurrent(int) = 0; // handle of context
		virtual void endCurrent(int) = 0; // handle of context
	};

    class _OgrePrivate AndroidGLESContext : public GLESContext
    {
		private:
			const AndroidGLESSupport *mGLSupport;
			int mHandle; // Handle to external implementation
			AndroidGLESContextDelegate *mDelegate;
        public:
            AndroidGLESContext(const AndroidGLESSupport *glsupport, int handle);
            virtual ~AndroidGLESContext();

			virtual void setCurrent();
            virtual void endCurrent();
            GLESContext* clone() const;
			
			void setDelegate(AndroidGLESContextDelegate *delegate);
			AndroidGLESContextDelegate *getDelegate();
			int getHandle() const;
    };
}

#endif
