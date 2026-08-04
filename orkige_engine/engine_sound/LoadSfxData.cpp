/********************************************************************
	created:	Wednesday 2026/07/29 at 11:00
	filename: 	LoadSfxData.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file LoadSfxData.cpp
//! @brief the procedural-sound "decoder": a sound PARAMETER file synthesized
//! into the same PCM block a wave file would have decoded to
//! @remarks This sits exactly where loadWavData / loadCafData sit
//! (SoundUtil::loadSoundData dispatches on the extension), so a parameter file
//! IS a sound file to everything above: SoundSource hands the samples to a
//! mixer voice, keeps its own copy for a rebuild after an audio interruption,
//! and every consumer - SoundComponent::addSound, the Lua sound surface, the
//! mixer groups, per-play pitch/volume variation, positional audio - works
//! verbatim with no new API.
//!
//! BOTH carriers of the one parameter model land here (@see
//! core_util/SfxAsset.h): the standard binary `.sfs` a designer saves out of an
//! authoring tool, and the `.osfx` text twin an agent writes. The bytes come
//! through the backend-neutral resource read, so either resolves out of a
//! mounted pak/APK like any other resource.

#include "engine_sound/SoundData.h"

#include <core_util/SfxAsset.h>
#include <core_util/SfxSynth.h>
#include <engine_render/RenderSystem.h>

#include <cstdlib>
#include <cstring>
#include <vector>

namespace Orkige
{
	namespace SoundUtil
	{
		void* loadSfxData(Orkige::String const & fileName, int * dataSize,
			PcmFormat * format)
		{
			RenderSystem* render = RenderSystem::get();
			// the resource read hands back the file's bytes verbatim (no
			// newline or encoding translation), which is what lets the SAME
			// call serve the text twin and the binary parameter file
			String bytes;
			if(!render || !render->readResourceText(fileName, bytes))
			{
				// the honest miss loadSoundData's callers already handle: a
				// NULL turns into the established SoundError naming the file
				oDebugError("sound", 0, "procedural sound '" << fileName
					<< "' not found in any resource group");
				return NULL;
			}

			SfxDesc desc;
			String parseError;
			const bool parsed = SfxAsset::isBinaryName(fileName)
				? SfxAsset::parseBinary(bytes.data(), bytes.size(), desc,
					&parseError)
				: SfxAsset::parse(bytes, desc, &parseError);
			if(!parsed)
			{
				oDebugError("sound", 0, "procedural sound '" << fileName
					<< "' failed to parse (" << parseError << ")");
				return NULL;
			}

			// out-of-range numbers are CORRECTED, and every correction is
			// named (the parser owns malformation, the synthesizer owns range -
			// @see core_util/SfxAsset.h)
			std::vector<String> notes;
			if(!SfxSynth::sanitize(desc, &notes))
			{
				for(std::size_t i = 0; i < notes.size(); ++i)
				{
					oDebugWarn("sound", 0, "procedural sound '" << fileName
						<< "': " << notes[i]);
				}
			}

			const SfxSynth::Pcm pcm = SfxSynth::render(desc);
			if(pcm.samples.empty())
			{
				oDebugError("sound", 0, "procedural sound '" << fileName
					<< "' rendered no samples (its envelope is empty)");
				return NULL;
			}

			format->channels = 1;				// mono, so a source is 3D
			format->bitsPerSample = 16;
			format->sampleRate = static_cast<int>(pcm.sampleRate);
			*dataSize = static_cast<int>(pcm.byteSize());

			// malloc + copy like the file loaders: SoundSource owns the block
			// and free()s it (the voice keeps its own copy of the samples)
			void* data = malloc(pcm.byteSize());
			if(!data)
			{
				return NULL;
			}
			memcpy(data, pcm.samples.data(), pcm.byteSize());

			oDebugMsg("sound", 0, "synthesized '" << fileName << "' ("
				<< pcm.durationSec() << "s, " << pcm.sampleRate
				<< " Hz mono 16 bit, " << pcm.samples.size() << " samples)");
			return data;
		}
	}
	//---------------------------------------------------------
}
