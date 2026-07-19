/**************************************************************
	created:	2026/07/19 at 10:00
	filename: 	SkyEnvMap.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "core_util/SkyEnvMap.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace SkyEnvMap
	{
		namespace
		{
			//! smoothstep 0..1 (Hermite), for the elevation gradient blend -
			//! the SAME curve the classic gradient sky dome uses
			inline float smoothstep01(float t)
			{
				t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
				return t * t * (3.0f - 2.0f * t);
			}
			inline float clamp01(float v)
			{
				return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
			}
			inline float mix(float a, float b, float t)
			{
				return a + (b - a) * t;
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
			// the vertical gradient - zenith -> horizon -> ground - hazed and
			// brightened toward the horizon by the sky density (the exact model
			// the classic flavor draws its visible procedural dome with)
			const float power = std::max(desc.skyPower, 0.0f);
			const float zenithR = desc.skyRed * power;
			const float zenithG = desc.skyGreen * power;
			const float zenithB = desc.skyBlue * power;
			const float haze = std::min(0.85f, std::max(0.0f, desc.density * 0.6f));
			const float hazeLevel = std::min(1.2f, std::max(power, 0.15f));
			const float horizonR = mix(zenithR, hazeLevel, haze);
			const float horizonG = mix(zenithG, hazeLevel, haze);
			const float horizonB = mix(zenithB, hazeLevel, haze);
			const float groundR = zenithR * 0.35f;
			const float groundG = zenithG * 0.35f;
			const float groundB = zenithB * 0.35f;
			const float elevation = dy;	// unit sphere: y is the elevation
			float baseR, baseG, baseB;
			if(elevation >= 0.0f)
			{
				const float t = smoothstep01(elevation);
				baseR = mix(horizonR, zenithR, t);
				baseG = mix(horizonG, zenithG, t);
				baseB = mix(horizonB, zenithB, t);
			}
			else
			{
				const float t = smoothstep01(-elevation);
				baseR = mix(horizonR, groundR, t);
				baseG = mix(horizonG, groundG, t);
				baseB = mix(horizonB, groundB, t);
			}
			// the sun glow: a tight bright core + a soft wide halo toward the sun
			const float toward = std::max(0.0f, dx * sx + dy * sy + dz * sz);
			const float glow = std::pow(toward, 160.0f) * 0.85f
				+ std::pow(toward, 6.0f) * 0.25f;
			Colour out;
			out.r = clamp01(baseR + 1.00f * glow);
			out.g = clamp01(baseG + 0.92f * glow);
			out.b = clamp01(baseB + 0.78f * glow);
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
