/********************************************************************
	created:	Thursday 2026/07/23 at 11:00
	filename: 	AtmosphereFogSrs.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file AtmosphereFogSrs.cpp
//! @brief the atmospheric object-fog RTSS sub-render-state
//! (@see AtmosphereFogSrs.h). Mirrors the Ogre-Next atmosphere's object fog:
//! per-vertex haze colour from the shared sky model, per-pixel exp2
//! transmittance with the luminance breakthrough, blended in linear.

#include "engine_render_classic/AtmosphereFogSrs.h"

#ifdef USE_RTSHADER_SYSTEM

#include <OgreShaderFFPRenderState.h>
#include <OgreShaderProgram.h>
#include <OgreShaderProgramSet.h>
#include <OgreShaderFunction.h>
#include <core_debug/DebugMacros.h>

#include <algorithm>

namespace Orkige
{
	namespace
	{
		using namespace Ogre;
		using namespace Ogre::RTShader;

		//! the live fog state, pushed to every generated surface shader each
		//! frame (one global state - the whole scene shares one atmosphere,
		//! exactly like the hemisphere ambient)
		AtmosphereFogState gFogState;

		//! the sub-render-state type name (the factory + createSubRenderState key)
		const Ogre::String SRS_ATMOSPHERE_FOG = "OrkigeAtmosphereFog";

		//! the native fog-breakthrough defaults the next flavor runs with (the
		//! atmosphere preset never varies them on either flavor): a pixel's
		//! luminance above minBrightness eases it out of the fog at this falloff
		const float kFogBreakMinBrightness = 0.25f;
		const float kFogBreakFalloff = 0.1f;

		//---------------------------------------------------------
		//! VS: evaluates the sky model's haze colour along the camera->vertex
		//! ray (the native AtmosphereNprSkyHlms vertex piece, transliterated
		//! atom for atom) and forwards it + the camera->vertex delta. PS:
		//! weight = exp2(-length(delta) * fogDensity) eased by the luminance
		//! breakthrough, then mix(fogColour, lit, weight) in linear - one step
		//! before the display transfer, exactly like the native pixel piece.
		//! An ADDITIVE pass (the emissive glow pass - scene blend ONE/ONE)
		//! instead scales its output by the weight alone: the haze-colour
		//! contribution belongs to the material's base pass, so base + glow
		//! sums to exactly the native single-shader lerp(fog, lit+emissive, w)
		//! (the stock SRS_FOG mixed the additive pass toward the fog colour
		//! too, adding the fog a second time over every emissive surface).
		class AtmosphereFog : public SubRenderState
		{
		public:
			AtmosphereFog() : mAdditive(false) {}

			const Ogre::String & getType() const override
			{
				return SRS_ATMOSPHERE_FOG;
			}

			//! the fixed-function fog slot it replaces
			int getExecutionOrder() const override { return FFP_FOG; }

			//! the global fog state carries the look; the additive flag is
			//! re-derived from the pass in preAddToRenderState after cloning
			void copyFrom(const SubRenderState & rhs) override
			{
				mAdditive = static_cast<const AtmosphereFog &>(rhs).mAdditive;
			}

			//! derive the pass shape: an additive pass (ONE/ONE scene blend)
			//! gets the weight-only variant
			bool preAddToRenderState(const RenderState *, Pass * srcPass,
				Pass *) override
			{
				mAdditive = srcPass &&
					srcPass->getSourceBlendFactor() == Ogre::SBF_ONE &&
					srcPass->getDestBlendFactor() == Ogre::SBF_ONE;
				return true;
			}

			bool createCpuSubPrograms(ProgramSet * programSet) override;

			void updateGpuProgramsParams(Ogre::Renderable * rend,
				const Ogre::Pass * pass, const Ogre::AutoParamDataSource * source,
				const Ogre::LightList * lightList) override;

		private:
			bool				mAdditive;	//!< weight-only (glow pass) variant
			//--- VS uniforms (the view-independent sky-model terms) ---
			UniformParameterPtr	mWorldMatrix;
			UniformParameterPtr	mCameraPos;
			UniformParameterPtr	mSunDir;
			UniformParameterPtr	mSkyColour;
			UniformParameterPtr	mInvSkyColour;
			UniformParameterPtr	mSunAbsorption;
			UniformParameterPtr	mMieAbsorption;
			UniformParameterPtr	mSkyLightAbsorption;
			//! x = density, y = lightDensity, z = finalMultiplier, w = antiMie
			UniformParameterPtr	mVsParams;
			//! 1 / max(1 - sunHeight, 1e-4) (the ptDensity divisor)
			UniformParameterPtr	mInvOneMinusSunHeight;
			//--- PS uniforms ---
			//! x = fogDensity, y = minBrightness * falloff, z = -falloff
			UniformParameterPtr	mPsParams;
		};

		//---------------------------------------------------------
		bool AtmosphereFog::createCpuSubPrograms(ProgramSet * programSet)
		{
			Program * vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
			Function * vsMain = vsProgram->getEntryPointFunction();
			Program * psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
			Function * psMain = psProgram->getEntryPointFunction();

			// the world-delta helper lives in the fixed-function fog library
			// ("FFPLib_Fog" - its name constant is internal to the RTSS
			// component's own sources); its FFP_FogFactor sibling needs a
			// FOG_TYPE to preprocess (unused here - only the position/depth
			// helper is called)
			vsProgram->addDependency("FFPLib_Fog");
			vsProgram->addPreprocessorDefines("FOG_TYPE=1");

			mWorldMatrix = vsProgram->resolveParameter(
				GpuProgramParameters::ACT_WORLD_MATRIX);
			mCameraPos = vsProgram->resolveParameter(
				GpuProgramParameters::ACT_CAMERA_POSITION);
			// the sky-model terms feed the haze colour - the additive (glow)
			// variant never computes one, so it carries none of them
			if(!mAdditive)
			{
				mSunDir =
					vsProgram->resolveParameter(GCT_FLOAT3, "orkFogSunDir");
				mSkyColour =
					vsProgram->resolveParameter(GCT_FLOAT3, "orkFogSkyColour");
				mInvSkyColour = vsProgram->resolveParameter(GCT_FLOAT3,
					"orkFogInvSkyColour");
				mSunAbsorption = vsProgram->resolveParameter(GCT_FLOAT3,
					"orkFogSunAbsorption");
				mMieAbsorption = vsProgram->resolveParameter(GCT_FLOAT3,
					"orkFogMieAbsorption");
				mSkyLightAbsorption = vsProgram->resolveParameter(GCT_FLOAT3,
					"orkFogSkyLightAbsorption");
				mVsParams =
					vsProgram->resolveParameter(GCT_FLOAT4, "orkFogVsParams");
				mInvOneMinusSunHeight = vsProgram->resolveParameter(GCT_FLOAT1,
					"orkFogInvOneMinusSunHeight");
			}
			mPsParams = psProgram->resolveParameter(GCT_FLOAT4, "orkFogPsParams");

			auto positionObj =
				vsMain->resolveInputParameter(Parameter::SPC_POSITION_OBJECT_SPACE);
			// fresh varyings (SPC_UNKNOWN never aliases an existing
			// interpolant): the camera->vertex delta always; the haze colour
			// only where the pass mixes toward it (the additive glow variant
			// only dims - its haze contribution rides the base pass)
			ParameterPtr vsOutFogColour;
			if(!mAdditive)
			{
				vsOutFogColour = vsMain->resolveOutputParameter(
					Parameter::SPC_UNKNOWN, GCT_FLOAT3);
			}
			auto vsOutDelta = vsMain->resolveOutputParameter(
				Parameter::SPC_UNKNOWN, GCT_FLOAT3);

			auto delta = vsMain->resolveLocalParameter(GCT_FLOAT3, "orkFogDelta");
			auto depth = vsMain->resolveLocalParameter(GCT_FLOAT1, "orkFogDepth");

			auto vstage = vsMain->getStage(FFP_VS_FOG);
			// delta = worldPos - cameraPos; depth = length(delta)
			vstage.callFunction("FFP_PixelFog_PositionDepth",
				{ In(mWorldMatrix), In(mCameraPos), In(positionObj),
				  Out(delta), Out(depth) });
			vstage.assign(In(delta), Out(vsOutDelta));
			// the glow variant needs only the delta; the mixing variant
			// evaluates the haze colour along the ray
			if(!mAdditive)
			{
			auto dir = vsMain->resolveLocalParameter(GCT_FLOAT3, "orkFogDir");
			auto bend = vsMain->resolveLocalParameter(GCT_FLOAT1, "orkFogBend");
			auto ldotv360 =
				vsMain->resolveLocalParameter(GCT_FLOAT1, "orkFogLdotv");
			auto ptArg = vsMain->resolveLocalParameter(GCT_FLOAT1, "orkFogPtArg");
			auto ptDensity =
				vsMain->resolveLocalParameter(GCT_FLOAT1, "orkFogPtDensity");
			auto absorb =
				vsMain->resolveLocalParameter(GCT_FLOAT3, "orkFogAbsorb");
			auto gradient =
				vsMain->resolveLocalParameter(GCT_FLOAT3, "orkFogGradient");
			auto sharedTerms =
				vsMain->resolveLocalParameter(GCT_FLOAT3, "orkFogShared");
			auto haze = vsMain->resolveLocalParameter(GCT_FLOAT3, "orkFogHaze");
			auto term = vsMain->resolveLocalParameter(GCT_FLOAT3, "orkFogTerm");
			auto scale = vsMain->resolveLocalParameter(GCT_FLOAT1, "orkFogScale");
			vstage.callBuiltin("normalize", In(delta), Out(dir));
			// the horizon bend: dir.y += densityDiffusion(2.0) * 0.075
			//                            * (1 - dir.y)^2, renormalised
			// (the native camera displacement term is skipped: it is the
			// camera height over the 110km atmosphere - vanishing at scene
			// scale on both flavors)
			vstage.sub(In(1.0f), In(dir).y(), Out(bend));
			vstage.mul(In(bend), In(bend), Out(bend));
			vstage.mul(In(bend), In(0.15f), Out(bend));
			vstage.add(In(dir).y(), In(bend), Out(dir).y());
			vstage.callBuiltin("normalize", In(dir), Out(dir));
			// the horizon floor + the fog piece's extra lift (y*0.9 + 0.1),
			// renormalised - the native fog colour reads a hair above the
			// horizon, which is what keeps it warm instead of ground-dark
			vstage.callBuiltin("max", In(dir).y(), In(0.025f), Out(dir).y());
			vstage.mul(In(dir).y(), In(0.9f), Out(dir).y());
			vstage.add(In(dir).y(), In(0.1f), Out(dir).y());
			vstage.callBuiltin("normalize", In(dir), Out(dir));
			// ldotv360 = dot(dir, sunDir) * 0.5 + 0.5 (the mie phase)
			vstage.callBuiltin("dot", In(dir), In(mSunDir), Out(ldotv360));
			vstage.mul(In(ldotv360), In(0.5f), Out(ldotv360));
			vstage.add(In(ldotv360), In(0.5f), Out(ldotv360));
			// ptDensity = density / max(dir.y / (1 - sunHeight), 0.0035)^2
			// (the fog piece's exponent is the densityDiffusion constant 2.0
			// exactly - a plain square)
			vstage.mul(In(dir).y(), In(mInvOneMinusSunHeight), Out(ptArg));
			vstage.callBuiltin("max", In(ptArg), In(0.0035f), Out(ptArg));
			vstage.mul(In(ptArg), In(ptArg), Out(ptArg));
			vstage.div(In(mVsParams).x(), In(ptArg), Out(ptDensity));
			// skyAbsorption = exp2(skyColour * -ptDensity) * 2
			vstage.mul(In(mSkyColour), In(ptDensity), Out(absorb));
			vstage.sub(In(0.0f), In(absorb), Out(absorb));
			vstage.callBuiltin("exp2", In(absorb), Out(absorb));
			vstage.mul(In(absorb), In(2.0f), Out(absorb));
			// skyColourGradient = exp2(-dir.y / skyColour)^1.5
			// (x^1.5 = x * sqrt(x) - no vector-scalar pow in GLSL)
			vstage.mul(In(mInvSkyColour), In(dir).y(), Out(gradient));
			vstage.sub(In(0.0f), In(gradient), Out(gradient));
			vstage.callBuiltin("exp2", In(gradient), Out(gradient));
			vstage.callBuiltin("sqrt", In(gradient), Out(sharedTerms));
			vstage.mul(In(gradient), In(sharedTerms), Out(gradient));
			// haze = (gradient * skyAbsorption)
			//        * (sunAbsorption * antiMie
			//           + skyLightAbsorption * (mie * ptDensity * lightDensity))
			//        + mieAbsorption * mie, all scaled lightDensity * finalMul
			vstage.mul(In(gradient), In(absorb), Out(sharedTerms));
			vstage.mul(In(sharedTerms), In(mSunAbsorption), Out(haze));
			vstage.mul(In(haze), In(mVsParams).w(), Out(haze));
			vstage.mul(In(ldotv360), In(ptDensity), Out(scale));
			vstage.mul(In(scale), In(mVsParams).y(), Out(scale));
			vstage.mul(In(sharedTerms), In(mSkyLightAbsorption), Out(term));
			vstage.mul(In(term), In(scale), Out(term));
			vstage.add(In(haze), In(term), Out(haze));
			vstage.mul(In(mMieAbsorption), In(ldotv360), Out(term));
			vstage.add(In(haze), In(term), Out(haze));
			vstage.mul(In(haze), In(mVsParams).y(), Out(haze));
			vstage.mul(In(haze), In(mVsParams).z(), Out(haze));
			vstage.assign(In(haze), Out(vsOutFogColour));
			}

			ParameterPtr psInFogColour;
			if(!mAdditive)
			{
				psInFogColour = psMain->resolveInputParameter(vsOutFogColour);
			}
			auto psInDelta = psMain->resolveInputParameter(vsOutDelta);
			auto outColour =
				psMain->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);

			auto dist = psMain->resolveLocalParameter(GCT_FLOAT1, "orkFogDist");
			auto lum = psMain->resolveLocalParameter(GCT_FLOAT1, "orkFogLum");
			auto lumTerm =
				psMain->resolveLocalParameter(GCT_FLOAT1, "orkFogLumTerm");
			auto lumWeight =
				psMain->resolveLocalParameter(GCT_FLOAT1, "orkFogLumWeight");
			auto weight =
				psMain->resolveLocalParameter(GCT_FLOAT1, "orkFogWeight");

			// one step before the display transfer (FFP_PS_POST_PROCESS): the
			// blend runs on the LINEAR lit colour, like the native pixel piece
			auto pstage = psMain->getStage(FFP_PS_FOG);
			// the Euclidean camera-to-fragment distance, per pixel
			pstage.callBuiltin("length", In(psInDelta), Out(dist));
			// the luminance breakthrough:
			// lumWeight = exp2(-falloff * luminance + minBrightness * falloff)
			pstage.callBuiltin("dot", In(outColour).xyz(),
				In(Ogre::Vector3(0.212655f, 0.715158f, 0.072187f)), Out(lum));
			pstage.mul(In(lum), In(mPsParams).z(), Out(lumTerm));
			pstage.add(In(lumTerm), In(mPsParams).y(), Out(lumTerm));
			pstage.callBuiltin("exp2", In(lumTerm), Out(lumWeight));
			pstage.callBuiltin("max", In(lumWeight), In(0.0f), Out(lumWeight));
			// weight = lerp(1, exp2(-dist * fogDensity), lumWeight)
			pstage.mul(In(dist), In(mPsParams).x(), Out(weight));
			pstage.sub(In(0.0f), In(weight), Out(weight));
			pstage.callBuiltin("exp2", In(weight), Out(weight));
			pstage.sub(In(weight), In(1.0f), Out(weight));
			pstage.mul(In(weight), In(lumWeight), Out(weight));
			pstage.add(In(weight), In(1.0f), Out(weight));
			if(mAdditive)
			{
				// glow = glow * weight: the additive pass fades with the fog
				// like the emissive term inside the native single shader; the
				// haze-colour side of the lerp already rode the base pass
				pstage.mul(In(outColour).xyz(), In(weight),
					Out(outColour).xyz());
			}
			else
			{
				// lit = mix(fogColour, lit, weight)
				pstage.callBuiltin("mix", In(psInFogColour),
					In(outColour).xyz(), In(weight), Out(outColour).xyz());
			}
			return true;
		}

		//---------------------------------------------------------
		void AtmosphereFog::updateGpuProgramsParams(Ogre::Renderable *,
			const Ogre::Pass *, const Ogre::AutoParamDataSource *,
			const Ogre::LightList *)
		{
			AtmosphereFogState const & fog = gFogState;
			// a disabled atmosphere pushes density 0: exp2(0) = 1, the blend
			// keeps the lit colour bit-exactly
			const float fogDensity = fog.enabled ? fog.fogDensity : 0.0f;
			if(mSunDir)
			{
				mSunDir->setGpuParameter(fog.sunDir);
			}
			if(mSkyColour)
			{
				mSkyColour->setGpuParameter(fog.skyColour);
			}
			if(mInvSkyColour)
			{
				// the gradient divisor, guarded like the CPU model's
				// max(skyColour, 1e-3)
				mInvSkyColour->setGpuParameter(Ogre::Vector3(
					1.0f / std::max(fog.skyColour.x, 1e-3f),
					1.0f / std::max(fog.skyColour.y, 1e-3f),
					1.0f / std::max(fog.skyColour.z, 1e-3f)));
			}
			if(mSunAbsorption)
			{
				mSunAbsorption->setGpuParameter(fog.sunAbsorption);
			}
			if(mMieAbsorption)
			{
				mMieAbsorption->setGpuParameter(fog.mieAbsorption);
			}
			if(mSkyLightAbsorption)
			{
				mSkyLightAbsorption->setGpuParameter(fog.skyLightAbsorption);
			}
			if(mVsParams)
			{
				mVsParams->setGpuParameter(Ogre::Vector4(fog.density,
					fog.lightDensity, fog.finalMultiplier, fog.antiMie));
			}
			if(mInvOneMinusSunHeight)
			{
				mInvOneMinusSunHeight->setGpuParameter(
					1.0f / std::max(1.0f - fog.sunHeight, 1e-4f));
			}
			if(mPsParams)
			{
				mPsParams->setGpuParameter(Ogre::Vector4(fogDensity,
					kFogBreakMinBrightness * kFogBreakFalloff,
					-kFogBreakFalloff, 0.0f));
			}
		}

		//---------------------------------------------------------
		//! the factory the generator clones per-material instances through
		class AtmosphereFogFactory : public SubRenderStateFactory
		{
		public:
			const Ogre::String & getType() const override
			{
				return SRS_ATMOSPHERE_FOG;
			}

		protected:
			SubRenderState * createInstanceImpl() override
			{
				return OGRE_NEW AtmosphereFog;
			}
		};

		//! one factory for the process, registered on first use and owned here
		//! (kept alive for the generator's lifetime, like OGRE's own factories)
		AtmosphereFogFactory gAtmosphereFogFactory;
		bool gAtmosphereFogFactoryRegistered = false;
	}

	//---------------------------------------------------------
	void noteAtmosphereFog(AtmosphereFogState const & state)
	{
		gFogState = state;
	}

	//---------------------------------------------------------
	AtmosphereFogState const & atmosphereFogState()
	{
		return gFogState;
	}

	//---------------------------------------------------------
	void addAtmosphereFogSubRenderState(
		Ogre::RTShader::ShaderGenerator * generator,
		Ogre::RTShader::RenderState * renderState)
	{
		oAssert(generator && renderState);
		if(!gAtmosphereFogFactoryRegistered)
		{
			generator->addSubRenderStateFactory(&gAtmosphereFogFactory);
			gAtmosphereFogFactoryRegistered = true;
		}
		renderState->addTemplateSubRenderState(
			generator->createSubRenderState(SRS_ATMOSPHERE_FOG));
	}
}

#endif // USE_RTSHADER_SYSTEM
