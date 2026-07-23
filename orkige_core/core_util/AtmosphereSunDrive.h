/**************************************************************
	created:	2026/07/17 at 06:00
	filename: 	AtmosphereSunDrive.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __AtmosphereSunDrive_h__17_7_2026__06_00_00__
#define __AtmosphereSunDrive_h__17_7_2026__06_00_00__

#include "core_util/AtmosphereDesc.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	//! @brief the sun-exposure linkage curve, pure: what an enabled atmosphere
	//! drives onto its linked sun (the FIRST directional light) and the scene
	//! ambient, from the sun's elevation and the AtmosphereDesc exposure knobs.
	//!
	//! This is a faithful reimplementation of the day/night colour model the
	//! default render backend evaluates natively (the NprSky atmosphere's
	//! sun/ambient synchronisation), so BOTH flavors read the same numbers:
	//! the next flavor gets the model from its atmosphere component, the
	//! classic backend consumes @ref compute to drive its light and flat
	//! ambient through the same curve. Pure (plain floats, no render types),
	//! so it unit-tests headlessly - AtmosphereSunDriveTests asserts the
	//! monotone day->night behaviour both backends rely on.
	//!
	//! CALIBRATION (the classic* fields): the two flavors shade differently -
	//! PBS divides the direct term by pi and (on a non-sRGB swapchain)
	//! gamma-encodes its linear result in-shader, while classic Blinn-Phong
	//! multiplies gamma-space colours directly. A single closed-form mapping
	//! makes the flavors agree at the mid-grey reference surface (albedo 0.5,
	//! lit head-on): classicLevel(x) = sqrt(2 x / pi) - the sqrt linearisation
	//! of the encoded PBS response at that reference. It is monotone, so the
	//! whole day->night arc keeps the same ordering on both flavors; it is NOT
	//! a per-pixel match (lit 3D content is a tolerance-parity case, see
	//! Docs/render-abstraction.md).
	namespace AtmosphereSunDrive
	{
		//! everything the linkage drives; next* fields are the linear model
		//! values (what the native atmosphere sets), classic* fields are the
		//! calibrated gamma-space equivalents the classic backend applies
		struct Drive
		{
			//--- the linked sun ---
			float sunRed;			//!< driven sun colour, normalized (max channel = 1)
			float sunGreen;
			float sunBlue;
			float nextSunPower;		//!< linear power scale (= desc.sunPower)
			float classicSunScale;	//!< classic diffuse multiplier for the colour

			//--- the scene ambient fill ---
			float nextUpperRed;		//!< linear upper-hemisphere ambient
			float nextUpperGreen;
			float nextUpperBlue;
			float nextLowerRed;		//!< linear lower-hemisphere ambient
			float nextLowerGreen;
			float nextLowerBlue;
			float classicAmbientRed;	//!< calibrated flat classic ambient
			float classicAmbientGreen;	//!< (the averaged-flat hemisphere subset)
			float classicAmbientBlue;
		};

		namespace Detail
		{
			//! a minimal float triple for the internal colour math
			struct V3 { float x, y, z; };

			inline float clampf(float v, float lo, float hi)
			{
				return v < lo ? lo : (v > hi ? hi : v);
			}
			inline V3 mul(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
			inline V3 mulv(V3 a, V3 b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
			inline V3 add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
			inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
			inline float smoothstep(float e0, float e1, float x)
			{
				const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
				return t * t * (3.0f - 2.0f * t);
			}
			inline V3 exp2v(V3 v)
			{
				return { std::exp2(v.x), std::exp2(v.y), std::exp2(v.z) };
			}
			inline V3 pow3(V3 v, float e)
			{
				return { std::pow(v.x, e), std::pow(v.y, e), std::pow(v.z, e) };
			}
			inline V3 normalize(V3 v)
			{
				const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
				return len > 0.0f ? mul(v, 1.0f / len) : V3{ 0.0f, 1.0f, 0.0f };
			}
			inline float dot(V3 a, V3 b)
			{
				return a.x * b.x + a.y * b.y + a.z * b.z;
			}

			//! the model constants the desc does not carry - the atmosphere
			//! component's native preset defaults (the linkage never varies
			//! them, on either flavor)
			constexpr float kDensityDiffusion = 2.0f;
			constexpr float kHorizonLimit = 0.025f;
			constexpr float kSkySunDiskPower = 1.0f;
			constexpr float kPi = 3.14159265358979f;

			//! Rayleigh-inspired absorption (in [0;1] for absorption in [0;1])
			inline V3 skyRayleighAbsorption(V3 dir, float density)
			{
				return mul(exp2v(mul(dir, -density)), 2.0f);
			}

			//! the atmosphere colour along @p viewDir - the sky model sampled
			//! on the CPU, mirroring the native evaluation (sun disk optional)
			inline V3 atmosphereAt(AtmosphereDesc const & desc, V3 toSun,
				float sunHeight, V3 viewDir, bool skipSun,
				float sunDiskPower = kSkySunDiskPower)
			{
				V3 dir = normalize(viewDir);
				const V3 skyColour = { desc.skyRed, desc.skyGreen, desc.skyBlue };
				// exp2(-1/sunHeight), the native day weight - which the model
				// evaluates for BELOW-horizon suns too (negative sunHeight
				// makes it >1: the night branch). Guard the h->0 singularity
				// the shader never hits exactly; cap the night branch so the
				// pure form stays finite for any elevation.
				const float guardedHeight = sunHeight >= 0.0f
					? std::max(sunHeight, 1e-4f)
					: std::min(sunHeight, -1e-4f);
				const float sunHeightWeight = std::min(
					std::exp2(-1.0f / guardedHeight), 64.0f);
				const float lightDensity = desc.density /
					std::pow(std::max(sunHeight, 0.0035f), 0.75f);
				const float finalMultiplier =
					(0.5f + smoothstep(0.02f, 0.4f, sunHeightWeight)) *
					desc.skyPower;
				const V3 one = { 1.0f, 1.0f, 1.0f };
				const V3 sunAbsorption = skyRayleighAbsorption(
					{ 1.0f - skyColour.x, 1.0f - skyColour.y,
					  1.0f - skyColour.z }, lightDensity);
				const V3 mieAbsorption = mul(
					V3{ lerp(skyColour.x, 1.0f, sunHeightWeight),
						lerp(skyColour.y, 1.0f, sunHeightWeight),
						lerp(skyColour.z, 1.0f, sunHeightWeight) },
					std::pow(std::max(1.0f - lightDensity, 0.1f), 4.0f));
				const V3 skyLightAbsorption =
					skyRayleighAbsorption(skyColour, lightDensity);

				const float lDotV = std::max(dot(dir, toSun), 0.0f);
				dir.y += kDensityDiffusion * 0.075f *
					(1.0f - dir.y) * (1.0f - dir.y);
				dir = normalize(dir);
				dir.y = std::max(dir.y, kHorizonLimit);
				dir = normalize(dir);
				const float lDotV360 = dot(dir, toSun) * 0.5f + 0.5f;

				const float ptDensity = desc.density /
					std::pow(std::max(
						dir.y / std::max(1.0f - sunHeight, 1e-4f), 0.0035f),
						lerp(0.10f, kDensityDiffusion, std::pow(dir.y, 0.3f)));
				// the native disk exponent carries a 0.25 scale; inert for the
				// sun/ambient linkage (its samples sit at lDotV 1 or skip the
				// sun) but it shapes the dome/IBL pixels sampled off-axis. The
				// disk MULTIPLIER, however, is NOT inert for the linkage: the
				// native getAtmosphereAt scales the sun disk by the sun POWER
				// (getSunDisk's sunPower arg), and since the disk term is added
				// AFTER the haze is scaled by lightDensity*finalMultiplier, its
				// weight relative to the (bluer) haze sets how warm the
				// normalized sun colour reads. The dome/IBL callers keep the
				// neutral kSkySunDiskPower default (their look is calibrated +
				// byte-stable); the sun-colour linkage passes desc.sunPower so
				// the classic driven sun carries the SAME grazing warmth the
				// native atmosphere gives next (AtmosphereSunDriveTests locks the
				// day/noon ratio; the render_facade driven-sun probe the sweep).
				const float sunDisk = std::pow(lDotV,
					lerp(4.0f, 8500.0f, sunHeight) * 0.25f) * sunDiskPower;
				const float antiMie = std::max(sunHeightWeight, 0.08f);
				const V3 skyAbsorption =
					skyRayleighAbsorption(skyColour, ptDensity);
				const V3 skyColourGradient = pow3(exp2v(
					{ -dir.y / std::max(skyColour.x, 1e-3f),
					  -dir.y / std::max(skyColour.y, 1e-3f),
					  -dir.y / std::max(skyColour.z, 1e-3f) }), 1.5f);
				const float mie = lDotV360;

				const V3 sharedTerms = mulv(skyColourGradient, skyAbsorption);
				V3 colour = mul(mulv(sharedTerms, sunAbsorption), antiMie);
				colour = add(colour, mul(mulv(sharedTerms, skyLightAbsorption),
					mie * ptDensity * lightDensity));
				colour = add(colour, mul(mieAbsorption, mie));
				colour = mul(colour, lightDensity);
				colour = mul(colour, finalMultiplier);
				if(!skipSun)
				{
					colour = add(colour, mul(skyLightAbsorption, sunDisk));
				}
				(void)one;
				return colour;
			}

			//! the display-encode calibration: the classic gamma-space level
			//! equivalent of a linear drive level. For a diffuse surface the
			//! gamma-naive pipeline shows albedo_encoded * level * NdotL while
			//! the linear pipeline shows encode(albedo_linear * L * NdotL);
			//! under the 2.2 power-law gamma the two agree when
			//! level = L^(1/2.2) * cos0^(1/2.2 - 1) at a REFERENCE incidence
			//! cos0. The reference sits between head-on and the typical
			//! lit-content regime (the 1.15 factor - a full cos0=0.6 factor of
			//! 1.32 overshot the night scenes, where the ambient share
			//! dominates); the flavors' NdotL falloff SHAPES differ (linear
			//! here, power-law there), so one calibration point must pick a
			//! middle. The earlier sqrt(2x/pi) mid-grey heuristic sat ~40%
			//! darker across real day content (the classic-surfaces-read-darker
			//! divergence).
			inline float classicLevel(float linearLevel)
			{
				return std::pow(std::max(linearLevel, 0.0f), 1.0f / 2.2f) *
					1.15f;
			}
		}

		//! @brief the sky model's colour along @p (viewX,viewY,viewZ) under
		//! @p desc with the sun toward @p (toSunX,toSunY,toSunZ) - the ONE
		//! shared CPU evaluation of the native sky pixel formula. The classic
		//! dome, the SkyEnvMap IBL capture and the sun/ambient linkage below
		//! all read THIS model, and the next flavor shades it natively, so
		//! every sky-derived colour agrees across flavors by construction.
		//! HDR: the result can exceed 1 near a bright sun/horizon (the callers
		//! clip, like the un-tonemapped native pipeline).
		inline void skyModelColour(AtmosphereDesc const & desc,
			float toSunX, float toSunY, float toSunZ,
			float viewX, float viewY, float viewZ, bool skipSun,
			float & outR, float & outG, float & outB)
		{
			using namespace Detail;
			const V3 toSun = normalize({ toSunX, toSunY, toSunZ });
			// a BELOW-horizon sun parks the model at the horizon (floor 0):
			// the haze weight exp2(-1/sunHeight) explodes for a small negative
			// elevation (a just-set sun whited the night sky out on both
			// flavors) - night darkness is the night preset's job. The next
			// backend floors its native drive identically.
			const float sunHeight = clampf(toSun.y, 0.0f, 1.0f);
			const V3 colour = atmosphereAt(desc, toSun, sunHeight,
				{ viewX, viewY, viewZ }, skipSun);
			outR = colour.x;
			outG = colour.y;
			outB = colour.z;
		}

		//! @brief evaluate the linkage for a desc + toward-the-sun direction
		//! (@p toSunY is the sun elevation; the vector need not be normalized).
		//! Meaningful only for an ENABLED desc - the caller gates on it.
		inline Drive compute(AtmosphereDesc const & desc,
			float toSunX, float toSunY, float toSunZ)
		{
			using namespace Detail;
			const V3 toSun = normalize({ toSunX, toSunY, toSunZ });
			// the native phase convention: normTime = asin(elevation)/pi, and
			// the model's sunHeight = sin(normTime*pi) - the elevation itself,
			// FLOORED at the horizon like skyModelColour (the model's haze
			// weight explodes just below it; night darkness is the preset's)
			const float sunHeight = clampf(toSun.y, 0.0f, 1.0f);

			// the sun colour: the sky model sampled toward the sun,
			// normalized so the max channel is 1 (the power knob carries the
			// magnitude) - identical to the native linkage, which scales the
			// sun disk by the sun power (@see atmosphereAt sunDiskPower)
			V3 sunColour = atmosphereAt(desc, toSun, sunHeight, toSun, false,
				desc.sunPower);
			const float maxPower = std::max(
				{ sunColour.x, sunColour.y, sunColour.z, 1e-6f });
			sunColour = mul(sunColour, 1.0f / maxPower);

			// the hemisphere fill: the sky sampled up / near the horizon,
			// proportioned to the sun via maxPower (native convention), then
			// scaled by the desc's ambientPower exposure knob
			const V3 upProbe = { 0.0f, 1.0f, 0.0f };
			const V3 horizonProbe = normalize({ 1.0f, 0.1f, 0.0f });
			const float upperPower =
				0.1f * kPi * desc.ambientPower / maxPower;
			const float lowerPower =
				0.01f * kPi * desc.ambientPower / maxPower;
			const V3 upper = mul(
				atmosphereAt(desc, toSun, sunHeight, upProbe, true),
				upperPower);
			const V3 lower = mul(
				atmosphereAt(desc, toSun, sunHeight, horizonProbe, true),
				lowerPower);

			Drive out;
			out.sunRed = sunColour.x;
			out.sunGreen = sunColour.y;
			out.sunBlue = sunColour.z;
			out.nextSunPower = desc.sunPower;
			out.classicSunScale = classicLevel(desc.sunPower);
			out.nextUpperRed = upper.x;
			out.nextUpperGreen = upper.y;
			out.nextUpperBlue = upper.z;
			out.nextLowerRed = lower.x;
			out.nextLowerGreen = lower.y;
			out.nextLowerBlue = lower.z;
			// classic has flat ambient only: average the hemisphere in linear
			// first (the setAmbientHemisphere averaged-flat precedent), then
			// DISPLAY-ENCODE each channel with a SOFTENED exponent (x^(1/1.6)):
			// the full display encode (1/2.2) matches the linear pipeline for a
			// lone diffuse term, but the linear flavor's ambient path carries
			// its own attenuations, and fully-encoded ambient OVERSHOT the
			// night scenes ~2x (low levels lift hardest under the encode). The
			// softened curve meets the measured night and day fills between
			// the flavors; a raw linear ambient sat far too dark (the
			// away-from-sun cube-face divergence)
			auto encodeAmbient = [](float linear)
			{
				return std::pow(std::max(linear, 0.0f), 1.0f / 1.6f);
			};
			out.classicAmbientRed = encodeAmbient((upper.x + lower.x) * 0.5f);
			out.classicAmbientGreen = encodeAmbient((upper.y + lower.y) * 0.5f);
			out.classicAmbientBlue = encodeAmbient((upper.z + lower.z) * 0.5f);
			return out;
		}
	}
}

#endif //__AtmosphereSunDrive_h__17_7_2026__06_00_00__
