/********************************************************************
	created:	Thursday 2026/07/30 at 09:10
	filename: 	MeshShapes.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "core_util/MeshShapes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace Orkige
{
	namespace
	{
		typedef MeshBuilder::Mesh Mesh;
		typedef MeshBuilder::Vertex Vertex;
		typedef MeshBuilder::Vec2f Vec2f;
		typedef MeshBuilder::Vec3f Vec3f;

		const float PI = 3.14159265358979f;
		const float TWO_PI = 6.28318530717959f;
		//! a radius below this counts as a lathe POLE
		const float POLE_EPSILON = 1.0e-6f;

		//! fail a generator honestly: empty mesh + a described reason
		bool refuse(Mesh & out, String * outError, char const * reason)
		{
			out.clear();
			if(outError)
			{
				*outError = reason;
			}
			return false;
		}
		//! every extent finite and strictly positive
		bool extentsOk(float a, float b, float c)
		{
			return MeshBuilder::isPositiveExtent(a) &&
				MeshBuilder::isPositiveExtent(b) &&
				MeshBuilder::isPositiveExtent(c);
		}
		//! push one vertex (tangents are generated at the end of every builder)
		std::size_t pushVertex(Mesh & mesh, Vec3f const & position,
			Vec3f const & normal, Vec2f const & uv)
		{
			Vertex vertex;
			vertex.position = position;
			vertex.normal = normal;
			vertex.uv = uv;
			mesh.vertices.push_back(vertex);
			return mesh.vertices.size() - 1;
		}
		//! two triangles for the quad a-b-c-d (a-b-c then a-c-d)
		void emitQuad(Mesh & mesh, std::size_t a, std::size_t b, std::size_t c,
			std::size_t d)
		{
			mesh.indices.push_back(static_cast<unsigned int>(a));
			mesh.indices.push_back(static_cast<unsigned int>(b));
			mesh.indices.push_back(static_cast<unsigned int>(c));
			mesh.indices.push_back(static_cast<unsigned int>(a));
			mesh.indices.push_back(static_cast<unsigned int>(c));
			mesh.indices.push_back(static_cast<unsigned int>(d));
		}
		void emitTriangle(Mesh & mesh, std::size_t a, std::size_t b,
			std::size_t c)
		{
			mesh.indices.push_back(static_cast<unsigned int>(a));
			mesh.indices.push_back(static_cast<unsigned int>(b));
			mesh.indices.push_back(static_cast<unsigned int>(c));
		}
		//! @brief a flat quad with its own 4 vertices and one face normal.
		//! The corner order a-b-c-d must wind counter-clockwise as seen from
		//! the OUTSIDE; UVs run 0..1 with a at (0,0) and c at (1,1).
		void addFlatQuad(Mesh & mesh, Vec3f const & a, Vec3f const & b,
			Vec3f const & c, Vec3f const & d, Vec3f const & normal)
		{
			const std::size_t base = mesh.vertices.size();
			pushVertex(mesh, a, normal, Vec2f(0.0f, 0.0f));
			pushVertex(mesh, b, normal, Vec2f(1.0f, 0.0f));
			pushVertex(mesh, c, normal, Vec2f(1.0f, 1.0f));
			pushVertex(mesh, d, normal, Vec2f(0.0f, 1.0f));
			emitQuad(mesh, base, base + 1, base + 2, base + 3);
		}
		//! a flat triangle with its own 3 vertices and one face normal
		void addFlatTriangle(Mesh & mesh, Vec3f const & a, Vec3f const & b,
			Vec3f const & c, Vec3f const & normal)
		{
			const std::size_t base = mesh.vertices.size();
			pushVertex(mesh, a, normal, Vec2f(0.0f, 0.0f));
			pushVertex(mesh, b, normal, Vec2f(1.0f, 0.0f));
			pushVertex(mesh, c, normal, Vec2f(0.5f, 1.0f));
			emitTriangle(mesh, base, base + 1, base + 2);
		}
		//! finish a generator: close its single section and derive tangents
		bool finish(Mesh & mesh, String * outError)
		{
			MeshBuilder::closeSection(mesh);
			MeshBuilder::computeTangents(mesh);
			String validationError;
			if(!MeshBuilder::validate(mesh, &validationError))
			{
				return refuse(mesh, outError, validationError.c_str());
			}
			return true;
		}
		//! shift every position so the mesh bounds centre lands on the origin
		void centreOnBounds(Mesh & mesh)
		{
			const MeshBuilder::Bounds bounds = mesh.computeBounds();
			if(!bounds.valid)
			{
				return;
			}
			const Vec3f centre = bounds.centre();
			for(std::size_t each = 0; each < mesh.vertices.size(); ++each)
			{
				mesh.vertices[each].position = MeshBuilder::subtract(
					mesh.vertices[each].position, centre);
			}
		}
		//! @brief the arc length walked along a lathe profile, normalised to
		//! 0..1 - the V coordinate every surface of revolution uses (so a
		//! texture stretches evenly over the swept silhouette)
		void profileV(std::vector<MeshShapes::ProfileRow> const & rows,
			std::vector<float> & outV)
		{
			outV.assign(rows.size(), 0.0f);
			float total = 0.0f;
			for(std::size_t each = 1; each < rows.size(); ++each)
			{
				const float dr = rows[each].radius - rows[each - 1].radius;
				const float dy = rows[each].y - rows[each - 1].y;
				total += std::sqrt(dr * dr + dy * dy);
				outV[each] = total;
			}
			if(total > 1.0e-9f)
			{
				for(std::size_t each = 0; each < outV.size(); ++each)
				{
					outV[each] /= total;
				}
			}
		}
		//! @brief sweep a (radius, y) profile about the Y axis. Rows connect in
		//! order; a row of radius 0 is a POLE whose degenerate triangles are
		//! skipped and whose vertices take the MID angle of the triangle that
		//! uses them (so a cone apex carries the surrounding slope normal).
		//! @p sweep is the swept angle in radians (TWO_PI = a closed surface).
		void latheProfile(Mesh & mesh,
			std::vector<MeshShapes::ProfileRow> const & rows,
			unsigned int segments, float sweep)
		{
			std::vector<float> vCoord;
			profileV(rows, vCoord);
			const std::size_t columns = static_cast<std::size_t>(segments) + 1;
			const std::size_t base = mesh.vertices.size();
			for(std::size_t row = 0; row < rows.size(); ++row)
			{
				MeshShapes::ProfileRow const & entry = rows[row];
				const bool pole = std::fabs(entry.radius) <= POLE_EPSILON;
				for(std::size_t column = 0; column < columns; ++column)
				{
					// a pole vertex is used by the triangle of segment
					// `column`, so it takes that segment's MID angle. The last
					// pole column is never referenced; clamp its U so no vertex
					// ever leaves the 0..1 window.
					const float t = std::min(1.0f,
						(static_cast<float>(column) + (pole ? 0.5f : 0.0f)) /
						static_cast<float>(segments));
					const float angle = t * sweep;
					const float cosine = std::cos(angle);
					const float sine = std::sin(angle);
					const Vec3f position(entry.radius * cosine, entry.y,
						entry.radius * sine);
					const Vec3f normal = MeshBuilder::normalise(Vec3f(
						entry.normalRadial * cosine, entry.normalY,
						entry.normalRadial * sine),
						Vec3f(0.0f, entry.normalY >= 0.0f ? 1.0f : -1.0f,
							0.0f));
					pushVertex(mesh, position, normal, Vec2f(t, vCoord[row]));
				}
			}
			for(std::size_t row = 0; row + 1 < rows.size(); ++row)
			{
				const bool poleUpper =
					std::fabs(rows[row].radius) <= POLE_EPSILON;
				const bool poleLower =
					std::fabs(rows[row + 1].radius) <= POLE_EPSILON;
				if(poleUpper && poleLower)
				{
					continue;	// a zero-radius span contributes nothing
				}
				for(unsigned int column = 0; column < segments; ++column)
				{
					const std::size_t a = base + row * columns + column;
					const std::size_t d = a + 1;
					const std::size_t b = base + (row + 1) * columns + column;
					const std::size_t c = b + 1;
					if(poleUpper)
					{
						emitTriangle(mesh, a, c, b);
					}
					else if(poleLower)
					{
						emitTriangle(mesh, a, d, b);
					}
					else
					{
						emitQuad(mesh, a, d, c, b);
					}
				}
			}
		}
		//! @brief a flat ring/disc face in the XZ plane at @p y: an annulus
		//! between @p innerRadius and @p outerRadius (a full disc when the
		//! inner radius is 0), facing +Y when @p up is true else -Y
		void addRingFace(Mesh & mesh, float y, float innerRadius,
			float outerRadius, unsigned int segments, bool up)
		{
			const Vec3f normal(0.0f, up ? 1.0f : -1.0f, 0.0f);
			const bool solid = innerRadius <= POLE_EPSILON;
			const std::size_t base = mesh.vertices.size();
			// one vertex ring (outer) plus either the centre or the inner ring
			for(unsigned int column = 0; column <= segments; ++column)
			{
				const float t = static_cast<float>(column) /
					static_cast<float>(segments);
				const float angle = t * TWO_PI;
				const float cosine = std::cos(angle);
				const float sine = std::sin(angle);
				pushVertex(mesh, Vec3f(outerRadius * cosine, y,
					outerRadius * sine), normal,
					Vec2f(0.5f + 0.5f * cosine, 0.5f + 0.5f * sine));
			}
			if(solid)
			{
				const std::size_t centre = pushVertex(mesh, Vec3f(0.0f, y, 0.0f),
					normal, Vec2f(0.5f, 0.5f));
				for(unsigned int column = 0; column < segments; ++column)
				{
					// +Y faces wind with DECREASING angle (a right-handed
					// system sees increasing theta as clockwise from above)
					if(up)
					{
						emitTriangle(mesh, centre, base + column + 1,
							base + column);
					}
					else
					{
						emitTriangle(mesh, centre, base + column,
							base + column + 1);
					}
				}
				return;
			}
			const std::size_t innerBase = mesh.vertices.size();
			for(unsigned int column = 0; column <= segments; ++column)
			{
				const float t = static_cast<float>(column) /
					static_cast<float>(segments);
				const float angle = t * TWO_PI;
				const float cosine = std::cos(angle);
				const float sine = std::sin(angle);
				const float uvScale = innerRadius / outerRadius;
				pushVertex(mesh, Vec3f(innerRadius * cosine, y,
					innerRadius * sine), normal,
					Vec2f(0.5f + 0.5f * uvScale * cosine,
						0.5f + 0.5f * uvScale * sine));
			}
			for(unsigned int column = 0; column < segments; ++column)
			{
				const std::size_t outerHere = base + column;
				const std::size_t outerNext = base + column + 1;
				const std::size_t innerHere = innerBase + column;
				const std::size_t innerNext = innerBase + column + 1;
				if(up)
				{
					emitQuad(mesh, outerNext, outerHere, innerHere, innerNext);
				}
				else
				{
					emitQuad(mesh, outerHere, outerNext, innerNext, innerHere);
				}
			}
		}
	}
	//---------------------------------------------------------
	bool MeshShapes::box(Mesh & out, float sizeX, float sizeY, float sizeZ,
		String * outError)
	{
		out.clear();
		if(!extentsOk(sizeX, sizeY, sizeZ))
		{
			return refuse(out, outError,
				"box needs finite positive sizeX/sizeY/sizeZ");
		}
		MeshBuilder::openSection(out, String());
		const float hx = sizeX * 0.5f;
		const float hy = sizeY * 0.5f;
		const float hz = sizeZ * 0.5f;
		// each face: p0 + u, v with u x v = the outward normal (that ordering
		// makes p0, p0+u, p0+u+v, p0+v counter-clockwise from outside)
		struct Face
		{
			Vec3f origin;
			Vec3f u;
			Vec3f v;
			Vec3f normal;
		};
		const Face faces[6] = {
			// +X
			{ Vec3f(hx, -hy, hz), Vec3f(0.0f, 0.0f, -sizeZ),
				Vec3f(0.0f, sizeY, 0.0f), Vec3f(1.0f, 0.0f, 0.0f) },
			// -X
			{ Vec3f(-hx, -hy, -hz), Vec3f(0.0f, 0.0f, sizeZ),
				Vec3f(0.0f, sizeY, 0.0f), Vec3f(-1.0f, 0.0f, 0.0f) },
			// +Y
			{ Vec3f(-hx, hy, hz), Vec3f(sizeX, 0.0f, 0.0f),
				Vec3f(0.0f, 0.0f, -sizeZ), Vec3f(0.0f, 1.0f, 0.0f) },
			// -Y
			{ Vec3f(-hx, -hy, -hz), Vec3f(sizeX, 0.0f, 0.0f),
				Vec3f(0.0f, 0.0f, sizeZ), Vec3f(0.0f, -1.0f, 0.0f) },
			// +Z
			{ Vec3f(-hx, -hy, hz), Vec3f(sizeX, 0.0f, 0.0f),
				Vec3f(0.0f, sizeY, 0.0f), Vec3f(0.0f, 0.0f, 1.0f) },
			// -Z
			{ Vec3f(hx, -hy, -hz), Vec3f(-sizeX, 0.0f, 0.0f),
				Vec3f(0.0f, sizeY, 0.0f), Vec3f(0.0f, 0.0f, -1.0f) },
		};
		for(int each = 0; each < 6; ++each)
		{
			Face const & face = faces[each];
			// V DOWN: a face whose v basis climbs +Y flips so the texture's top
			// row lands at the shape's top
			const bool flipV = face.v.y > 0.0f;
			const std::size_t base = out.vertices.size();
			const Vec3f p0 = face.origin;
			const Vec3f p1 = MeshBuilder::add(p0, face.u);
			const Vec3f p2 = MeshBuilder::add(p1, face.v);
			const Vec3f p3 = MeshBuilder::add(p0, face.v);
			const float vLow = flipV ? 1.0f : 0.0f;
			const float vHigh = flipV ? 0.0f : 1.0f;
			pushVertex(out, p0, face.normal, Vec2f(0.0f, vLow));
			pushVertex(out, p1, face.normal, Vec2f(1.0f, vLow));
			pushVertex(out, p2, face.normal, Vec2f(1.0f, vHigh));
			pushVertex(out, p3, face.normal, Vec2f(0.0f, vHigh));
			emitQuad(out, base, base + 1, base + 2, base + 3);
		}
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::roundedBox(Mesh & out, float sizeX, float sizeY,
		float sizeZ, float radius, int segments, String * outError)
	{
		out.clear();
		if(!extentsOk(sizeX, sizeY, sizeZ))
		{
			return refuse(out, outError,
				"roundedBox needs finite positive sizeX/sizeY/sizeZ");
		}
		if(!MeshBuilder::isFinite(radius) || radius < 0.0f)
		{
			return refuse(out, outError,
				"roundedBox needs a finite non-negative radius");
		}
		const float hx = sizeX * 0.5f;
		const float hy = sizeY * 0.5f;
		const float hz = sizeZ * 0.5f;
		const float corner = std::min(radius, std::min(hx, std::min(hy, hz)));
		const unsigned int steps = MeshBuilder::clampSegments(segments, 1);
		MeshBuilder::openSection(out, String());
		const Vec3f half(hx, hy, hz);
		const Vec3f inner(hx - corner, hy - corner, hz - corner);
		struct Face
		{
			Vec3f normal;
			Vec3f u;
			Vec3f v;
		};
		const Face faces[6] = {
			{ Vec3f(1.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, -1.0f),
				Vec3f(0.0f, 1.0f, 0.0f) },
			{ Vec3f(-1.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 1.0f),
				Vec3f(0.0f, 1.0f, 0.0f) },
			{ Vec3f(0.0f, 1.0f, 0.0f), Vec3f(1.0f, 0.0f, 0.0f),
				Vec3f(0.0f, 0.0f, -1.0f) },
			{ Vec3f(0.0f, -1.0f, 0.0f), Vec3f(1.0f, 0.0f, 0.0f),
				Vec3f(0.0f, 0.0f, 1.0f) },
			{ Vec3f(0.0f, 0.0f, 1.0f), Vec3f(1.0f, 0.0f, 0.0f),
				Vec3f(0.0f, 1.0f, 0.0f) },
			{ Vec3f(0.0f, 0.0f, -1.0f), Vec3f(-1.0f, 0.0f, 0.0f),
				Vec3f(0.0f, 1.0f, 0.0f) },
		};
		for(int face = 0; face < 6; ++face)
		{
			Face const & entry = faces[face];
			const bool flipV = entry.v.y > 0.0f;
			const std::size_t base = out.vertices.size();
			// the face's own half extents along its u/v basis
			const float halfU = std::fabs(entry.u.x) * half.x +
				std::fabs(entry.u.y) * half.y + std::fabs(entry.u.z) * half.z;
			const float halfV = std::fabs(entry.v.x) * half.x +
				std::fabs(entry.v.y) * half.y + std::fabs(entry.v.z) * half.z;
			const float halfN = std::fabs(entry.normal.x) * half.x +
				std::fabs(entry.normal.y) * half.y +
				std::fabs(entry.normal.z) * half.z;
			for(unsigned int iv = 0; iv <= steps; ++iv)
			{
				const float tv = static_cast<float>(iv) /
					static_cast<float>(steps);
				for(unsigned int iu = 0; iu <= steps; ++iu)
				{
					const float tu = static_cast<float>(iu) /
						static_cast<float>(steps);
					// the point on the SHARP box surface
					const Vec3f surface = MeshBuilder::add(
						MeshBuilder::add(
							MeshBuilder::scale(entry.normal, halfN),
							MeshBuilder::scale(entry.u,
								(tu * 2.0f - 1.0f) * halfU)),
						MeshBuilder::scale(entry.v,
							(tv * 2.0f - 1.0f) * halfV));
					// clamp it into the inner box, then push back out by the
					// corner radius - the exact rounded hull and its normal
					const Vec3f clamped(
						std::max(-inner.x, std::min(inner.x, surface.x)),
						std::max(-inner.y, std::min(inner.y, surface.y)),
						std::max(-inner.z, std::min(inner.z, surface.z)));
					const Vec3f offset = MeshBuilder::subtract(surface, clamped);
					const Vec3f normal = (corner > POLE_EPSILON)
						? MeshBuilder::normalise(offset, entry.normal)
						: entry.normal;
					const Vec3f position = (corner > POLE_EPSILON)
						? MeshBuilder::add(clamped,
							MeshBuilder::scale(normal, corner))
						: surface;
					pushVertex(out, position, normal,
						Vec2f(tu, flipV ? 1.0f - tv : tv));
				}
			}
			const std::size_t stride = static_cast<std::size_t>(steps) + 1;
			for(unsigned int iv = 0; iv < steps; ++iv)
			{
				for(unsigned int iu = 0; iu < steps; ++iu)
				{
					const std::size_t a = base + iv * stride + iu;
					emitQuad(out, a, a + 1, a + 1 + stride, a + stride);
				}
			}
		}
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::plane(Mesh & out, float sizeX, float sizeZ,
		int segmentsX, int segmentsZ, String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(sizeX) ||
			!MeshBuilder::isPositiveExtent(sizeZ))
		{
			return refuse(out, outError,
				"plane needs finite positive sizeX/sizeZ");
		}
		const unsigned int stepsX = MeshBuilder::clampSegments(segmentsX, 1);
		const unsigned int stepsZ = MeshBuilder::clampSegments(segmentsZ, 1);
		MeshBuilder::openSection(out, String());
		const float hx = sizeX * 0.5f;
		const float hz = sizeZ * 0.5f;
		const Vec3f normal(0.0f, 1.0f, 0.0f);
		for(unsigned int iz = 0; iz <= stepsZ; ++iz)
		{
			const float tz = static_cast<float>(iz) /
				static_cast<float>(stepsZ);
			for(unsigned int ix = 0; ix <= stepsX; ++ix)
			{
				const float tx = static_cast<float>(ix) /
					static_cast<float>(stepsX);
				pushVertex(out, Vec3f(-hx + tx * sizeX, 0.0f, -hz + tz * sizeZ),
					normal, Vec2f(tx, tz));
			}
		}
		const std::size_t stride = static_cast<std::size_t>(stepsX) + 1;
		for(unsigned int iz = 0; iz < stepsZ; ++iz)
		{
			for(unsigned int ix = 0; ix < stepsX; ++ix)
			{
				const std::size_t a = iz * stride + ix;
				// +Y front faces: (x, z) -> (x, z+1) -> (x+1, z+1) -> (x+1, z)
				emitQuad(out, a, a + stride, a + stride + 1, a + 1);
			}
		}
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::revolveProfile(Mesh & out, ProfileRow const * rows,
		std::size_t rowCount, int segments, float sweepDegrees,
		String * outError)
	{
		out.clear();
		if(!rows || rowCount < 2)
		{
			return refuse(out, outError,
				"a lathe profile needs at least 2 rows");
		}
		if(!MeshBuilder::isFinite(sweepDegrees) ||
			std::fabs(sweepDegrees) < 1.0e-3f)
		{
			return refuse(out, outError,
				"a lathe sweep must be a finite non-zero angle");
		}
		std::vector<ProfileRow> profile;
		profile.reserve(rowCount);
		for(std::size_t each = 0; each < rowCount; ++each)
		{
			ProfileRow row = rows[each];
			if(!MeshBuilder::isFinite(row.radius) ||
				!MeshBuilder::isFinite(row.y) || row.radius < 0.0f)
			{
				return refuse(out, outError,
					"a lathe profile row needs a finite non-negative radius "
					"and a finite height");
			}
			if(!MeshBuilder::isFinite(row.normalRadial) ||
				!MeshBuilder::isFinite(row.normalY))
			{
				row.normalRadial = 1.0f;
				row.normalY = 0.0f;
			}
			profile.push_back(row);
		}
		const unsigned int steps = MeshBuilder::clampSegments(segments);
		const float sweep = std::min(360.0f, std::fabs(sweepDegrees)) *
			(PI / 180.0f);
		MeshBuilder::openSection(out, String());
		latheProfile(out, profile, steps, sweep);
		if(out.indices.empty())
		{
			return refuse(out, outError,
				"the lathe profile produced no geometry (every row is a pole)");
		}
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::uvSphere(Mesh & out, float radius, int segments,
		int rings, String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(radius))
		{
			return refuse(out, outError,
				"uvSphere needs a finite positive radius");
		}
		const unsigned int columns = MeshBuilder::clampSegments(segments);
		const unsigned int bands = MeshBuilder::clampSegments(rings, 2);
		std::vector<ProfileRow> profile;
		profile.reserve(bands + 1);
		for(unsigned int each = 0; each <= bands; ++each)
		{
			const float phi = PI * static_cast<float>(each) /
				static_cast<float>(bands);
			const float sine = std::sin(phi);
			const float cosine = std::cos(phi);
			profile.push_back(ProfileRow(radius * sine, radius * cosine,
				sine, cosine));
		}
		MeshBuilder::openSection(out, String());
		latheProfile(out, profile, columns, TWO_PI);
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::icosphere(Mesh & out, float radius, int subdivisions,
		String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(radius))
		{
			return refuse(out, outError,
				"icosphere needs a finite positive radius");
		}
		const int levels = std::max(0, std::min(5, subdivisions));
		// the regular icosahedron (golden-ratio rectangles), unit-normalised
		const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
		std::vector<Vec3f> points;
		const float seed[12][3] = {
			{-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
			{ 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
			{ t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
		};
		for(int each = 0; each < 12; ++each)
		{
			points.push_back(MeshBuilder::normalise(
				Vec3f(seed[each][0], seed[each][1], seed[each][2])));
		}
		const unsigned int seedFaces[20][3] = {
			{0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
			{1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
			{3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
			{4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1},
		};
		std::vector<unsigned int> faces;
		faces.reserve(60);
		for(int each = 0; each < 20; ++each)
		{
			faces.push_back(seedFaces[each][0]);
			faces.push_back(seedFaces[each][1]);
			faces.push_back(seedFaces[each][2]);
		}
		// subdivide: a std::map keyed on the sorted edge pair keeps the
		// midpoint cache DETERMINISTIC (never a hash order)
		for(int level = 0; level < levels; ++level)
		{
			std::map<std::pair<unsigned int, unsigned int>, unsigned int> cache;
			std::vector<unsigned int> split;
			split.reserve(faces.size() * 4);
			for(std::size_t each = 0; each < faces.size(); each += 3)
			{
				unsigned int corner[3] = { faces[each], faces[each + 1],
					faces[each + 2] };
				unsigned int middle[3];
				for(int edge = 0; edge < 3; ++edge)
				{
					const unsigned int from = corner[edge];
					const unsigned int to = corner[(edge + 1) % 3];
					const std::pair<unsigned int, unsigned int> key(
						std::min(from, to), std::max(from, to));
					std::map<std::pair<unsigned int, unsigned int>,
						unsigned int>::const_iterator found = cache.find(key);
					if(found != cache.end())
					{
						middle[edge] = found->second;
						continue;
					}
					const Vec3f midpoint = MeshBuilder::normalise(
						MeshBuilder::scale(MeshBuilder::add(points[from],
							points[to]), 0.5f));
					points.push_back(midpoint);
					const unsigned int index =
						static_cast<unsigned int>(points.size() - 1);
					cache[key] = index;
					middle[edge] = index;
				}
				const unsigned int table[4][3] = {
					{ corner[0], middle[0], middle[2] },
					{ corner[1], middle[1], middle[0] },
					{ corner[2], middle[2], middle[1] },
					{ middle[0], middle[1], middle[2] },
				};
				for(int piece = 0; piece < 4; ++piece)
				{
					split.push_back(table[piece][0]);
					split.push_back(table[piece][1]);
					split.push_back(table[piece][2]);
				}
			}
			faces.swap(split);
		}
		MeshBuilder::openSection(out, String());
		for(std::size_t each = 0; each < points.size(); ++each)
		{
			// a spherical UV projection (longitude/latitude) - it carries the
			// usual seam, which the header documents
			const Vec3f direction = points[each];
			const float u = std::atan2(direction.z, direction.x) / TWO_PI + 0.5f;
			const float v = std::acos(std::max(-1.0f,
				std::min(1.0f, direction.y))) / PI;
			pushVertex(out, MeshBuilder::scale(direction, radius), direction,
				Vec2f(u, v));
		}
		out.indices = faces;
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::cylinder(Mesh & out, float radius, float height,
		int segments, bool caps, String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(radius) ||
			!MeshBuilder::isPositiveExtent(height))
		{
			return refuse(out, outError,
				"cylinder needs a finite positive radius and height");
		}
		const unsigned int columns = MeshBuilder::clampSegments(segments);
		const float half = height * 0.5f;
		std::vector<ProfileRow> profile;
		profile.push_back(ProfileRow(radius, half, 1.0f, 0.0f));
		profile.push_back(ProfileRow(radius, -half, 1.0f, 0.0f));
		MeshBuilder::openSection(out, String());
		latheProfile(out, profile, columns, TWO_PI);
		if(caps)
		{
			addRingFace(out, half, 0.0f, radius, columns, true);
			addRingFace(out, -half, 0.0f, radius, columns, false);
		}
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::cone(Mesh & out, float radius, float height,
		int segments, bool cap, String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(radius) ||
			!MeshBuilder::isPositiveExtent(height))
		{
			return refuse(out, outError,
				"cone needs a finite positive radius and height");
		}
		const unsigned int columns = MeshBuilder::clampSegments(segments);
		const float half = height * 0.5f;
		// the slope normal in the (radial, Y) plane: perpendicular to the
		// silhouette segment from (radius, -half) to (0, +half)
		const Vec3f slope = MeshBuilder::normalise(
			Vec3f(height, radius, 0.0f));
		std::vector<ProfileRow> profile;
		profile.push_back(ProfileRow(0.0f, half, slope.x, slope.y));
		profile.push_back(ProfileRow(radius, -half, slope.x, slope.y));
		MeshBuilder::openSection(out, String());
		latheProfile(out, profile, columns, TWO_PI);
		if(cap)
		{
			addRingFace(out, -half, 0.0f, radius, columns, false);
		}
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::capsule(Mesh & out, float radius, float height,
		int segments, int rings, String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(radius))
		{
			return refuse(out, outError,
				"capsule needs a finite positive radius");
		}
		if(!MeshBuilder::isFinite(height) || height < 0.0f)
		{
			return refuse(out, outError,
				"capsule needs a finite non-negative height (the straight part)");
		}
		const unsigned int columns = MeshBuilder::clampSegments(segments);
		const unsigned int bands = MeshBuilder::clampSegments(rings, 1);
		const float half = height * 0.5f;
		std::vector<ProfileRow> profile;
		// upper hemisphere: pole down to the equator at +half
		for(unsigned int each = 0; each <= bands; ++each)
		{
			const float phi = (PI * 0.5f) * static_cast<float>(each) /
				static_cast<float>(bands);
			const float sine = std::sin(phi);
			const float cosine = std::cos(phi);
			profile.push_back(ProfileRow(radius * sine,
				half + radius * cosine, sine, cosine));
		}
		if(height > POLE_EPSILON)
		{
			profile.push_back(ProfileRow(radius, -half, 1.0f, 0.0f));
		}
		// lower hemisphere: equator at -half down to the pole
		for(unsigned int each = 1; each <= bands; ++each)
		{
			const float phi = (PI * 0.5f) * (1.0f +
				static_cast<float>(each) / static_cast<float>(bands));
			const float sine = std::sin(phi);
			const float cosine = std::cos(phi);
			profile.push_back(ProfileRow(radius * sine,
				-half + radius * cosine, sine, cosine));
		}
		MeshBuilder::openSection(out, String());
		latheProfile(out, profile, columns, TWO_PI);
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::torus(Mesh & out, float radius, float tubeRadius,
		int segments, int tubeSegments, String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(radius) ||
			!MeshBuilder::isPositiveExtent(tubeRadius))
		{
			return refuse(out, outError,
				"torus needs a finite positive radius and tubeRadius");
		}
		const unsigned int columns = MeshBuilder::clampSegments(segments);
		const unsigned int tubeColumns =
			MeshBuilder::clampSegments(tubeSegments);
		// a self-intersecting tube is geometry nobody wants: keep it just
		// inside the ring so the surface stays a torus
		const float tube = std::min(tubeRadius, radius * 0.999f);
		std::vector<ProfileRow> profile;
		profile.reserve(tubeColumns + 1);
		for(unsigned int each = 0; each <= tubeColumns; ++each)
		{
			const float alpha = TWO_PI * static_cast<float>(each) /
				static_cast<float>(tubeColumns);
			const float cosine = std::cos(alpha);
			const float sine = std::sin(alpha);
			profile.push_back(ProfileRow(radius + tube * cosine, tube * sine,
				cosine, sine));
		}
		MeshBuilder::openSection(out, String());
		latheProfile(out, profile, columns, TWO_PI);
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::tube(Mesh & out, float outerRadius, float innerRadius,
		float height, int segments, bool caps, String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(outerRadius) ||
			!MeshBuilder::isPositiveExtent(innerRadius) ||
			!MeshBuilder::isPositiveExtent(height))
		{
			return refuse(out, outError,
				"tube needs finite positive outerRadius/innerRadius/height");
		}
		if(innerRadius >= outerRadius)
		{
			return refuse(out, outError,
				"tube needs innerRadius smaller than outerRadius");
		}
		const unsigned int columns = MeshBuilder::clampSegments(segments);
		const float half = height * 0.5f;
		MeshBuilder::openSection(out, String());
		// outer wall (top -> bottom rows = outward normals)
		std::vector<ProfileRow> outer;
		outer.push_back(ProfileRow(outerRadius, half, 1.0f, 0.0f));
		outer.push_back(ProfileRow(outerRadius, -half, 1.0f, 0.0f));
		latheProfile(out, outer, columns, TWO_PI);
		// inner wall: the row order REVERSES the winding, and the radial
		// normal flips, so the surface faces into the bore
		std::vector<ProfileRow> inner;
		inner.push_back(ProfileRow(innerRadius, -half, -1.0f, 0.0f));
		inner.push_back(ProfileRow(innerRadius, half, -1.0f, 0.0f));
		latheProfile(out, inner, columns, TWO_PI);
		if(caps)
		{
			addRingFace(out, half, innerRadius, outerRadius, columns, true);
			addRingFace(out, -half, innerRadius, outerRadius, columns, false);
		}
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::disc(Mesh & out, float radius, float innerRadius,
		int segments, String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(radius))
		{
			return refuse(out, outError,
				"disc needs a finite positive radius");
		}
		if(!MeshBuilder::isFinite(innerRadius) || innerRadius < 0.0f ||
			innerRadius >= radius)
		{
			return refuse(out, outError,
				"disc needs a finite innerRadius in [0; radius)");
		}
		const unsigned int columns = MeshBuilder::clampSegments(segments);
		MeshBuilder::openSection(out, String());
		addRingFace(out, 0.0f, innerRadius, radius, columns, true);
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::wedge(Mesh & out, float sizeX, float sizeY, float sizeZ,
		String * outError)
	{
		out.clear();
		if(!extentsOk(sizeX, sizeY, sizeZ))
		{
			return refuse(out, outError,
				"wedge needs finite positive sizeX/sizeY/sizeZ");
		}
		MeshBuilder::openSection(out, String());
		const float hx = sizeX * 0.5f;
		const float hy = sizeY * 0.5f;
		const float hz = sizeZ * 0.5f;
		// bottom (-Y)
		addFlatQuad(out, Vec3f(-hx, -hy, -hz), Vec3f(hx, -hy, -hz),
			Vec3f(hx, -hy, hz), Vec3f(-hx, -hy, hz),
			Vec3f(0.0f, -1.0f, 0.0f));
		// the tall back wall at +X
		addFlatQuad(out, Vec3f(hx, -hy, hz), Vec3f(hx, -hy, -hz),
			Vec3f(hx, hy, -hz), Vec3f(hx, hy, hz), Vec3f(1.0f, 0.0f, 0.0f));
		// the slope: its outward normal is perpendicular to the rise
		const Vec3f slopeNormal = MeshBuilder::normalise(
			Vec3f(-sizeY, sizeX, 0.0f));
		addFlatQuad(out, Vec3f(-hx, -hy, -hz), Vec3f(-hx, -hy, hz),
			Vec3f(hx, hy, hz), Vec3f(hx, hy, -hz), slopeNormal);
		// the two triangular sides
		addFlatTriangle(out, Vec3f(-hx, -hy, hz), Vec3f(hx, -hy, hz),
			Vec3f(hx, hy, hz), Vec3f(0.0f, 0.0f, 1.0f));
		addFlatTriangle(out, Vec3f(-hx, -hy, -hz), Vec3f(hx, hy, -hz),
			Vec3f(hx, -hy, -hz), Vec3f(0.0f, 0.0f, -1.0f));
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::stairs(Mesh & out, float sizeX, float sizeY, float sizeZ,
		int steps, String * outError)
	{
		out.clear();
		if(!extentsOk(sizeX, sizeY, sizeZ))
		{
			return refuse(out, outError,
				"stairs needs finite positive sizeX/sizeY/sizeZ");
		}
		const unsigned int count = MeshBuilder::clampSegments(steps, 1);
		MeshBuilder::openSection(out, String());
		const float hx = sizeX * 0.5f;
		const float hy = sizeY * 0.5f;
		const float hz = sizeZ * 0.5f;
		const float run = sizeX / static_cast<float>(count);
		const float rise = sizeY / static_cast<float>(count);
		// THE GRID DISCIPLINE: the cross-section is tiled into run x rise CELLS
		// and the outline is SUBDIVIDED to the same pitch, so every cap edge
		// meets exactly one neighbouring cap edge or exactly one wall quad.
		// Coarser tilings (one column per step) leave T-junctions where a short
		// column edge meets a taller one - a hairline crack under any renderer
		// and a non-manifold solid for a future collider.
		for(unsigned int column = 0; column < count; ++column)
		{
			const float x0 = -hx + static_cast<float>(column) * run;
			const float x1 = x0 + run;
			for(unsigned int row = 0; row <= column; ++row)
			{
				const float y0 = -hy + static_cast<float>(row) * rise;
				const float y1 = y0 + rise;
				// +Z cap
				addFlatQuad(out, Vec3f(x0, y0, hz), Vec3f(x1, y0, hz),
					Vec3f(x1, y1, hz), Vec3f(x0, y1, hz),
					Vec3f(0.0f, 0.0f, 1.0f));
				// -Z cap
				addFlatQuad(out, Vec3f(x0, y0, -hz), Vec3f(x0, y1, -hz),
					Vec3f(x1, y1, -hz), Vec3f(x1, y0, -hz),
					Vec3f(0.0f, 0.0f, -1.0f));
			}
		}
		// the closed outline, counter-clockwise in XY and subdivided to the
		// cell pitch: the underside, up the back, then the stepped walking
		// surface descending back to the start
		std::vector<Vec2f> outline;
		for(unsigned int each = 0; each <= count; ++each)
		{
			outline.push_back(Vec2f(-hx + static_cast<float>(each) * run, -hy));
		}
		for(unsigned int each = 1; each <= count; ++each)
		{
			outline.push_back(Vec2f(hx,
				-hy + static_cast<float>(each) * rise));
		}
		for(unsigned int each = count; each-- > 0; )
		{
			const float x = -hx + static_cast<float>(each) * run;
			outline.push_back(Vec2f(x,
				-hy + static_cast<float>(each + 1) * rise));	// tread start
			if(each > 0)
			{
				outline.push_back(Vec2f(x,
					-hy + static_cast<float>(each) * rise));	// riser foot
			}
		}
		for(std::size_t each = 0; each < outline.size(); ++each)
		{
			Vec2f const & from = outline[each];
			Vec2f const & to = outline[(each + 1) % outline.size()];
			const Vec3f direction(to.x - from.x, to.y - from.y, 0.0f);
			if(MeshBuilder::length(direction) < POLE_EPSILON)
			{
				continue;
			}
			// the outward normal of a counter-clockwise outline edge
			const Vec3f normal = MeshBuilder::normalise(
				Vec3f(direction.y, -direction.x, 0.0f));
			addFlatQuad(out, Vec3f(from.x, from.y, hz),
				Vec3f(from.x, from.y, -hz), Vec3f(to.x, to.y, -hz),
				Vec3f(to.x, to.y, hz), normal);
		}
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::sweepPath(Mesh & out, Vec2f const * path,
		std::size_t pathCount, float width, float depth, String * outError)
	{
		out.clear();
		if(!path || pathCount < 2)
		{
			return refuse(out, outError, "a sweep needs at least 2 path points");
		}
		if(!MeshBuilder::isPositiveExtent(width) ||
			!MeshBuilder::isPositiveExtent(depth))
		{
			return refuse(out, outError,
				"a sweep needs a finite positive width and depth");
		}
		// drop repeated points: a zero-length segment has no direction
		std::vector<Vec2f> line;
		for(std::size_t each = 0; each < pathCount; ++each)
		{
			if(!MeshBuilder::isFinite(path[each].x) ||
				!MeshBuilder::isFinite(path[each].y))
			{
				return refuse(out, outError,
					"a sweep path point is not finite");
			}
			if(!line.empty())
			{
				const float dx = path[each].x - line.back().x;
				const float dy = path[each].y - line.back().y;
				if(std::sqrt(dx * dx + dy * dy) < 1.0e-6f)
				{
					continue;
				}
			}
			line.push_back(path[each]);
		}
		if(line.size() < 2)
		{
			return refuse(out, outError, "a sweep path has no length");
		}
		const std::size_t rings = line.size();
		// per-point mitred cross-section normal + the cumulative path length
		// (the sweep's U coordinate)
		std::vector<Vec2f> normals(rings);
		std::vector<float> along(rings, 0.0f);
		std::vector<Vec2f> segmentNormal(rings - 1);
		for(std::size_t each = 0; each + 1 < rings; ++each)
		{
			const float dx = line[each + 1].x - line[each].x;
			const float dy = line[each + 1].y - line[each].y;
			const float len = std::sqrt(dx * dx + dy * dy);
			segmentNormal[each] = Vec2f(-dy / len, dx / len);
			along[each + 1] = along[each] + len;
		}
		for(std::size_t each = 0; each < rings; ++each)
		{
			if(each == 0)
			{
				normals[each] = segmentNormal[0];
			}
			else if(each + 1 == rings)
			{
				normals[each] = segmentNormal[rings - 2];
			}
			else
			{
				Vec2f const & before = segmentNormal[each - 1];
				Vec2f const & after = segmentNormal[each];
				const Vec3f averaged = MeshBuilder::normalise(
					Vec3f(before.x + after.x, before.y + after.y, 0.0f),
					Vec3f(after.x, after.y, 0.0f));
				// the miter factor keeps the band's THICKNESS constant round a
				// corner (a plain average would pinch it)
				const float projection = averaged.x * after.x +
					averaged.y * after.y;
				const float miter = 1.0f / std::max(0.25f,
					std::fabs(projection));
				normals[each] = Vec2f(averaged.x * miter, averaged.y * miter);
			}
		}
		const float totalLength = along[rings - 1];
		const float halfWidth = width * 0.5f;
		const float halfDepth = depth * 0.5f;
		MeshBuilder::openSection(out, String());
		// the cross-section loop, in order: +n/-z, +n/+z, -n/+z, -n/-z. Each
		// consecutive pair is one STRIP along the path; a strip's normal is
		// constant across it, so the +/-n strips shade smoothly along a curve.
		struct Strip
		{
			int		fromCorner;
			int		toCorner;
			int		normalMode;	//!< 0 = +n, 1 = +z, 2 = -n, 3 = -z
		};
		const Strip strips[4] = { {0, 1, 0}, {1, 2, 1}, {2, 3, 2}, {3, 0, 3} };
		for(int strip = 0; strip < 4; ++strip)
		{
			const std::size_t base = out.vertices.size();
			for(std::size_t ring = 0; ring < rings; ++ring)
			{
				const Vec2f & n = normals[ring];
				const float u = (totalLength > 1.0e-9f)
					? along[ring] / totalLength : 0.0f;
				Vec3f normal;
				switch(strips[strip].normalMode)
				{
				case 0: normal = MeshBuilder::normalise(
					Vec3f(n.x, n.y, 0.0f)); break;
				case 1: normal = Vec3f(0.0f, 0.0f, 1.0f); break;
				case 2: normal = MeshBuilder::normalise(
					Vec3f(-n.x, -n.y, 0.0f)); break;
				default: normal = Vec3f(0.0f, 0.0f, -1.0f); break;
				}
				for(int end = 0; end < 2; ++end)
				{
					const int corner = (end == 0) ? strips[strip].fromCorner
						: strips[strip].toCorner;
					const float side = (corner == 0 || corner == 1)
						? 1.0f : -1.0f;
					const float z = (corner == 1 || corner == 2)
						? halfDepth : -halfDepth;
					pushVertex(out, Vec3f(
						line[ring].x + n.x * halfWidth * side,
						line[ring].y + n.y * halfWidth * side, z), normal,
						Vec2f(u, (end == 0) ? 0.0f : 1.0f));
				}
			}
			for(std::size_t ring = 0; ring + 1 < rings; ++ring)
			{
				const std::size_t here = base + ring * 2;
				const std::size_t next = here + 2;
				emitQuad(out, here, here + 1, next + 1, next);
			}
		}
		// the two end caps, wound so they face away from the band
		for(int end = 0; end < 2; ++end)
		{
			const std::size_t ring = (end == 0) ? 0 : rings - 1;
			const Vec2f & n = normals[ring];
			const std::size_t other = (end == 0) ? 1 : rings - 2;
			const Vec3f tangent = MeshBuilder::normalise(Vec3f(
				line[ring].x - line[other].x, line[ring].y - line[other].y,
				0.0f));
			Vec3f corners[4];
			for(int corner = 0; corner < 4; ++corner)
			{
				const float side = (corner == 0 || corner == 1) ? 1.0f : -1.0f;
				const float z = (corner == 1 || corner == 2)
					? halfDepth : -halfDepth;
				corners[corner] = Vec3f(line[ring].x + n.x * halfWidth * side,
					line[ring].y + n.y * halfWidth * side, z);
			}
			if(end == 0)
			{
				addFlatQuad(out, corners[0], corners[3], corners[2], corners[1],
					tangent);
			}
			else
			{
				addFlatQuad(out, corners[0], corners[1], corners[2], corners[3],
					tangent);
			}
		}
		return finish(out, outError);
	}
	//---------------------------------------------------------
	bool MeshShapes::arch(Mesh & out, float spanWidth, float legHeight,
		float thickness, float depth, int segments, String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(spanWidth) ||
			!MeshBuilder::isPositiveExtent(thickness) ||
			!MeshBuilder::isPositiveExtent(depth))
		{
			return refuse(out, outError,
				"arch needs finite positive spanWidth/thickness/depth");
		}
		if(!MeshBuilder::isFinite(legHeight) || legHeight < 0.0f)
		{
			return refuse(out, outError,
				"arch needs a finite non-negative legHeight");
		}
		const unsigned int steps = MeshBuilder::clampSegments(segments);
		// the band's CENTRELINE radius: the opening's half width plus half the
		// band thickness, so the inner face bounds exactly spanWidth
		const float centreRadius = spanWidth * 0.5f + thickness * 0.5f;
		std::vector<Vec2f> path;
		path.push_back(Vec2f(-centreRadius, 0.0f));
		path.push_back(Vec2f(-centreRadius, legHeight));
		for(unsigned int each = 1; each <= steps; ++each)
		{
			const float angle = PI * (1.0f - static_cast<float>(each) /
				static_cast<float>(steps));
			path.push_back(Vec2f(centreRadius * std::cos(angle),
				legHeight + centreRadius * std::sin(angle)));
		}
		path.push_back(Vec2f(centreRadius, 0.0f));
		if(!sweepPath(out, path.data(), path.size(), thickness, depth,
			outError))
		{
			return false;
		}
		centreOnBounds(out);
		return true;
	}
}
