/********************************************************************
	created:	Wednesday 2026/07/22 at 12:00
	filename: 	HemisphereAmbientSrs.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file HemisphereAmbientSrs.cpp
//! @brief the per-pixel two-colour hemisphere ambient RTSS sub-render-state
//! (@see HemisphereAmbientSrs.h). Mirrors the Ogre-Next HlmsPbs
//! ambient-hemisphere response so a surface's ambient fill reads the sky colour
//! on up-facing normals and the ground colour on down-facing ones, on BOTH
//! flavors, instead of one flat average.

#include "engine_render_classic/HemisphereAmbientSrs.h"

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

		//! the live sky/ground colours (linear), pushed to every generated
		//! surface shader each frame. One global state: the whole scene shares
		//! one hemisphere ambient, exactly like the flat scene-ambient it
		//! replaces (@see noteHemisphereAmbientColours).
		Ogre::ColourValue gHemisphereUpper(0.2f, 0.2f, 0.2f, 1.0f);
		Ogre::ColourValue gHemisphereLower(0.2f, 0.2f, 0.2f, 1.0f);
		//! the WORLD-space hemisphere axis the sky/ground split blends around:
		//! straight up for the authored ambient (matching the other backend's
		//! setAmbientHemisphere), TILTED toward the mirrored sun while an
		//! atmosphere drives the fill (its native linkage points the axis at
		//! normalize(up + halfTurnAboutUp(toSun)), so horizon-facing surfaces
		//! read the warm horizon band instead of the zenith)
		Ogre::Vector3 gHemisphereDir(Ogre::Vector3::UNIT_Y);

		//! the sub-render-state type name (the factory + createSubRenderState key)
		const Ogre::String SRS_HEMISPHERE_AMBIENT = "OrkigeHemisphereAmbient";

		//! the engine shader library carrying the hemisphere response (the same
		//! library the metal-rough lighting stage depends on - PixelParams and
		//! Orkige_HemisphereAmbient live there)
		const char* const METAL_ROUGH_LIB = "OrkigeLib_MetalRough";

		//---------------------------------------------------------
		//! calls the library's Orkige_HemisphereAmbient with the metal-rough
		//! stage's PixelParams: the diffuse hemisphere fill PLUS the specular
		//! hemisphere lane (the fresnelS-weighted env-specular add) - the
		//! classic mirror of the full HlmsPbs ambient-hemisphere term.
		class HemisphereAmbient : public SubRenderState
		{
		public:
			const Ogre::String & getType() const override
			{
				return SRS_HEMISPHERE_AMBIENT;
			}

			//! after the Cook-Torrance lighting stage (FFP_LIGHTING) and the
			//! image-based-lighting stage (FFP_LIGHTING + 10): the ambient fill
			//! rides on top of the direct + environment lighting, never shadowed
			int getExecutionOrder() const override { return FFP_LIGHTING + 20; }

			//! no per-instance authored state - the colours are global and the
			//! shader parameters are resolved fresh on every program build
			void copyFrom(const SubRenderState &) override {}

			bool createCpuSubPrograms(ProgramSet * programSet) override;

			void updateGpuProgramsParams(Ogre::Renderable * rend,
				const Ogre::Pass * pass, const Ogre::AutoParamDataSource * source,
				const Ogre::LightList * lightList) override;

		private:
			UniformParameterPtr	mUpperHemi;		//!< sky colour (linear)
			UniformParameterPtr	mLowerHemi;		//!< ground colour (linear)
			UniformParameterPtr	mHemiDirView;	//!< world up, rotated to view space
		};

		//---------------------------------------------------------
		bool HemisphereAmbient::createCpuSubPrograms(ProgramSet * programSet)
		{
			Program * vsProgram = programSet->getCpuProgram(GPT_VERTEX_PROGRAM);
			Function * vsMain = vsProgram->getEntryPointFunction();
			Program * psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
			Function * psMain = psProgram->getEntryPointFunction();

			// the metal-rough lighting stage (lower execution order, so it built
			// first) left its PixelParams local (raw albedo, metal-aware diffuse
			// reflectance, f0, perceptual roughness - everything both hemisphere
			// lanes consume); this stage is only ever added alongside it
			// (@see addHemisphereAmbientSubRenderState), so a missing local means
			// that pairing regressed - say so, add nothing
			auto pixel = psMain->getLocalParameter("pixel");
			if(!pixel)
			{
				oDebugWarning(false, "hemisphere ambient: the lighting stage left "
					"no 'pixel' params local - the ambient fill is skipped");
				return true;
			}

			// the view-space position (the specular lane needs the view direction
			// for reflDir + NoV) - resolved the way the image-based-lighting
			// stage resolves it
			auto vsOutViewPos =
				vsMain->resolveOutputParameter(Parameter::SPC_POSITION_VIEW_SPACE);
			auto viewPos = psMain->resolveInputParameter(vsOutViewPos);

			// the view-space normal the lighting stage shades with (normal-mapped
			// if the normal-map stage wrote one)
			auto viewNormal =
				psMain->getLocalParameter(Parameter::SPC_NORMAL_VIEW_SPACE);
			if(!viewNormal)
			{
				auto vsOutNormal =
					vsMain->resolveOutputParameter(Parameter::SPC_NORMAL_VIEW_SPACE);
				viewNormal = psMain->resolveInputParameter(vsOutNormal);
			}

			psProgram->addDependency(METAL_ROUGH_LIB);

			auto outColour =
				psMain->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);

			// per-frame uniforms, pushed to the DRAWN generated pass in
			// updateGpuProgramsParams (never a hand-push to a non-rendered pass)
			mUpperHemi =
				psProgram->resolveParameter(GCT_FLOAT3, "orkigeAmbientUpperHemi");
			mLowerHemi =
				psProgram->resolveParameter(GCT_FLOAT3, "orkigeAmbientLowerHemi");
			mHemiDirView =
				psProgram->resolveParameter(GCT_FLOAT3, "orkigeAmbientHemiDirView");

			// right after the Cook-Torrance lighting stage: both hemisphere lanes
			// (diffuse fill + fresnelS-weighted specular) in one library call -
			// the response lives in Orkige_HemisphereAmbient
			// (media/rtss/OrkigeLib_MetalRough.glsl), shared by both GLSL profiles
			auto stage = psMain->getStage(FFP_PS_PBR_LIGHTING_END + 1);
			stage.callFunction("Orkige_HemisphereAmbient",
				{ In(pixel), In(viewNormal), In(viewPos), In(mHemiDirView),
				  In(mUpperHemi), In(mLowerHemi), InOut(outColour).xyz() });
			return true;
		}

		//---------------------------------------------------------
		void HemisphereAmbient::updateGpuProgramsParams(Ogre::Renderable *,
			const Ogre::Pass *, const Ogre::AutoParamDataSource * source,
			const Ogre::LightList *)
		{
			if(mUpperHemi)
			{
				mUpperHemi->setGpuParameter(Ogre::Vector3(gHemisphereUpper.r,
					gHemisphereUpper.g, gHemisphereUpper.b));
			}
			if(mLowerHemi)
			{
				mLowerHemi->setGpuParameter(Ogre::Vector3(gHemisphereLower.r,
					gHemisphereLower.g, gHemisphereLower.b));
			}
			if(mHemiDirView && source)
			{
				// next rotates the world hemisphere axis into view space and
				// normalises it; the shader dots it with the view-space normal,
				// so the split is frame-invariant
				Ogre::Vector3 dir =
					source->getViewMatrix().linear() * gHemisphereDir;
				dir.normalise();
				mHemiDirView->setGpuParameter(dir);
			}
		}

		//---------------------------------------------------------
		//! the factory the generator clones per-material instances through
		class HemisphereAmbientFactory : public SubRenderStateFactory
		{
		public:
			const Ogre::String & getType() const override
			{
				return SRS_HEMISPHERE_AMBIENT;
			}

		protected:
			SubRenderState * createInstanceImpl() override
			{
				return OGRE_NEW HemisphereAmbient;
			}
		};

		//! one factory for the process, registered on first use and owned here
		//! (kept alive for the generator's lifetime, like OGRE's own factories)
		HemisphereAmbientFactory gHemisphereFactory;
		bool gHemisphereFactoryRegistered = false;
	}

	//---------------------------------------------------------
	void noteHemisphereAmbientColours(Ogre::ColourValue const & upperHemisphere,
		Ogre::ColourValue const & lowerHemisphere)
	{
		gHemisphereUpper = upperHemisphere;
		gHemisphereLower = lowerHemisphere;
		gHemisphereDir = Ogre::Vector3::UNIT_Y;
	}
	//---------------------------------------------------------
	void noteHemisphereAmbientColours(Ogre::ColourValue const & upperHemisphere,
		Ogre::ColourValue const & lowerHemisphere,
		Ogre::Vector3 const & worldDirection)
	{
		gHemisphereUpper = upperHemisphere;
		gHemisphereLower = lowerHemisphere;
		gHemisphereDir = worldDirection;
		gHemisphereDir.normalise();
	}

	//---------------------------------------------------------
	void hemisphereAmbientColours(Ogre::ColourValue & outUpper,
		Ogre::ColourValue & outLower)
	{
		outUpper = gHemisphereUpper;
		outLower = gHemisphereLower;
	}

	//---------------------------------------------------------
	void addHemisphereAmbientSubRenderState(
		Ogre::RTShader::ShaderGenerator * generator,
		Ogre::RTShader::RenderState * renderState)
	{
		oAssert(generator && renderState);
		if(!gHemisphereFactoryRegistered)
		{
			generator->addSubRenderStateFactory(&gHemisphereFactory);
			gHemisphereFactoryRegistered = true;
		}
		renderState->addTemplateSubRenderState(
			generator->createSubRenderState(SRS_HEMISPHERE_AMBIENT));
	}
}

#endif // USE_RTSHADER_SYSTEM
