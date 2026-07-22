/**************************************************************
	created:	2026/07/19 at 10:00
	filename: 	SkyEnvMap.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "core_util/SkyEnvMap.h"
#include "core_util/AtmosphereSunDrive.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace SkyEnvMap
	{
		namespace
		{
			inline float clamp01(float v)
			{
				return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
			}
			inline unsigned char toByte(float linear)
			{
				const float v = clamp01(linear) * 255.0f + 0.5f;
				return static_cast<unsigned char>(v);
			}
		}
		//---------------------------------------------------------
		Colour skyColour(float dx, float dy, float dz,
			AtmosphereDesc const & desc, float sx, float sy, float sz)
		{
			// the ONE shared sky model (AtmosphereSunDrive::skyModelColour - the
			// CPU evaluation of the native sky pixel formula), saturated to the
			// 8-bit capture range the same way the un-tonemapped pipeline clips
			float r = 0.0f, g = 0.0f, b = 0.0f;
			AtmosphereSunDrive::skyModelColour(desc, sx, sy, sz, dx, dy, dz,
				false /*skipSun*/, r, g, b);
			Colour out;
			out.r = clamp01(r);
			out.g = clamp01(g);
			out.b = clamp01(b);
			return out;
		}
		//---------------------------------------------------------
		void faceDirection(unsigned int face, float u, float v,
			float & outX, float & outY, float & outZ)
		{
			// u,v are texel-centre fractions in [0;1]; map to [-1;1] face-local
			const float a = 2.0f * u - 1.0f;
			const float b = 2.0f * v - 1.0f;
			float x = 0.0f, y = 0.0f, z = 0.0f;
			switch(face)
			{
			case 0: x =  1.0f; y =   -b; z =   -a; break;	// +X
			case 1: x = -1.0f; y =   -b; z =    a; break;	// -X
			case 2: x =    a; y =  1.0f; z =    b; break;	// +Y
			case 3: x =    a; y = -1.0f; z =   -b; break;	// -Y
			case 4: x =    a; y =   -b; z =  1.0f; break;	// +Z
			case 5: x =   -a; y =   -b; z = -1.0f; break;	// -Z
			default: break;
			}
			const float len = std::sqrt(x * x + y * y + z * z);
			const float inv = len > 0.0f ? 1.0f / len : 0.0f;
			outX = x * inv;
			outY = y * inv;
			outZ = z * inv;
		}
		//---------------------------------------------------------
		void renderFaceRgba8(unsigned int face, unsigned int edge,
			AtmosphereDesc const & desc, float sx, float sy, float sz,
			unsigned char * out)
		{
			const float inv = edge > 0u ? 1.0f / static_cast<float>(edge) : 0.0f;
			unsigned char * write = out;
			for(unsigned int row = 0; row < edge; ++row)
			{
				const float v = (static_cast<float>(row) + 0.5f) * inv;
				for(unsigned int col = 0; col < edge; ++col)
				{
					const float u = (static_cast<float>(col) + 0.5f) * inv;
					float dirX, dirY, dirZ;
					faceDirection(face, u, v, dirX, dirY, dirZ);
					const Colour c = skyColour(dirX, dirY, dirZ, desc,
						sx, sy, sz);
					write[0] = toByte(c.r);
					write[1] = toByte(c.g);
					write[2] = toByte(c.b);
					write[3] = 255;
					write += 4;
				}
			}
		}
		//---------------------------------------------------------
		void halveFaceRgba8(unsigned char const * src, unsigned int srcEdge,
			unsigned char * dst)
		{
			const unsigned int dstEdge = srcEdge / 2u;
			for(unsigned int row = 0; row < dstEdge; ++row)
			{
				for(unsigned int col = 0; col < dstEdge; ++col)
				{
					const unsigned int sr = row * 2u;
					const unsigned int sc = col * 2u;
					unsigned char * out = dst + (row * dstEdge + col) * 4u;
					for(unsigned int channel = 0; channel < 4u; ++channel)
					{
						const unsigned int sum =
							src[(sr * srcEdge + sc) * 4u + channel] +
							src[(sr * srcEdge + sc + 1u) * 4u + channel] +
							src[((sr + 1u) * srcEdge + sc) * 4u + channel] +
							src[((sr + 1u) * srcEdge + sc + 1u) * 4u + channel];
						out[channel] = static_cast<unsigned char>((sum + 2u) / 4u);
					}
				}
			}
		}
		//---------------------------------------------------------
		unsigned int mipCountForEdge(unsigned int edge)
		{
			unsigned int mips = 1u;
			while(edge > 1u)
			{
				edge /= 2u;
				++mips;
			}
			return mips;
		}
		//---------------------------------------------------------
		size_t faceMipOffset(unsigned int edge, unsigned int mip,
			unsigned int face)
		{
			// walk the mip-major / face-major layout: sum the whole levels
			// before @p mip, then @p face faces of the target level
			size_t offset = 0;
			unsigned int levelEdge = edge;
			for(unsigned int m = 0; m < mip; ++m)
			{
				offset += size_t(levelEdge) * levelEdge * 4u * 6u;
				levelEdge = std::max(1u, levelEdge / 2u);
			}
			offset += size_t(levelEdge) * levelEdge * 4u * face;
			return offset;
		}
		//---------------------------------------------------------
		void buildCubemapChainRgba8(unsigned int edge,
			AtmosphereDesc const & desc, float sx, float sy, float sz,
			std::vector<unsigned char> & out, unsigned int & outMips)
		{
			const unsigned int mips = mipCountForEdge(edge);
			outMips = mips;
			// total tightly-packed size: sum over mips of edge_m^2 * 4 * 6
			size_t total = 0;
			for(unsigned int m = 0; m < mips; ++m)
			{
				const unsigned int e = std::max(1u, edge >> m);
				total += size_t(e) * e * 4u * 6u;
			}
			out.assign(total, 0);
			// base level from the sky model; each finer level a box downsample
			// of the previous level's matching face
			for(unsigned int face = 0; face < 6u; ++face)
			{
				renderFaceRgba8(face, edge, desc, sx, sy, sz,
					out.data() + faceMipOffset(edge, 0u, face));
				for(unsigned int m = 1; m < mips; ++m)
				{
					const unsigned int srcEdge = std::max(1u, edge >> (m - 1u));
					halveFaceRgba8(
						out.data() + faceMipOffset(edge, m - 1u, face),
						srcEdge,
						out.data() + faceMipOffset(edge, m, face));
				}
			}
		}
		//---------------------------------------------------------
		CaptureKey keyFor(AtmosphereDesc const & desc,
			float sx, float sy, float sz)
		{
			CaptureKey key;
			const float len = std::sqrt(sx * sx + sy * sy + sz * sz);
			const float inv = len > 0.0f ? 1.0f / len : 0.0f;
			key.sunX = sx * inv;
			key.sunY = sy * inv;
			key.sunZ = sz * inv;
			key.skyR = desc.skyRed;
			key.skyG = desc.skyGreen;
			key.skyB = desc.skyBlue;
			key.skyPower = desc.skyPower;
			key.density = desc.density;
			key.sunPower = desc.sunPower;
			return key;
		}
		//---------------------------------------------------------
		bool materiallyDiffers(CaptureKey const & last, CaptureKey const & now,
			float sunMoveCosThreshold)
		{
			const float dot = last.sunX * now.sunX + last.sunY * now.sunY +
				last.sunZ * now.sunZ;
			if(dot < sunMoveCosThreshold)
			{
				return true;
			}
			const float epsilon = 0.02f;
			return std::fabs(last.skyR - now.skyR) > epsilon ||
				std::fabs(last.skyG - now.skyG) > epsilon ||
				std::fabs(last.skyB - now.skyB) > epsilon ||
				std::fabs(last.skyPower - now.skyPower) > epsilon ||
				std::fabs(last.density - now.density) > epsilon ||
				std::fabs(last.sunPower - now.sunPower) > epsilon;
		}
	}
}
