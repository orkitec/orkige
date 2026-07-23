/********************************************************************
	created:	Thursday 2026/07/23 at 12:00
	filename: 	ImageLightingSrs.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file ImageLightingSrs.cpp
//! @brief the engine-owned image-based-lighting sub-render-state
//! (@see ImageLightingSrs.h). The program-building code is derived from
//! OGRE's stock image-based-lighting stage (MIT licensed); the response
//! lives in the engine shader library's Orkige_ImageLighting
//! (media/rtss/OrkigeLib_MetalRough.glsl), which evaluates the other
//! backend's live env term instead of the stock split-sum lookup.

#include "engine_render_classic/ImageLightingSrs.h"

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
		const Ogre::String SRS_ORKIGE_IMAGE_LIGHTING = "OrkigeImageLighting";

		//! the engine shader library carrying the env-term response (the same
		//! library the metal-rough lighting stage depends on - PixelParams and
		//! Orkige_ImageLighting live there)
		const char* const METAL_ROUGH_LIB = "OrkigeLib_MetalRough";

		//---------------------------------------------------------
		//! binds the environment chain cubemap (RAW - the chain stores clamped
		//! linear radiance, so no hardware-sRGB view) and calls the library's
		//! Orkige_ImageLighting with the metal-rough stage's PixelParams; the
		//! fill adds linearly and the shared display transfer encodes it.
		class ImageLighting : public SubRenderState
		{
		public:
			const Ogre::String & getType() const override
			{
				return SRS_ORKIGE_IMAGE_LIGHTING;
			}

			//! right after the metal-rough lighting stage (FFP_LIGHTING), before
			//! the hemisphere ambient (FFP_LIGHTING + 20) - the stock stage's slot
			int getExecutionOrder() const override { return FFP_LIGHTING + 10; }

			bool setParameter(const Ogre::String & name,
				const Ogre::String & value) override
			{
				if(name == "texture" && !value.empty())
				{
					this->mEnvMapName = value;
					return true;
				}
				if(name == "luminance")
				{
					this->mLuminanceDirty = true;
					return StringConverter::parse(value, this->mLuminance);
				}
				return false;
			}

			void copyFrom(const SubRenderState & rhs) override
			{
				const ImageLighting & other =
					static_cast<const ImageLighting &>(rhs);
				this->mEnvMapName = other.mEnvMapName;
				this->mLuminance = other.mLuminance;
			}

			bool preAddToRenderState(const RenderState *, Ogre::Pass * srcPass,
				Ogre::Pass * dstPass) override
			{
				if(!srcPass->getLightingEnabled())
				{
					return false;
				}
				// the generated program indexes the chain's mip levels per
				// fragment, which needs GLSL ES 3.0 on a GLES target (the
				// engine-level capability probe refuses earlier; this guard
				// keeps the stage honest if reached directly)
				if(ShaderGenerator::getSingleton().getTargetLanguage() ==
					"glsles" &&
					!GpuProgramManager::getSingleton().isSyntaxSupported(
						"glsl300es"))
				{
					return false;
				}
				// ONE texture unit: the environment chain, sampled raw (its
				// texels are clamped linear radiance - a hardware-sRGB view
				// would decode-crush them; the other backend samples the same
				// bytes raw)
				TextureUnitState* tus = dstPass->createTextureUnitState();
				tus->setTextureName(this->mEnvMapName, TEX_TYPE_CUBE_MAP);
				this->mEnvMapSamplerIndex =
					dstPass->getNumTextureUnitStates() - 1;
				return true;
			}

			bool createCpuSubPrograms(ProgramSet * programSet) override;

			void updateGpuProgramsParams(Ogre::Renderable *, const Ogre::Pass *,
				const Ogre::AutoParamDataSource *,
				const Ogre::LightList *) override
			{
				if(this->mLuminanceDirty && this->mLuminanceParam)
				{
					this->mLuminanceParam->setGpuParameter(this->mLuminance);
					this->mLuminanceDirty = false;
				}
			}

		private:
			Ogre::String		mEnvMapName;			//!< environment chain cubemap
			float				mLuminance = 1.0f;		//!< effective env scale
			bool				mLuminanceDirty = true;	//!< push pending?
			int					mEnvMapSamplerIndex = 0;//!< bound chain unit
			UniformParameterPtr	mLuminanceParam;		//!< the shader uniform
		};

		//---------------------------------------------------------
		bool ImageLighting::createCpuSubPrograms(ProgramSet * programSet)
		{
			Program * vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
			Function * vsMain = vsProgram->getEntryPointFunction();
			Program * psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
			Function * psMain = psProgram->getEntryPointFunction();

			// the metal-rough lighting stage (lower execution order, built
			// first) left its PixelParams local; this stage is only ever added
			// alongside it (@see configureSurfaceShaderState) - a missing local
			// means that pairing regressed: say so, add nothing
			auto pixel = psMain->getLocalParameter("pixel");
			if(!pixel)
			{
				oDebugWarning(false, "image lighting: the lighting stage left "
					"no 'pixel' params local - the environment fill is skipped");
				return true;
			}

			auto vsOutViewPos =
				vsMain->resolveOutputParameter(Parameter::SPC_POSITION_VIEW_SPACE);
			auto viewPos = psMain->resolveInputParameter(vsOutViewPos);

			// the view-space normal the lighting stage shades with (normal-
			// mapped if the normal-map stage wrote one)
			auto viewNormal =
				psMain->getLocalParameter(Parameter::SPC_NORMAL_VIEW_SPACE);
			if(!viewNormal)
			{
				auto vsOutNormal = vsMain->resolveOutputParameter(
					Parameter::SPC_NORMAL_VIEW_SPACE);
				viewNormal = psMain->resolveInputParameter(vsOutNormal);
			}

			psProgram->addDependency(METAL_ROUGH_LIB);

			auto outDiffuse =
				psMain->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);
			auto envSampler = psProgram->resolveParameter(GCT_SAMPLERCUBE,
				"orkigeIblEnvSampler", this->mEnvMapSamplerIndex);
			// .w = the chain's mip count EXCLUDING the base level (the classic
			// texture-size convention; the shader adds the base back to run the
			// other backend's inclusive-count lod map)
			auto envSize = psProgram->resolveParameter(
				GpuProgramParameters::ACT_TEXTURE_SIZE,
				this->mEnvMapSamplerIndex);
			auto invViewMat = psProgram->resolveParameter(
				GpuProgramParameters::ACT_INVERSE_VIEW_MATRIX);
			this->mLuminanceParam = psProgram->resolveParameter(GCT_FLOAT1,
				"orkigeIblLuminance");
			this->mLuminanceDirty = true;

			// between the lighting stage's parameter build and its light loop -
			// every term adds linearly, so the slot only needs PixelParams ready
			auto stage = psMain->getStage(FFP_PS_PBR_LIGHTING_BEGIN + 5);
			stage.callFunction("Orkige_ImageLighting",
				{ In(pixel), In(viewNormal), In(viewPos), In(invViewMat),
				  In(envSampler), In(envSize).w(), In(mLuminanceParam),
				  InOut(outDiffuse).xyz() });
			return true;
		}

		//---------------------------------------------------------
		//! the factory the generator clones per-material instances through
		class ImageLightingFactory : public SubRenderStateFactory
		{
		public:
			const Ogre::String & getType() const override
			{
				return SRS_ORKIGE_IMAGE_LIGHTING;
			}

		protected:
			SubRenderState * createInstanceImpl() override
			{
				return OGRE_NEW ImageLighting;
			}
		};

		//! one factory for the process, registered on first use and owned here
		//! (kept alive for the generator's lifetime, like OGRE's own factories)
		ImageLightingFactory gImageLightingFactory;
		bool gImageLightingFactoryRegistered = false;
	}

	//---------------------------------------------------------
	void addImageLightingSubRenderState(
		Ogre::RTShader::ShaderGenerator * generator,
		Ogre::RTShader::RenderState * renderState,
		Ogre::String const & envTexture, float luminance)
	{
		oAssert(generator && renderState);
		if(!gImageLightingFactoryRegistered)
		{
			generator->addSubRenderStateFactory(&gImageLightingFactory);
			gImageLightingFactoryRegistered = true;
		}
		Ogre::RTShader::SubRenderState* state =
			generator->createSubRenderState(SRS_ORKIGE_IMAGE_LIGHTING);
		state->setParameter("texture", envTexture);
		state->setParameter("luminance",
			Ogre::StringConverter::toString(luminance));
		renderState->addTemplateSubRenderState(state);
	}
}

#endif // USE_RTSHADER_SYSTEM
