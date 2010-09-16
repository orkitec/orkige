/********************************************************************
	created:	Monday 2010/09/06 at 13:43
	filename: 	LoadCafData.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "engine_sound/SoundPlatform.h"
#include "engine_util/ResourceUtil.h"

namespace Orkige
{
	namespace SoundUtil
	{
#ifdef ORKIGE_IPHONE
		void* loadCafDataInternal(CFURLRef fileURL, ALsizei *dataSize, ALenum *dataFormat, ALsizei*	sampleRate);
		//---------------------------------------------------------
		void* LoadCafData(Orkige::String const & fileName, ALsizei *dataSize, ALenum *dataFormat, ALsizei* sampleRate)
		{
			NSString *tempString = [NSString stringWithCString:(/*GetIPhoneDataPath() + */ ResurceUtil::findPath(fileName) + fileName).c_str() encoding:NSASCIIStringEncoding];
			CFURLRef fileURL = (CFURLRef)[[NSURL fileURLWithPath:tempString] retain];
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
				oDebugMsg("sound", 0, "ExtAudioFileOpenURL FAILED, Error = " << err); 
				goto Exit; 
			}
	
			// Get the audio data format
			err = ExtAudioFileGetProperty(extRef, kExtAudioFileProperty_FileDataFormat, &propertySize, &fileFormat);
			if(err) 
			{ 
				oDebugMsg("sound", 0, "ExtAudioFileGetProperty(kExtAudioFileProperty_FileDataFormat) FAILED, Error = " << err); 
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
			outputFormat.mFormatFlags = kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked | kAudioFormatFlagIsSignedInteger;
	
			// Set the desired client (output) data format
			err = ExtAudioFileSetProperty(extRef, kExtAudioFileProperty_ClientDataFormat, sizeof(outputFormat), &outputFormat);
			if(err) 
			{
				oDebugMsg("sound", 0, "ExtAudioFileSetProperty(kExtAudioFileProperty_ClientDataFormat) FAILED, Error = " << err); 
				goto Exit; 
			}
	
			// Get the total frame count
			propertySize = sizeof(fileLengthInFrames);
			err = ExtAudioFileGetProperty(extRef, kExtAudioFileProperty_FileLengthFrames, &propertySize, &fileLengthInFrames);
			if(err) 
			{ 
				oDebugMsg("sound", 0, "ExtAudioFileGetProperty(kExtAudioFileProperty_FileLengthFrames) FAILED, Error = " << err); 
				goto Exit; 
			}
			{
				// Read all the data into memory
				UInt32		dataSize = fileLengthInFrames * outputFormat.mBytesPerFrame;;
				data = malloc(dataSize);
				if (data)
				{
					AudioBufferList		dataBuffer;
					dataBuffer.mNumberBuffers = 1;
					dataBuffer.mBuffers[0].mDataByteSize = dataSize;
					dataBuffer.mBuffers[0].mNumberChannels = outputFormat.mChannelsPerFrame;
					dataBuffer.mBuffers[0].mData = data;
		
					// Read the data into an AudioBufferList
					err = ExtAudioFileRead(extRef, (UInt32*)&fileLengthInFrames, &dataBuffer);
					if(err == noErr)
					{
						// success
						*dataSize = (ALsizei)dataSize;
						*dataFormat = (outputFormat.mChannelsPerFrame > 1) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
						*sampleRate = (ALsizei)outputFormat.mSampleRate;
					}
					else 
					{ 
						// failure
						free (data);
						data = NULL; // make sure to return NULL
						printf("MyGetOpenALAudioData: ExtAudioFileRead FAILED, Error = %ld\n", err); goto Exit;
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
#endif
	}
}