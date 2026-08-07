/********************************************************************
	created:	Thursday 2026/08/07 at 12:00
	filename: 	DecalComposeSrs.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file DecalComposeSrs.cpp
//! @brief the engine-owned decal-coverage sub-render-state
//! (@see DecalComposeSrs.h). Three stage operations at the post-process slot -
//! the whole response is the display transfer's own inverse, so it needs no
//! shader library and no uniform.

#include "engine_render_classic/DecalComposeSrs.h"

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
		const Ogre::String SRS_DECAL_COMPOSE = "OrkigeDecalCompose";

		//---------------------------------------------------------
		//! rewrites the fragment's alpha to a' = 1 - sqrt(1 - a), the coverage
		//! that removes from a DISPLAY-ENCODED destination exactly the fraction
		//! of LINEAR radiance the other backend's projected decal removes from
		//! the surface before encoding (@see the header for the derivation)
		class DecalCompose : public SubRenderState
		{
		public:
			const Ogre::String & getType() const override
			{
				return SRS_DECAL_COMPOSE;
			}

			//! the post-process slot: after texturing composed the texture
			//! alpha with the quad's vertex alpha, before the blend consumes it
			int getExecutionOrder() const override { return FFP_POST_PROCESS; }

			void copyFrom(const SubRenderState &) override {}

			bool createCpuSubPrograms(ProgramSet * programSet) override;
		};

		//---------------------------------------------------------
		bool DecalCompose::createCpuSubPrograms(ProgramSet * programSet)
		{
			Program * psProgram = programSet->getCpuProgram(GPT_FRAGMENT_PROGRAM);
			Function * psMain = psProgram->getEntryPointFunction();
			auto outDiffuse =
				psMain->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);
			auto survival = psMain->resolveLocalParameter(GCT_FLOAT1,
				"orkigeDecalSurvival");

			auto stage = psMain->getStage(FFP_PS_POST_PROCESS);
			// survival = 1 - a, clamped at zero so the root stays real for any
			// alpha a filtered texture can produce
			stage.sub(1.0f, In(outDiffuse).w(), survival);
			stage.callBuiltin("max", In(survival), In(0.0f), Out(survival));
			// the display transfer of the linear survival, and back to coverage
			stage.callBuiltin("sqrt", In(survival), Out(survival));
			stage.sub(1.0f, In(survival), Out(outDiffuse).w());
			return true;
		}

		//---------------------------------------------------------
		//! the factory the generator clones per-material instances through
		class DecalComposeFactory : public SubRenderStateFactory
		{
		public:
			const Ogre::String & getType() const override
			{
				return SRS_DECAL_COMPOSE;
			}

		protected:
			SubRenderState * createInstanceImpl() override
			{
				return OGRE_NEW DecalCompose;
			}
		};

		//! one factory for the process, registered on first use and owned here
		//! (kept alive for the generator's lifetime, like OGRE's own factories)
		DecalComposeFactory gDecalComposeFactory;
		bool gDecalComposeFactoryRegistered = false;
	}

	//---------------------------------------------------------
	void addDecalComposeSubRenderState(
		Ogre::RTShader::ShaderGenerator * generator,
		Ogre::RTShader::RenderState * renderState)
	{
		oAssert(generator && renderState);
		if(!gDecalComposeFactoryRegistered)
		{
			generator->addSubRenderStateFactory(&gDecalComposeFactory);
			gDecalComposeFactoryRegistered = true;
		}
		renderState->addTemplateSubRenderState(
			generator->createSubRenderState(SRS_DECAL_COMPOSE));
	}
}

#endif // USE_RTSHADER_SYSTEM
