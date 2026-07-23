/********************************************************************
	created:	Thursday 2026/07/23 at 12:00
	filename: 	MetalRoughLightingSrs.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file MetalRoughLightingSrs.cpp
//! @brief the engine-owned metal-rough lighting sub-render-state
//! (@see MetalRoughLightingSrs.h). The program-building code is derived from
//! OGRE's stock Cook-Torrance lighting stage (MIT licensed); the response
//! changes live in the shader library it depends on
//! (media/rtss/OrkigeLib_MetalRough.glsl) plus three structural differences
//! here: no linear-colours program flag (albedo reaches the shader raw, the
//! way the other backend consumes it), no gamma defines on either program,
//! and a final sqrt() display transfer appended at the post-process stage
//! (the other backend's non-sRGB-target transfer).

#include "engine_render_classic/MetalRoughLightingSrs.h"

#ifdef USE_RTSHADER_SYSTEM

#include <OgreShaderFFPRenderState.h>
#include <OgreShaderProgram.h>
#include <OgreShaderProgramSet.h>
#include <OgreShaderFunction.h>
#include <core_debug/DebugMacros.h>

namespace Orkige
{
	namespace
	{
		using namespace Ogre;
		using namespace Ogre::RTShader;

		//! the sub-render-state type name (the factory + createSubRenderState key)
		const Ogre::String SRS_METAL_ROUGH_LIGHTING = "OrkigeMetalRoughLighting";

		//! the engine shader library carrying the metal-rough response
		//! (registered from orkige_engine/media/rtss like the other engine media)
		const char* const METAL_ROUGH_LIB = "OrkigeLib_MetalRough";

		//---------------------------------------------------------
		//! builds the per-pixel metal-rough lighting programs: albedo from the
		//! pass diffuse x interpolated colour x sampled texture (all raw),
		//! roughness/metalness from the pass specular's red/green channels,
		//! lights evaluated by the library's PBR_Lights, output display-encoded
		//! by Orkige_DisplayTransfer at the post-process stage.
		class MetalRoughLighting : public SubRenderState
		{
		public:
			const Ogre::String & getType() const override
			{
				return SRS_METAL_ROUGH_LIGHTING;
			}

			//! the lighting slot, exactly where the stage it replaces ran (the
			//! hemisphere-ambient and image-based-lighting stages order after it)
			int getExecutionOrder() const override { return FFP_LIGHTING; }

			void copyFrom(const SubRenderState & rhs) override
			{
				const MetalRoughLighting & other =
					static_cast<const MetalRoughLighting &>(rhs);
				this->mLightCount = other.mLightCount;
			}

			bool preAddToRenderState(const RenderState * renderState,
				Ogre::Pass * srcPass, Ogre::Pass *) override
			{
				if(!srcPass->getLightingEnabled())
				{
					return false;
				}
				this->mLightCount = renderState->getLightCount();
				return true;
			}

			bool createCpuSubPrograms(ProgramSet * programSet) override;

		private:
			int	mLightCount = 0;	//!< resolved light slots (LIGHT_COUNT)
		};

		//---------------------------------------------------------
		bool MetalRoughLighting::createCpuSubPrograms(ProgramSet * programSet)
		{
			Program * vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
			Function * vsMain = vsProgram->getEntryPointFunction();
			Program * psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
			Function * psMain = psProgram->getEntryPointFunction();

			// NOTE the deliberate absence of the stock stage's linear-colours
			// program define/flag on BOTH programs: material, vertex and sampled
			// colours reach the lighting maths raw (the texturing/colour decode
			// macros collapse to no-ops, and the CPU-side colour uniforms are
			// written verbatim) - the other backend's colour convention.
			vsProgram->addDependency("FFPLib_Transform");
			psProgram->addDependency("FFPLib_Transform");
			psProgram->addDependency("FFPLib_Texturing");
			psProgram->addDependency(METAL_ROUGH_LIB);
			psProgram->addPreprocessorDefines(
				StringUtil::format("LIGHT_COUNT=%d", this->mLightCount));

			// texture coordinates (forwarded for a possible orm map; kept for
			// parity with the stage this one replaces)
			auto uvSet = Parameter::SPC_TEXTURE_COORDINATE0;
			auto vsOutTexcoord = vsMain->getOutputParameter(uvSet, GCT_FLOAT2);
			ParameterPtr vsInTexcoord;
			if(!vsOutTexcoord)
			{
				vsInTexcoord = vsMain->resolveInputParameter(uvSet, GCT_FLOAT2);
				vsOutTexcoord = vsMain->resolveOutputParameter(uvSet, GCT_FLOAT2);
			}
			psMain->resolveInputParameter(vsOutTexcoord);

			// view-space position
			auto vsInPosition =
				vsMain->getLocalParameter(Parameter::SPC_POSITION_OBJECT_SPACE);
			if(!vsInPosition)
			{
				vsInPosition =
					vsMain->resolveInputParameter(Parameter::SPC_POSITION_OBJECT_SPACE);
			}
			auto vsOutViewPos =
				vsMain->resolveOutputParameter(Parameter::SPC_POSITION_VIEW_SPACE);
			auto viewPos = psMain->resolveInputParameter(vsOutViewPos);
			auto worldViewMatrix =
				vsProgram->resolveParameter(GpuProgramParameters::ACT_WORLDVIEW_MATRIX);

			// view-space normal (a normal-map stage may already have written one)
			auto viewNormal =
				psMain->getLocalParameter(Parameter::SPC_NORMAL_VIEW_SPACE);
			ParameterPtr vsInNormal, vsOutNormal;
			if(!viewNormal)
			{
				vsInNormal =
					vsMain->resolveInputParameter(Parameter::SPC_NORMAL_OBJECT_SPACE);
				vsOutNormal =
					vsMain->resolveOutputParameter(Parameter::SPC_NORMAL_VIEW_SPACE);
				viewNormal = psMain->resolveInputParameter(vsOutNormal);
			}

			auto outDiffuse =
				psMain->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);
			psMain->resolveLocalParameter(Parameter::SPC_COLOR_SPECULAR);

			auto vstage = vsMain->getStage(FFP_PS_COLOUR_BEGIN + 1);
			auto fstage = psMain->getStage(FFP_PS_PBR_LIGHTING_BEGIN);

			if(vsInTexcoord)
			{
				vstage.assign(vsInTexcoord, vsOutTexcoord);
			}
			vstage.callFunction("FFP_Transform", worldViewMatrix, vsInPosition,
				vsOutViewPos);
			if(vsOutNormal)
			{
				auto worldViewITMatrix = vsProgram->resolveParameter(
					GpuProgramParameters::ACT_NORMAL_MATRIX);
				vstage.callBuiltin("mul", worldViewITMatrix, vsInNormal,
					vsOutNormal);
			}

			// occlusion/roughness/metalness: occlusion 1, roughness/metalness
			// from the pass specular's red/green channels (the orm layout the
			// generated materials write - @see createMaterial)
			auto ormParams = psMain->resolveLocalParameter(GCT_FLOAT3, "ormParams");
			fstage.assign(1.0f, Out(ormParams).x());
			auto specular = psProgram->resolveParameter(
				GpuProgramParameters::ACT_SURFACE_SPECULAR_COLOUR);
			fstage.assign(In(specular).xy(), Out(ormParams).mask(Operand::OPM_YZ));

			auto sceneCol = psProgram->resolveParameter(
				GpuProgramParameters::ACT_DERIVED_SCENE_COLOUR);
			auto diffuse = psProgram->resolveParameter(
				GpuProgramParameters::ACT_SURFACE_DIFFUSE_COLOUR);
			auto baseColor = psMain->resolveLocalParameter(GCT_FLOAT3, "baseColor");
			auto pixelParams =
				psMain->resolveLocalStructParameter("PixelParams", "pixel");

			// baseColor = material diffuse x interpolated colour (which carries
			// any sampled albedo texture by this stage); alpha forwards
			fstage.mul(In(diffuse).xyz(), In(outDiffuse).xyz(), baseColor);
			fstage.assign(Ogre::Vector3(0), Out(outDiffuse).xyz());
			fstage.mul(In(diffuse).w(), In(outDiffuse).w(), Out(outDiffuse).w());

			fstage.callFunction("PBR_MakeParams",
				{ In(baseColor), In(ormParams), InOut(pixelParams) });

			fstage = psMain->getStage(FFP_PS_PBR_LIGHTING_END);
			if(this->mLightCount > 0)
			{
				auto lightPos = psProgram->resolveParameter(
					GpuProgramParameters::ACT_LIGHT_POSITION_VIEW_SPACE_ARRAY,
					this->mLightCount);
				auto lightDiffuse = psProgram->resolveParameter(
					GpuProgramParameters::ACT_LIGHT_DIFFUSE_COLOUR_POWER_SCALED_ARRAY,
					this->mLightCount);
				auto pointParams = psProgram->resolveParameter(
					GpuProgramParameters::ACT_LIGHT_ATTENUATION_ARRAY,
					this->mLightCount);
				auto spotParams = psProgram->resolveParameter(
					GpuProgramParameters::ACT_SPOTLIGHT_PARAMS_ARRAY,
					this->mLightCount);
				auto lightDirView = psProgram->resolveParameter(
					GpuProgramParameters::ACT_LIGHT_DIRECTION_VIEW_SPACE_ARRAY,
					this->mLightCount);

				std::vector<Operand> params = { In(viewNormal), In(viewPos),
					In(sceneCol), In(lightPos), In(lightDiffuse), In(pointParams),
					In(lightDirView), In(spotParams), In(pixelParams),
					InOut(outDiffuse).xyz() };

				// the integrated-PSSM receiver leaves its per-light factors in
				// this local when the material receives shadows
				if(auto shadowFactor = psMain->getLocalParameter("lShadowFactor"))
				{
					params.insert(params.begin(), In(shadowFactor));
				}

				fstage.callFunction("PBR_Lights", params);
			}

			// the display transfer, after fog (which blends in linear, like the
			// other backend's) and after the hemisphere/image-lighting stages
			// added their linear terms
			psMain->getStage(FFP_PS_POST_PROCESS)
				.callFunction("Orkige_DisplayTransfer", InOut(outDiffuse));

			return true;
		}

		//---------------------------------------------------------
		//! the factory the generator clones per-material instances through
		class MetalRoughLightingFactory : public SubRenderStateFactory
		{
		public:
			const Ogre::String & getType() const override
			{
				return SRS_METAL_ROUGH_LIGHTING;
			}

		protected:
			SubRenderState * createInstanceImpl() override
			{
				return OGRE_NEW MetalRoughLighting;
			}
		};

		//! one factory for the process, registered on first use and owned here
		//! (kept alive for the generator's lifetime, like OGRE's own factories)
		MetalRoughLightingFactory gMetalRoughFactory;
		bool gMetalRoughFactoryRegistered = false;
	}

	//---------------------------------------------------------
	void addMetalRoughLightingSubRenderState(
		Ogre::RTShader::ShaderGenerator * generator,
		Ogre::RTShader::RenderState * renderState)
	{
		oAssert(generator && renderState);
		if(!gMetalRoughFactoryRegistered)
		{
			generator->addSubRenderStateFactory(&gMetalRoughFactory);
			gMetalRoughFactoryRegistered = true;
		}
		renderState->addTemplateSubRenderState(
			generator->createSubRenderState(SRS_METAL_ROUGH_LIGHTING));
	}
}

#endif // USE_RTSHADER_SYSTEM
