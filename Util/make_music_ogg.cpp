/********************************************************************
	filename: 	make_music_ogg.cpp
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	Mint the committed OGG Vorbis test fixtures the music-stream
	tests decode (tests/assets/blip.ogg and the loopable
	samples/hello_orkige/media/music_loop.ogg). A DEV-ONLY generator: it
	synthesizes a short sine tone and encodes it with a Vorbis encoder, so
	it is NOT part of the engine build (the engine ships only stb_vorbis, a
	decoder). The .ogg outputs are committed binary fixtures; this file
	documents how they were produced and lets anyone with a Vorbis encoder
	regenerate them bit-for-comparable.

	Build (one-off, against a locally installed libvorbis):
	  c++ -std=c++17 Util/make_music_ogg.cpp -o /tmp/make_music_ogg \
	      -I<vorbis-include> -L<vorbis-lib> -lvorbisenc -lvorbis -logg
	Run:
	  /tmp/make_music_ogg <out.ogg> <seconds> <channels> <hz>
*********************************************************************/
#include <vorbis/vorbisenc.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	// synthesize a fading sine tone and encode it to an Ogg Vorbis file;
	// deterministic so the committed fixture is reproducible
	int encodeTone(std::string const & path, float seconds, int channels,
		float frequency)
	{
		const int sampleRate = 44100;
		const int totalFrames = static_cast<int>(seconds * sampleRate);

		vorbis_info info;
		vorbis_info_init(&info);
		// VBR quality 0.1: tiny files, ample for a test tone
		if (vorbis_encode_init_vbr(&info, channels, sampleRate, 0.1f) != 0)
		{
			std::fprintf(stderr, "vorbis_encode_init_vbr failed\n");
			return 1;
		}

		vorbis_comment comment;
		vorbis_comment_init(&comment);
		vorbis_comment_add_tag(&comment, "ENCODER", "orkige make_music_ogg");

		vorbis_dsp_state dsp;
		vorbis_analysis_init(&dsp, &info);
		vorbis_block block;
		vorbis_block_init(&dsp, &block);

		ogg_stream_state stream;
		ogg_stream_init(&stream, 0xC0FFEE);

		std::FILE * out = std::fopen(path.c_str(), "wb");
		if (out == nullptr)
		{
			std::fprintf(stderr, "cannot open %s\n", path.c_str());
			return 1;
		}

		// header pages (identification, comment, codebooks)
		{
			ogg_packet header;
			ogg_packet headerComment;
			ogg_packet headerCode;
			vorbis_analysis_headerout(&dsp, &comment, &header, &headerComment,
				&headerCode);
			ogg_stream_packetin(&stream, &header);
			ogg_stream_packetin(&stream, &headerComment);
			ogg_stream_packetin(&stream, &headerCode);
			ogg_page page;
			while (ogg_stream_flush(&stream, &page) != 0)
			{
				std::fwrite(page.header, 1, page.header_len, out);
				std::fwrite(page.body, 1, page.body_len, out);
			}
		}

		int written = 0;
		const int chunk = 1024;
		bool done = false;
		while (!done)
		{
			const int frames = (written + chunk <= totalFrames)
				? chunk : (totalFrames - written);
			if (frames <= 0)
			{
				// signal end of stream
				vorbis_analysis_wrote(&dsp, 0);
			}
			else
			{
				float ** buffer = vorbis_analysis_buffer(&dsp, frames);
				for (int i = 0; i < frames; ++i)
				{
					const int frame = written + i;
					const float t = static_cast<float>(frame) / sampleRate;
					// gentle fade at both ends so a loop point has no click
					const float fade = std::sin(3.14159265f *
						static_cast<float>(frame) / totalFrames);
					const float value = 0.6f * fade *
						std::sin(2.0f * 3.14159265f * frequency * t);
					for (int c = 0; c < channels; ++c)
					{
						buffer[c][i] = value;
					}
				}
				vorbis_analysis_wrote(&dsp, frames);
				written += frames;
			}

			while (vorbis_analysis_blockout(&dsp, &block) == 1)
			{
				vorbis_analysis(&block, nullptr);
				vorbis_bitrate_addblock(&block);
				ogg_packet packet;
				while (vorbis_bitrate_flushpacket(&dsp, &packet) != 0)
				{
					ogg_stream_packetin(&stream, &packet);
					ogg_page page;
					while (ogg_stream_pageout(&stream, &page) != 0)
					{
						std::fwrite(page.header, 1, page.header_len, out);
						std::fwrite(page.body, 1, page.body_len, out);
						if (ogg_page_eos(&page) != 0)
						{
							done = true;
						}
					}
				}
			}
			if (frames <= 0 && !done)
			{
				// flush any remaining page at end of stream
				ogg_page page;
				while (ogg_stream_flush(&stream, &page) != 0)
				{
					std::fwrite(page.header, 1, page.header_len, out);
					std::fwrite(page.body, 1, page.body_len, out);
				}
				done = true;
			}
		}

		std::fclose(out);
		ogg_stream_clear(&stream);
		vorbis_block_clear(&block);
		vorbis_dsp_clear(&dsp);
		vorbis_comment_clear(&comment);
		vorbis_info_clear(&info);
		std::printf("wrote %s (%.2fs, %d ch, %.0f Hz)\n", path.c_str(),
			seconds, channels, frequency);
		return 0;
	}
}

int main(int argc, char ** argv)
{
	if (argc < 5)
	{
		std::fprintf(stderr,
			"usage: %s <out.ogg> <seconds> <channels> <hz>\n", argv[0]);
		return 2;
	}
	return encodeTone(argv[1], std::atof(argv[2]),
		std::atoi(argv[3]), std::atof(argv[4]));
}
