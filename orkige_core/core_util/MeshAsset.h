/********************************************************************
	created:	Thursday 2026/07/30 at 09:30
	filename: 	MeshAsset.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __MeshAsset_h__30_7_2026__09_30_00__
#define __MeshAsset_h__30_7_2026__09_30_00__

//! @file MeshAsset.h
//! @brief parser for the lean, agent-authorable `.omesh` text asset - a
//! parametric 3D mesh described in one diffable text file
//! @remarks A `.omesh` is a LIST OF PLACED SHAPES. It is plain text an agent
//! writes over write_project_file (or a designer types), it needs no binary
//! asset and no offline tool, and it is parsed pure and headless here
//! (orkige_core, unit-tested without a renderer) into a MeshBuilder::Mesh. The
//! engine side turns that into a real mesh RESOURCE, so `ModelComponent.mesh`
//! accepts a `.omesh` exactly like a `.glb`.
//!
//! GRAMMAR (v1) - one directive per LINE, `#` starts a line comment, tokens are
//! whitespace-separated and keywords are case-insensitive:
//!
//!   version 1                     optional; must be the FIRST directive, and
//!                                 only version 1 is accepted
//!   material NAME                 on its OWN line: the material every later
//!                                 shape uses until the next such line
//!
//! Every other line is ONE SHAPE. A shape's SIZE is positional where it is a
//! box-like extent triple and NAMED where it is a radius/height:
//!
//!   box        SX SY SZ
//!   roundedbox SX SY SZ  radius R [segments N]
//!   plane      SX SZ     [segments NX NZ]
//!   wedge      SX SY SZ
//!   stairs     SX SY SZ  [steps N]
//!   sphere     radius R  [segments N] [rings N]
//!   icosphere  radius R  [subdivisions N]
//!   cylinder   radius R height H  [segments N] [caps 0|1]
//!   cone       radius R height H  [segments N] [caps 0|1]
//!   capsule    radius R height H  [segments N] [rings N]
//!   torus      radius R tube T    [segments N] [tubesegments N]
//!   tube       radius R inner I height H  [segments N] [caps 0|1]
//!   disc       radius R  [inner I] [segments N]
//!   arch       span W legs H thickness T depth D  [segments N]
//!   extrude    shape FILE.oshape depth D  [smoothsides]
//!   revolve    shape FILE.oshape  [segments N] [sweep DEGREES]
//!
//! and EVERY shape line may carry these trailing modifiers, in any order:
//!
//!   at X Y Z                      translation (default 0 0 0)
//!   rotate X Y Z                  Euler DEGREES, Ry * Rx * Rz
//!   scale S | scale SX SY SZ      scale about the shape's own origin
//!   material NAME                 this shape's material, overriding the
//!                                 current default
//!   uv MODE [SU SV]               re-project the UVs: MODE is one of
//!                                 xz / xy / zy / box / cylindrical /
//!                                 spherical, with an optional tiling scale
//!   smooth [ANGLE]                re-average the normals, welding faces that
//!                                 meet below ANGLE degrees (default 60)
//!   flat                          replace the normals with face normals
//!
//! MATERIALS: a `material NAME` value is an ASSET REFERENCE stored verbatim in
//! the produced mesh's section (@see MeshBuilder::Section::material); the engine
//! resolves a bare NAME as `NAME.omat`. Shapes sharing a material MERGE into one
//! section (one draw), so the section order is the order materials first appear.
//!
//! WHAT IS DELIBERATELY ABSENT (v1): booleans (union/subtract), arrays/loops,
//! variables, expressions and per-vertex colour. Each is a real feature with a
//! real cost; the grammar stays a flat, diffable placed-shape list until one of
//! them earns its keep. UNLIKE `.oshape` (which reserves future keywords), an
//! unknown directive or an unknown modifier key is an ERROR - a typo silently
//! ignored would misrender without a trace, and the editor's live diagnostics
//! turn the reported line into a clickable marker.

#include "core_util/MeshBuilder.h"
#include "core_util/VectorTessellator.h"

#include <core_util/String.h>
#include <vector>

namespace Orkige
{
	//! @brief the `.omesh` text -> indexed mesh front end (pure, headless)
	class MeshAsset
	{
	public:
		//! @brief how the parser reaches an `.oshape` referenced by an
		//! `extrude`/`revolve` line. The parser itself never touches a
		//! filesystem (that keeps it pure and unit-testable); the engine passes
		//! an implementation backed by the resource system, and a
		//! `.omesh` that references a shape with NO source available fails
		//! honestly with the reference in the message.
		struct ShapeSource
		{
			virtual ~ShapeSource() {}
			//! @brief resolve @p name to a region list; false = unavailable
			//! (the parser then reports the missing reference on its line)
			virtual bool loadShape(String const & name,
				std::vector<VectorTessellator::Region> & outRegions) const = 0;
		};

		//! @brief parse `.omesh` text into a finished multi-section mesh.
		//! @param shapes resolver for `extrude`/`revolve` references; NULL means
		//! no shape can be resolved (a text-only `.omesh` still parses)
		//! @return true on a well-formed asset that produced geometry. On ANY
		//! malformation (unknown directive/key, missing or non-numeric value,
		//! duplicate key, trailing garbage, unsupported version, a refused
		//! shape, an unresolvable shape reference, no shape at all) it returns
		//! false, leaves @p out EMPTY and describes the problem in @p outError
		//! as "line N: ..." when one is passed - the same shape
		//! `MaterialAsset::parse` reports, so the editor's line-numbered
		//! diagnostics read it with no new plumbing.
		static bool parse(String const & text, MeshBuilder::Mesh & out,
			ShapeSource const * shapes = NULL, String * outError = NULL);

		//! @brief validate `.omesh` text WITHOUT resolving its asset references -
		//! the editor's live-diagnostics entry (the `ScriptRuntime::checkSyntax`
		//! seam name, same job). Every directive, key, value and shape refusal is
		//! checked by actually building the geometry, with `extrude`/`revolve`
		//! standing in a placeholder outline; a `shape` reference that does not
		//! exist is therefore NOT reported here - the editor cannot resolve
		//! project assets from a pure function, and a missing reference surfaces
		//! at load time the way a missing `.omat` does.
		//! @return true when the text is well-formed; otherwise false with
		//! "line N: ..." in @p outError
		static bool checkSyntax(String const & text, String * outError = NULL);

		//! @brief the `.oshape` references a `.omesh` text depends on, in first
		//! appearance order (an exporter/asset-database probe that needs the
		//! dependency set without building geometry). Malformed lines are
		//! skipped rather than reported - this is a scan, not a validation.
		static StringVector shapeReferences(String const & text);

		//! is @p fileName a `.omesh` asset name (case-insensitive extension)
		static bool isMeshAssetName(String const & fileName);
	};
}

#endif //__MeshAsset_h__30_7_2026__09_30_00__
