/********************************************************************
	created:	Monday 2010/09/06 at 13:43
	filename: 	LoadCafData.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
// Apple-only: decodes .caf files through AudioToolbox (ExtAudioFile) into
// 16 bit signed PCM for OpenAL Soft. Works on macOS and iOS alike now (used
// to be iPhone-only); on other platforms loadSoundData never calls this.
#ifdef __APPLE__
#include "engine_sound/SoundPlatform.h"
#ifdef ORKIGE_OPENAL_SOUND
#include "engine_util/ResourceUtil.h"
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/ExtendedAudioFile.h>

namespace Orkige
{
	namespace SoundUtil
	{
		void* loadCafDataInternal(CFURLRef fileURL, ALsizei *dataSize, ALenum *dataFormat, ALsizei*	sampleRate);
		//---------------------------------------------------------
		void* loadCafData(Orkige::String const & fileName, ALsizei *dataSize, ALenum *dataFormat, ALsizei* sampleRate)
		{
			// resolve the file through the OGRE resource system (CAF decoding
			// needs a real file on disk for ExtAudioFileOpenURL)
			String fullPath = ResourceUtil::findPath(fileName) + fileName;
			CFStringRef pathString = CFStringCreateWithCString(kCFAllocatorDefault, fullPath.c_str(), kCFStringEncodingUTF8);
			CFURLRef fileURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, pathString, kCFURLPOSIXPathStyle, false);
			CFRelease(pathString);
			void* data = loadCafDataInternal(fileURL, dataSize, dataFormat, sampleRate);
			CFRelease(fileURL);
			return data;
		}
		//---------------------------------------------------------
		void* loadCafDataInternal(CFURLRef fileURL, ALsizei *dataSize, ALenum *dataFormat, ALsizei*	sampleRate)
		{
			OSStatus						err = noErr;
			SInt64							fileLengthInFrames = 0;
			AudioStreamBasicDescription		fileFormat;
			UInt32							propertySize = sizeof(fileFormat);
			ExtAudioFileRef					extRef = NULL;
			void*							data = NULL;
			AudioStreamBasicDescription		outputFormat;

			// Open a file with ExtAudioFileOpen()
			err = ExtAudioFileOpenURL(fileURL, &extRef);
			if(err)
			{
				oDebugMsg("sound", 0, "ExtAudioFileOpenURL FAILED, Error = " << (int)err);
				goto Exit;
			}

			// Get the audio data format
			err = ExtAudioFileGetProperty(extRef, kExtAudioFileProperty_FileDataFormat, &propertySize, &fileFormat);
			if(err)
			{
				oDebugMsg("sound", 0, "ExtAudioFileGetProperty(kExtAudioFileProperty_FileDataFormat) FAILED, Error = " << (int)err);
				goto Exit;
			}
			if (fileFormat.mChannelsPerFrame > 2)
			{
				oDebugMsg("sound", 0, "Unsupported Format, channel count is greater than stereo");
				goto Exit;
			}

			// Set the client format to 16 bit signed integer (native-endian) data
			// Maintain the channel count and sample rate of the original source format
			outputFormat.mSampleRate = fileFormat.mSampleRate;
			outputFormat.mChannelsPerFrame = fileFormat.mChannelsPerFrame;

			outputFormat.mFormatID = kAudioFormatLinearPCM;
			outputFormat.mBytesPerPacket = 2 * outputFormat.mChannelsPerFrame;
			outputFormat.mFramesPerPacket = 1;
			outputFormat.mBytesPerFrame = 2 * outputFormat.mChannelsPerFrame;
			outputFormat.mBitsPerChannel = 16;
			outputFormat.mFormatFlags = (UInt32)kAudioFormatFlagsNativeEndian | (UInt32)kAudioFormatFlagIsPacked | (UInt32)kAudioFormatFlagIsSignedInteger;

			// Set the desired client (output) data format
			err = ExtAudioFileSetProperty(extRef, kExtAudioFileProperty_ClientDataFormat, sizeof(outputFormat), &outputFormat);
			if(err)
			{
				oDebugMsg("sound", 0, "ExtAudioFileSetProperty(kExtAudioFileProperty_ClientDataFormat) FAILED, Error = " << (int)err);
				goto Exit;
			}

			// Get the total frame count
			propertySize = sizeof(fileLengthInFrames);
			err = ExtAudioFileGetProperty(extRef, kExtAudioFileProperty_FileLengthFrames, &propertySize, &fileLengthInFrames);
			if(err)
			{
				oDebugMsg("sound", 0, "ExtAudioFileGetProperty(kExtAudioFileProperty_FileLengthFrames) FAILED, Error = " << (int)err);
				goto Exit;
			}
			{
				// Read all the data into memory
				UInt32		numFrames = (UInt32)fileLengthInFrames;
				UInt32		_dataSize = numFrames * outputFormat.mBytesPerFrame;
				data = malloc(_dataSize);
				if (data)
				{
					AudioBufferList		dataBuffer;
					dataBuffer.mNumberBuffers = 1;
					dataBuffer.mBuffers[0].mDataByteSize = _dataSize;
					dataBuffer.mBuffers[0].mNumberChannels = outputFormat.mChannelsPerFrame;
					dataBuffer.mBuffers[0].mData = data;

					// Read the data into an AudioBufferList
					err = ExtAudioFileRead(extRef, &numFrames, &dataBuffer);
					if(err == noErr)
					{
						// success
						*dataSize = (ALsizei)_dataSize;
						*dataFormat = (outputFormat.mChannelsPerFrame > 1) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
						*sampleRate = (ALsizei)outputFormat.mSampleRate;
					}
					else
					{
						// failure
						free (data);
						data = NULL; // make sure to return NULL
						oDebugMsg("sound", 0, "ExtAudioFileRead FAILED, Error = " << (int)err);
						goto Exit;
					}
				}
			}
Exit:
			// Dispose the ExtAudioFileRef, it is no longer needed
			if (extRef)
			{
				ExtAudioFileDispose(extRef);
			}
			return data;
		}
	}
}
#endif //ORKIGE_OPENAL_SOUND
#endif //__APPLE__
