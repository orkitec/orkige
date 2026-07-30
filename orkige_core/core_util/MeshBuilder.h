/********************************************************************
	created:	Thursday 2026/07/30 at 09:00
	filename: 	MeshBuilder.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __MeshBuilder_h__30_7_2026__09_00_00__
#define __MeshBuilder_h__30_7_2026__09_00_00__

//! @file MeshBuilder.h
//! @brief the indexed 3D mesh a generator produces and the operators that
//! transform, merge and finish it - pure, headless, renderer-free
//! @remarks Lives in orkige_core beside VectorTessellator (the 2D flat-colour
//! tessellator) and ShapeCollider (planar collision geometry) ON PURPOSE: it is
//! straight geometry math with no Ogre, physics or filesystem dependency, so the
//! unit suite pins every shape's vertex counts, winding, normals and UVs WITHOUT
//! booting a render system. The engine side hands a finished Mesh to
//! `RenderWorld::createMeshFromData`, which turns it into a mesh RESOURCE that
//! then instantiates through `createMeshInstance` exactly like a loaded `.glb` -
//! same PBS `.omat` materials, shadows, static flag and visibility flags.
//!
//! THE VERTEX FORMAT is the one the lit road needs: position, normal, one UV set
//! and a tangent (xyz + handedness w). Every generator fills all four, because a
//! normal-mapping material is refused on a tangent-less mesh and a textured one
//! on a UV-less mesh - a generated mesh that cannot take an authored `.omat`
//! would be a half-feature.
//!
//! SECTIONS are the material split: one section per material name, each a
//! contiguous vertex+index SPAN of the shared arrays (the VectorTessellator::Run
//! precedent - a section's indices address only its own vertex span, so a
//! consumer turns each section into one sub-mesh/draw). A single-material mesh is
//! exactly one section.
//!
//! COORDINATES are right-handed, +Y up, and every closed shape is built with
//! COUNTER-CLOCKWISE front faces seen from outside (outward normals). Shapes are
//! centred on the local origin unless their own documentation says otherwise (a
//! ground plane, a cylinder and a cone all sit centred so `at x y z` places their
//! middle; the blockout solids document their own footprint).
//!
//! DETERMINISM is a contract, not an accident: the same parameters produce
//! byte-identical vertex and index buffers on every run and platform. Nothing
//! here iterates an unordered container, hashes a pointer or reads a clock; the
//! vertex weld sorts on a quantised position key with the original index as the
//! tie-break, so even coincident vertices group in a stable order.
//!
//! REFUSAL is honest and uniform: a non-positive or non-finite extent is an
//! ERROR (the call returns false, leaves the mesh EMPTY and fills the optional
//! error string) because no sensible geometry exists; a segment/ring/step COUNT
//! below the shape's structural minimum or above MAX_SEGMENTS is CLAMPED into
//! range (a friendly reading of `segments 2`, and the ceiling is what keeps
//! `segments 100000000` from trying to allocate the machine). Neither path
//! crashes and neither can emit a NaN.

#include <core_util/String.h>

#include <cstddef>
#include <vector>

namespace Orkige
{
	//! @brief the indexed lit-mesh type plus the pure operators over it
	//! (@see MeshBuilder.h). Static functions only - no state.
	class MeshBuilder
	{
	public:
		//--- Types -------------------------------------------------
		//! a 3D vector in mesh-local units (plain POD - no renderer math, so
		//! this compiles into orkige_core with zero Ogre coupling)
		struct Vec3f
		{
			float x;
			float y;
			float z;
			Vec3f() : x(0.0f), y(0.0f), z(0.0f) {}
			Vec3f(float px, float py, float pz) : x(px), y(py), z(pz) {}
		};
		//! a 2D vector (texture coordinates)
		struct Vec2f
		{
			float x;
			float y;
			Vec2f() : x(0.0f), y(0.0f) {}
			Vec2f(float px, float py) : x(px), y(py) {}
		};
		//! a tangent frame: the tangent direction plus the bitangent
		//! HANDEDNESS in w (+1 or -1), the glTF/Hlms convention
		struct Vec4f
		{
			float x;
			float y;
			float z;
			float w;
			Vec4f() : x(1.0f), y(0.0f), z(0.0f), w(1.0f) {}
			Vec4f(float px, float py, float pz, float pw)
				: x(px), y(py), z(pz), w(pw) {}
		};
		//! one mesh vertex - the lit format (@see MeshBuilder.h)
		struct Vertex
		{
			Vec3f	position;	//!< mesh-local position
			Vec3f	normal;		//!< unit outward normal
			Vec2f	uv;			//!< texture coordinate (v down, the sprite convention)
			Vec4f	tangent;	//!< tangent xyz + bitangent handedness w
		};
		//! one draw/material section: a contiguous vertex+index span of the
		//! mesh's shared arrays bound to one material name. Sections cover the
		//! arrays completely and contiguously, and a section's indices address
		//! only its own vertex span (the VectorTessellator::Run contract), so a
		//! consumer maps each to one sub-mesh.
		struct Section
		{
			String		material;		//!< material name ("" = the mesh default)
			std::size_t	vertexStart;	//!< first vertex of the section
			std::size_t	vertexCount;	//!< vertices in the section
			std::size_t	indexStart;		//!< first index of the section
			std::size_t	indexCount;		//!< indices in the section (3 per triangle)
			Section() : vertexStart(0), vertexCount(0), indexStart(0),
				indexCount(0) {}
		};
		//! a 3D axis-aligned bounds (placement, budget assertions, culling)
		struct Bounds
		{
			Vec3f	minimum;
			Vec3f	maximum;
			bool	valid;	//!< false when no vertex contributed
			Bounds() : valid(false) {}
			//! the box size (zero when invalid)
			Vec3f size() const;
			//! the box centre (origin when invalid)
			Vec3f centre() const;
			//! grow to include one point
			void include(Vec3f const & point);
		};
		//! the built mesh: one vertex array, one triangle index list (3 indices
		//! per triangle) and the per-material sections
		struct Mesh
		{
			std::vector<Vertex>			vertices;
			std::vector<unsigned int>	indices;	//!< 3 per triangle
			std::vector<Section>		sections;	//!< per-material spans, build order
			//! triangle count (indices/3)
			std::size_t triangleCount() const { return this->indices.size() / 3; }
			//! is there any geometry at all
			bool empty() const { return this->indices.empty(); }
			//! drop all geometry and sections
			void clear();
			//! bounds over every vertex position (recomputed on each call)
			Bounds computeBounds() const;
		};

		//! @brief an affine transform as a 3x4 ROW-major matrix: rows 0..2 each
		//! carry a basis row plus that axis' translation
		//! (`m[row][0..2]` = linear part, `m[row][3]` = translation). Enough for
		//! every placement a mesh generator needs and small enough to stay a POD.
		struct Xform
		{
			float m[3][4];
			//! the identity transform
			Xform();
			//! @brief translate/rotate/scale, applied scale-first then rotation
			//! then translation (the authoring order every asset format uses).
			//! Rotation is Euler DEGREES composed as Ry(yaw) * Rx(pitch) *
			//! Rz(roll) - roll about Z first, then pitch about X, then yaw
			//! about Y, which is what a `rotate x y z` directive means.
			static Xform fromTRS(Vec3f const & translation,
				Vec3f const & rotationDegrees, Vec3f const & scale);
			//! @brief this transform followed by @p outer (i.e. outer * this)
			Xform then(Xform const & outer) const;
			//! transform a POSITION (translation applied)
			Vec3f transformPoint(Vec3f const & point) const;
			//! @brief transform a DIRECTION through the inverse-transpose of the
			//! linear part and renormalise - the correct normal transform under
			//! non-uniform scale. A singular linear part leaves the direction
			//! untouched rather than emitting a NaN.
			Vec3f transformNormal(Vec3f const & normal) const;
			//! @brief determinant of the linear part - negative means the
			//! transform MIRRORS, so triangle winding must be flipped to keep
			//! front faces outward (append() does that automatically)
			float linearDeterminant() const;
		};

		//! how a UV strategy projects a position onto texture coordinates
		enum UvMode
		{
			UV_PLANAR_XZ,		//!< project down +Y (a ground/floor plan)
			UV_PLANAR_XY,		//!< project along +Z (a facing wall)
			UV_PLANAR_ZY,		//!< project along +X (a side wall)
			UV_BOX,				//!< per-triangle: whichever planar axis the face normal favours
			UV_CYLINDRICAL,		//!< angle about +Y -> u, height -> v
			UV_SPHERICAL		//!< longitude -> u, latitude -> v
		};

		//--- limits ------------------------------------------------
		//! the structural floor every ring/segment count is clamped up to
		static constexpr unsigned int MIN_SEGMENTS = 3;
		//! @brief the ceiling every count is clamped down to - the guard that
		//! keeps a mistyped `segments 100000000` from trying to allocate the
		//! machine (the largest generator is O(segments x rings) vertices)
		static constexpr unsigned int MAX_SEGMENTS = 512;

		//--- assembly ----------------------------------------------
		//! @brief append @p source into @p destination, transformed by @p place
		//! and attributed to material @p material, as ONE new section (merged
		//! into the preceding section when that section already carries the same
		//! material, so a run of same-material appends stays one draw).
		//! A MIRRORING transform (negative determinant) flips the appended
		//! triangles' winding so front faces stay outward. Normals are carried
		//! through the inverse-transpose, tangents through the linear part.
		//! Appending an empty source is a no-op.
		static void append(Mesh & destination, Mesh const & source,
			Xform const & place, String const & material);
		//! @brief append @p source verbatim (identity transform), keeping the
		//! SOURCE's own section materials instead of overriding them - the merge
		//! that preserves an already multi-material mesh
		static void appendSections(Mesh & destination, Mesh const & source,
			Xform const & place);
		//! @brief transform every vertex of @p mesh in place (positions,
		//! normals and tangents), flipping winding under a mirroring transform
		static void transform(Mesh & mesh, Xform const & place);

		//--- finishing ---------------------------------------------
		//! @brief replace every normal with its triangle's geometric face
		//! normal, SPLITTING shared vertices so each triangle owns its three
		//! (the faceted look; vertex/index counts become 3 per triangle).
		//! Sections and UVs ride along. Degenerate triangles keep a unit +Y
		//! normal rather than a NaN.
		static void computeFlatNormals(Mesh & mesh);
		//! @brief recompute normals by averaging the area-weighted face normals
		//! of the triangles meeting at each vertex, welding only vertices that
		//! share a position (within @p weldTolerance) AND whose faces meet at an
		//! angle below @p smoothAngleDegrees - so a box keeps its crisp edges
		//! while a sphere's seam vertices average smoothly. Topology is
		//! untouched (no vertex is added or removed). A degenerate triangle
		//! contributes nothing.
		static void computeSmoothNormals(Mesh & mesh,
			float smoothAngleDegrees = 60.0f, float weldTolerance = 1.0e-5f);
		//! @brief (re)project every vertex's UV with the given strategy.
		//! @p scale multiplies the projected coordinate (tiling); a scale
		//! component of 0 is treated as 1. UV_BOX picks the dominant axis of
		//! each vertex's NORMAL, so it needs normals already computed.
		//! Positions are normalised against the mesh bounds for the planar and
		//! cylindrical modes, so the result lands in 0..1 for a mesh that fills
		//! its bounds (times the tiling scale).
		static void applyUV(Mesh & mesh, UvMode mode,
			Vec2f const & scale = Vec2f(1.0f, 1.0f));
		//! @brief compute per-vertex tangents from the positions, normals and
		//! UVs (accumulate per triangle, Gram-Schmidt orthogonalise against the
		//! normal, handedness in w). A vertex whose UV gradient is degenerate
		//! gets an arbitrary but FINITE frame perpendicular to its normal -
		//! never a NaN, never a zero tangent (the Hlms rejects those).
		static void computeTangents(Mesh & mesh);

		//--- helpers -----------------------------------------------
		//! @brief begin a new section for material @p material at the mesh's
		//! current end (or return the last section when it already carries this
		//! material). Generators call this before emitting their geometry; the
		//! section's counts are closed by closeSection.
		static Section & openSection(Mesh & mesh, String const & material);
		//! @brief close the mesh's last section against the current array ends
		//! (idempotent). A mesh whose sections a generator maintained by hand
		//! needs this before it is handed on.
		static void closeSection(Mesh & mesh);
		//! @brief is every section span in range, contiguous and complete, is
		//! the index count a multiple of 3, does every index address its own
		//! section's vertex span, and is every position/normal/uv finite? The
		//! invariant the unit suite and the facade entry both assert.
		static bool validate(Mesh const & mesh, String * outError = NULL);
		//! clamp a count into [MIN_SEGMENTS; MAX_SEGMENTS] (@see MeshBuilder.h)
		static unsigned int clampSegments(int requested,
			unsigned int minimum = MIN_SEGMENTS);
		//! @brief is @p value finite and strictly positive - the extent gate
		//! every generator runs before it allocates (@see MeshBuilder.h)
		static bool isPositiveExtent(float value);
		//! is @p value finite (not NaN, not infinite)
		static bool isFinite(float value);

		//--- small vector math (POD, exposed for the generators + tests) ---
		static Vec3f add(Vec3f const & a, Vec3f const & b);
		static Vec3f subtract(Vec3f const & a, Vec3f const & b);
		static Vec3f scale(Vec3f const & a, float factor);
		static float dot(Vec3f const & a, Vec3f const & b);
		static Vec3f cross(Vec3f const & a, Vec3f const & b);
		static float length(Vec3f const & a);
		//! @brief unit-length @p a, or @p fallback when @p a is (near) zero -
		//! the never-NaN normalise every generator uses
		static Vec3f normalise(Vec3f const & a,
			Vec3f const & fallback = Vec3f(0.0f, 1.0f, 0.0f));
	};
}

#endif //__MeshBuilder_h__30_7_2026__09_00_00__
