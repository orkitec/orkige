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

		//! the sub-render-state type name (the factory + createSubRenderState key)
		const Ogre::String SRS_HEMISPHERE_AMBIENT = "OrkigeHemisphereAmbient";

		//---------------------------------------------------------
		//! evaluates mix(lower, upper, dot(hemisphereDir, N)*0.5+0.5) per pixel
		//! and adds it, scaled by the surface diffuse reflectance, to the lit
		//! output - the classic mirror of the HlmsPbs ambient-hemisphere term.
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

			// the Cook-Torrance stage (lower execution order, so it built first)
			// left the surface albedo in the 'baseColor' local; this stage is only
			// ever added alongside it (@see addHemisphereAmbientSubRenderState), so
			// a missing local means that pairing regressed - say so, add nothing
			auto baseColor = psMain->getLocalParameter("baseColor");
			if(!baseColor)
			{
				oDebugWarning(false, "hemisphere ambient: the lighting stage left "
					"no 'baseColor' local - the ambient fill is skipped");
				return true;
			}
			// metalness rides ormParams.z (occlusion/roughness/metalness); a pure
			// metal has no diffuse ambient, matching next's pixelData.diffuse
			auto ormParams = psMain->getLocalParameter("ormParams");

			// the view-space normal the lighting stage shades with (normal-mapped
			// if the normal-map stage wrote one) - resolved the way the
			// image-based-lighting stage resolves it
			auto viewNormal =
				psMain->getLocalParameter(Parameter::SPC_NORMAL_VIEW_SPACE);
			if(!viewNormal)
			{
				auto vsOutNormal =
					vsMain->resolveOutputParameter(Parameter::SPC_NORMAL_VIEW_SPACE);
				viewNormal = psMain->resolveInputParameter(vsOutNormal);
			}

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

			auto normal = psMain->resolveLocalParameter(GCT_FLOAT3, "orkHemiNormal");
			auto weight = psMain->resolveLocalParameter(GCT_FLOAT1, "orkHemiWeight");
			auto ambient = psMain->resolveLocalParameter(GCT_FLOAT3, "orkHemiColour");
			auto albedo = psMain->resolveLocalParameter(GCT_FLOAT3, "orkHemiAlbedo");
			auto diffuse = psMain->resolveLocalParameter(GCT_FLOAT3, "orkHemiDiffuse");

			// right after the Cook-Torrance lighting stage
			auto stage = psMain->getStage(FFP_PS_PBR_LIGHTING_END + 1);

			// weight = dot(hemisphereDir, normalize(N)) * 0.5 + 0.5
			stage.callBuiltin("normalize", In(viewNormal), Out(normal));
			stage.callBuiltin("dot", In(mHemiDirView), In(normal), Out(weight));
			stage.mul(In(weight), In(0.5f), Out(weight));
			stage.add(In(weight), In(0.5f), Out(weight));

			// ambient = mix(lower, upper, weight) == lower + (upper - lower)*weight
			// built from portable operators so it emits on glsl AND glsles
			stage.sub(In(mUpperHemi), In(mLowerHemi), Out(ambient));
			stage.mul(In(ambient), In(weight), Out(ambient));
			stage.add(In(ambient), In(mLowerHemi), Out(ambient));

			// the reflectance the sky ambient fills. Next's HlmsPbs consumes a
			// FLAT material albedo / a raw (non-sRGB) texture in DISPLAY space for
			// the ambient-hemisphere term, while classic's Cook-Torrance baseColor
			// is linearised (USE_LINEAR_COLOURS): recover the display-space albedo
			// so the ISOLATED ambient response matches next (the hemisphere probe -
			// linear baseColor read HALF next's ambient at mid greys, closing
			// toward parity only near white). For real sRGB-textured content both
			// flavors already linearise the sample, so on a scene where direct
			// light dominates this recovery is a small fraction of the pixel and
			// leaves lake/vista/cutout byte-close either way; the residual gap it
			// closes is the flat-colour albedo-space difference the two flavors
			// carry surface-wide (the same one the direct path compensates for).
			stage.callBuiltin("pow", In(baseColor),
				In(Ogre::Vector3(1.0f / 2.2f)), Out(albedo));

			// diffuse = albedo * (1 - metalness): the metal-aware reflectance the
			// sky ambient fills (next scales its envColourD by pixelData.diffuse)
			if(ormParams)
			{
				auto kd = psMain->resolveLocalParameter(GCT_FLOAT1, "orkHemiKd");
				stage.sub(In(1.0f), In(ormParams).z(), Out(kd));
				stage.mul(In(albedo), In(kd), Out(diffuse));
			}
			else
			{
				stage.assign(In(albedo), Out(diffuse));
			}

			// outColour.rgb += diffuse * ambient (added over the analytic lighting)
			stage.mul(In(diffuse), In(ambient), Out(diffuse));
			stage.add(In(outColour).xyz(), In(diffuse), Out(outColour).xyz());
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
				// next rotates the world hemisphere direction (up, UNIT_Y) into
				// view space and normalises it; the shader dots it with the
				// view-space normal, so the split is frame-invariant
				Ogre::Vector3 dir =
					source->getViewMatrix().linear() * Ogre::Vector3::UNIT_Y;
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
