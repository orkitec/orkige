/********************************************************************
	created:	Monday 2010/09/06 at 13:43
	filename: 	LoadWavData.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_sound/SoundPlatform.h"
#include "engine_sound/SoundError.h"

namespace Orkige
{
	namespace SoundUtil
	{	/// Returns the next 16 bytes from a buffer
		inline unsigned short readSoundByte16(const unsigned char buffer[2])
		{
#if(OGRE_ENDIAN == OGRE_ENDIAN_BIG)
			return (buffer[0] << 8) + buffer[1];
#else
			return (buffer[1] << 8) + buffer[0];
#endif
		}
		//----------------------------------------------------
		/// Returns the next 32 bytes from a buffer
		inline unsigned long readSoundByte32(const unsigned char buffer[4])
		{
#if(OGRE_ENDIAN == OGRE_ENDIAN_BIG)
			return (buffer[0] << 24) + (buffer[1] << 16) + (buffer[2] << 8) + buffer[3];
#else
			return (buffer[3] << 24) + (buffer[2] << 16) + (buffer[1] << 8) + buffer[0];
#endif
		}
		//----------------------------------------------------
		inline std::vector<char> getSoundData(std::size_t size, int bufferSize, Ogre::DataStreamPtr soundData)
		{
			size_t bytes;
			std::vector<char> data;
			char *array = new char[bufferSize];

			while(data.size() != size)
			{
				// Read up to a buffer's worth of decoded sound data
				bytes = soundData->read(array, bufferSize);

				if (bytes <= 0)
					break;

				if (data.size() + bytes > size)
					bytes = size - data.size();

				// Append to end of buffer
				data.insert(data.end(), array, array + bytes);
			}

			delete []array;
			array = NULL;

			return data;
		}
		//----------------------------------------------------
		inline void prepareBuffer(ALsizei channels, ALsizei bits, ALsizei freq, int* bufferSize, ALenum* format)
		{
			switch(channels)
			{
			case 1:
				if(bits == 8)
				{
					*format = AL_FORMAT_MONO8;
					// Set BufferSize to 250ms (Frequency divided by 4 (quarter of a second))
					*bufferSize = freq / 4;
				}
				else
				{
					*format = AL_FORMAT_MONO16;
					// Set BufferSize to 250ms (Frequency * 2 (16bit) divided by 4 (quarter of a second))
					*bufferSize = freq >> 1;
					// IMPORTANT : The Buffer Size must be an exact multiple of the BlockAlignment ...
					*bufferSize -= (*bufferSize % 2);
				}
				break;
			case 2:
				if(bits == 8)
				{
					*format = AL_FORMAT_STEREO16;
					// Set BufferSize to 250ms (Frequency * 2 (8bit stereo) divided by 4 (quarter of a second))
					*bufferSize = freq >> 1;
					// IMPORTANT : The Buffer Size must be an exact multiple of the BlockAlignment ...
					*bufferSize -= (*bufferSize % 2);
				}
				else
				{
					*format = AL_FORMAT_STEREO16;
					// Set BufferSize to 250ms (Frequency * 4 (16bit stereo) divided by 4 (quarter of a second))
					*bufferSize = freq;
					// IMPORTANT : The Buffer Size must be an exact multiple of the BlockAlignment ...
					*bufferSize-= (*bufferSize % 4);
				}
				break;
			case 4:
				*format = alGetEnumValue("AL_FORMAT_QUAD16");
				// Set BufferSize to 250ms (Frequency * 8 (16bit 4-channel) divided by 4 (quarter of a second))
				*bufferSize = freq * 2;
				// IMPORTANT : The Buffer Size must be an exact multiple of the BlockAlignment ...
				*bufferSize -= (*bufferSize % 8);
				break;
			case 6:
				*format = alGetEnumValue("AL_FORMAT_51CHN16");
				// Set BufferSize to 250ms (Frequency * 12 (16bit 6-channel) divided by 4 (quarter of a second))
				*bufferSize = freq * 3;
				// IMPORTANT : The Buffer Size must be an exact multiple of the BlockAlignment ...
				*bufferSize -= (*bufferSize % 12);
				break;
			case 7:
				*format = alGetEnumValue("AL_FORMAT_61CHN16");
				// Set BufferSize to 250ms (Frequency * 16 (16bit 7-channel) divided by 4 (quarter of a second))
				*bufferSize = freq  * 4;
				// IMPORTANT : The Buffer Size must be an exact multiple of the BlockAlignment ...
				*bufferSize -= (*bufferSize % 16);
				break;
			case 8:
				*format = alGetEnumValue("AL_FORMAT_71CHN16");
				// Set BufferSize to 250ms (Frequency * 20 (16bit 8-channel) divided by 4 (quarter of a second))
				*bufferSize = freq * 5;
				// IMPORTANT : The Buffer Size must be an exact multiple of the BlockAlignment ...
				*bufferSize -= (*bufferSize % 20);
				break;
			default:
				// Couldn't determine buffer format so log the error and default to mono
				oDebugMsg("sound",0,"Could not determine buffer format!  Setting to MONO");

				*format = AL_FORMAT_MONO16;
				// Set BufferSize to 250ms (Frequency * 2 (16bit) divided by 4 (quarter of a second))
				*bufferSize = freq  >> 1;
				// IMPORTANT : The Buffer Size must be an exact multiple of the BlockAlignment ...
				*bufferSize -= (*bufferSize % 2);
				break;
			}
		}
		//----------------------------------------------------
		void* loadWavData(Orkige::String const & fileName, ALsizei *dataSize, ALenum *dataFormat, ALsizei* sampleRate)
		{
			ALsizei bits;
			ALsizei channels;
			int bufferSize;
			ALsizei size;

			Ogre::ResourceGroupManager *groupManager = Ogre::ResourceGroupManager::getSingletonPtr();
			Ogre::String group = groupManager->findGroupContainingResource(fileName);
			Ogre::DataStreamPtr soundData = groupManager->openResource(fileName, group);

			// buffers
			char magic[5];
			magic[4] = '\0';
			unsigned char buffer32[4];
			unsigned char buffer16[2];

			// check magic
			SoundError::call(soundData->read(magic, 4) == 4, "Cannot read wav file " + fileName);
			SoundError::call(String(magic) == "RIFF", "Wrong wav file format. (no RIFF magic): " + fileName);

			// The next 4 bytes are the file size, we can skip this since we get the size from the DataStream
			soundData->skip(4);
			size = static_cast<ALsizei>(soundData->size());

			// check file format
			SoundError::call(soundData->read(magic, 4) == 4, "Cannot read wav file " + fileName);
			SoundError::call(String(magic) == "WAVE", "Wrong wav file format. (no WAVE format): " + fileName);

			// check 'fmt ' sub chunk (1)
			SoundError::call(soundData->read(magic, 4) == 4, "Cannot read wav file " + fileName);
			SoundError::call(String(magic) == "fmt ", "Wrong wav file format. (no 'fmt ' subchunk): " + fileName);

			// read (1)'s size
			SoundError::call(soundData->read(buffer32, 4) == 4, "Cannot read wav file " + fileName);
			unsigned long subChunk1Size = readSoundByte32(buffer32);
			SoundError::call(subChunk1Size >= 16, "Wrong wav file format. ('fmt ' chunk too small, truncated file?): " + fileName);

			// check PCM audio format
			SoundError::call(soundData->read(buffer16, 2) == 2, "Cannot read wav file " + fileName);
			unsigned short audioFormat = readSoundByte16(buffer16);
			SoundError::call(audioFormat == 1, "Wrong wav file format. This file is not a .wav file (audio format is not PCM): " + fileName);

			// read number of channels
			SoundError::call(soundData->read(buffer16, 2) == 2, "Cannot read wav file " + fileName);
			channels = readSoundByte16(buffer16);

			// read frequency (sample rate)
			SoundError::call(soundData->read(buffer32, 4) == 4, "Cannot read wav file " + fileName);
			*sampleRate = readSoundByte32(buffer32);

			// skip 6 bytes (Byte rate (4), Block align (2))
			soundData->skip(6);

			// read bits per sample
			SoundError::call(soundData->read(buffer16, 2) == 2, "Cannot read wav file " + fileName);
			bits = readSoundByte16(buffer16);

			// check 'data' sub chunk (2)
			SoundError::call(soundData->read(magic, 4) == 4, "Cannot read wav file " + fileName);
			SoundError::call(String(magic) == "data" || std::string(magic) == "fact", "Wrong wav file format. (no data subchunk): " + fileName);

			// fact is an option section we don't need to worry about
			if(String(magic) == "fact")
			{
				soundData->skip(8);

				// Now we shoudl hit the data chunk
				SoundError::call(soundData->read(magic, 4) == 4, "Cannot read wav file " + fileName);
				SoundError::call(String(magic) == "data", "Wrong wav file format. (no data subchunk): " + fileName);
			}

			// The next four bytes are the size remaing of the file
			SoundError::call(soundData->read(buffer32, 4) == 4, "Cannot read wav file " + fileName);
			*dataSize = readSoundByte32(buffer32);

			prepareBuffer(channels, bits, *sampleRate, &bufferSize, dataFormat);

			std::vector<char> vdata = getSoundData(*dataSize, bufferSize, soundData);
			*dataSize = vdata.size();
			void* data = malloc(sizeof(char) * vdata.size());
			memcpy(data, &vdata[0], sizeof(char) * vdata.size());

			return data;
		}
	}
	//---------------------------------------------------------
}