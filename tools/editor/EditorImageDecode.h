/********************************************************************
	created:	Monday 2026/07/14 at 15:00
	filename: 	EditorImageDecode.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorImageDecode_h__14_7_2026__15_00_00__
#define __EditorImageDecode_h__14_7_2026__15_00_00__

//! @file EditorImageDecode.h
//! @brief a tiny CPU image decoder for the editor, confined to one TU (the
//! StbVorbisImpl / FontBakeImpl precedent keeps the single-file stb library
//! out of every other translation unit and the precompiled header).

#include <string>
#include <vector>

namespace OrkigeEditor
{
	//! @brief decode an on-disk image file (PNG etc.) into tightly-packed RGBA
	//! rows (width*4 bytes, straight alpha) - the exact layout
	//! RenderSystem::createTexture2D consumes. @return false (outRgba cleared)
	//! on a read/decode failure.
	bool decodeImageRgba(std::string const& path,
		std::vector<unsigned char>& outRgba, int& outWidth, int& outHeight);
}

#endif //__EditorImageDecode_h__14_7_2026__15_00_00__
