/********************************************************************
	created:	Thursday 2026/07/30 at 09:20
	filename: 	MeshExtrude.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "core_util/MeshExtrude.h"

#include "core_util/MeshShapes.h"
#include "core_util/ShapeCollider.h"

#include <algorithm>
#include <cmath>

namespace Orkige
{
	namespace
	{
		typedef MeshBuilder::Mesh Mesh;
		typedef MeshBuilder::Vec2f Vec2f;
		typedef MeshBuilder::Vec3f Vec3f;
		typedef VectorTessellator::Point Point;

		bool refuse(Mesh & out, String * outError, char const * reason)
		{
			out.clear();
			if(outError)
			{
				*outError = reason;
			}
			return false;
		}
		std::size_t pushVertex(Mesh & mesh, Vec3f const & position,
			Vec3f const & normal, Vec2f const & uv)
		{
			MeshBuilder::Vertex vertex;
			vertex.position = position;
			vertex.normal = normal;
			vertex.uv = uv;
			mesh.vertices.push_back(vertex);
			return mesh.vertices.size() - 1;
		}
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
		//! @brief re-orient a loop to the requested handedness in XY
		//! (counter-clockwise = positive signed area). The wall sweep's outward
		//! normal formula assumes outer loops run counter-clockwise and holes
		//! clockwise, so both are normalised before anything is emitted.
		void orientLoop(std::vector<Point> & loop, bool counterClockwise)
		{
			const float area = VectorTessellator::signedArea(loop);
			if((area < 0.0f) == counterClockwise)
			{
				std::reverse(loop.begin(), loop.end());
			}
		}
		//! @brief sweep one boundary loop into side walls between the back and
		//! front cap planes. The loop must already carry the orientation whose
		//! (dy, -dx) edge normal points AWAY from the solid.
		void sweepWalls(Mesh & mesh, std::vector<Point> const & loop,
			float frontZ, float backZ, bool smooth)
		{
			const std::size_t count = loop.size();
			if(count < 3)
			{
				return;
			}
			// per-edge outward normals plus the cumulative loop length (the
			// wall's U coordinate)
			std::vector<Vec3f> edgeNormal(count);
			std::vector<float> along(count + 1, 0.0f);
			for(std::size_t each = 0; each < count; ++each)
			{
				Point const & from = loop[each];
				Point const & to = loop[(each + 1) % count];
				const float dx = to.x - from.x;
				const float dy = to.y - from.y;
				edgeNormal[each] = MeshBuilder::normalise(Vec3f(dy, -dx, 0.0f),
					Vec3f(0.0f, 0.0f, 1.0f));
				along[each + 1] = along[each] + std::sqrt(dx * dx + dy * dy);
			}
			const float perimeter = along[count];
			const float uScale = (perimeter > 1.0e-9f) ? 1.0f / perimeter : 0.0f;
			for(std::size_t each = 0; each < count; ++each)
			{
				Point const & from = loop[each];
				Point const & to = loop[(each + 1) % count];
				Vec3f normalFrom = edgeNormal[each];
				Vec3f normalTo = edgeNormal[each];
				if(smooth)
				{
					const std::size_t before = (each + count - 1) % count;
					const std::size_t after = (each + 1) % count;
					normalFrom = MeshBuilder::normalise(MeshBuilder::add(
						edgeNormal[before], edgeNormal[each]), edgeNormal[each]);
					normalTo = MeshBuilder::normalise(MeshBuilder::add(
						edgeNormal[each], edgeNormal[after]), edgeNormal[each]);
				}
				const float uFrom = along[each] * uScale;
				const float uTo = along[each + 1] * uScale;
				// wound so the face looks along its outward normal: walking the
				// loop's own direction on the FRONT plane and back along the
				// BACK plane would face inward, so the quad runs
				// front-from, back-from, back-to, front-to
				const std::size_t base = mesh.vertices.size();
				pushVertex(mesh, Vec3f(from.x, from.y, frontZ), normalFrom,
					Vec2f(uFrom, 0.0f));
				pushVertex(mesh, Vec3f(from.x, from.y, backZ), normalFrom,
					Vec2f(uFrom, 1.0f));
				pushVertex(mesh, Vec3f(to.x, to.y, backZ), normalTo,
					Vec2f(uTo, 1.0f));
				pushVertex(mesh, Vec3f(to.x, to.y, frontZ), normalTo,
					Vec2f(uTo, 0.0f));
				emitQuad(mesh, base, base + 1, base + 2, base + 3);
			}
		}
	}
	//---------------------------------------------------------
	bool MeshExtrude::extrudeShape(Mesh & out,
		std::vector<VectorTessellator::Region> const & regions,
		float depth, bool smoothSides, String * outError)
	{
		out.clear();
		if(!MeshBuilder::isPositiveExtent(depth))
		{
			return refuse(out, outError,
				"extrude needs a finite positive depth");
		}
		// the SHARED contour vocabulary: exactly the regions the collider path
		// treats as solid boundaries
		std::vector<VectorTessellator::Region const *> solid;
		for(std::size_t each = 0; each < regions.size(); ++each)
		{
			if(ShapeCollider::isSolidRegion(regions[each]))
			{
				solid.push_back(&regions[each]);
			}
		}
		if(solid.empty())
		{
			return refuse(out, outError,
				"extrude found no solid fill region in the shape");
		}
		const float frontZ = depth * 0.5f;
		const float backZ = -depth * 0.5f;
		const VectorTessellator::Bounds bounds =
			VectorTessellator::computeBounds(regions);
		const float sizeX = (bounds.maxX - bounds.minX > 1.0e-6f)
			? (bounds.maxX - bounds.minX) : 1.0f;
		const float sizeY = (bounds.maxY - bounds.minY > 1.0e-6f)
			? (bounds.maxY - bounds.minY) : 1.0f;
		MeshBuilder::openSection(out, String());
		for(std::size_t each = 0; each < solid.size(); ++each)
		{
			VectorTessellator::Region const & region = *solid[each];
			// --- the two caps: the tessellator's own earcut (holes included) ---
			VectorTessellator::Mesh flat;
			VectorTessellator::triangulateFill(region, flat);
			const std::size_t triangles = flat.indices.size() / 3;
			for(int side = 0; side < 2; ++side)
			{
				const bool front = (side == 0);
				const float z = front ? frontZ : backZ;
				const Vec3f normal(0.0f, 0.0f, front ? 1.0f : -1.0f);
				const std::size_t base = out.vertices.size();
				for(std::size_t at = 0; at < flat.positions.size(); ++at)
				{
					Point const & point = flat.positions[at];
					// cap UVs: the outline's place in the shape bounds, v DOWN
					pushVertex(out, Vec3f(point.x, point.y, z), normal,
						Vec2f((point.x - bounds.minX) / sizeX,
							1.0f - (point.y - bounds.minY) / sizeY));
				}
				for(std::size_t at = 0; at < triangles; ++at)
				{
					const unsigned int i0 = flat.indices[at * 3 + 0];
					const unsigned int i1 = flat.indices[at * 3 + 1];
					const unsigned int i2 = flat.indices[at * 3 + 2];
					// the triangulator's winding is not guaranteed, so orient
					// each triangle by its own signed area: counter-clockwise
					// in XY faces +Z
					Point const & a = flat.positions[i0];
					Point const & b = flat.positions[i1];
					Point const & c = flat.positions[i2];
					const float twiceArea = (b.x - a.x) * (c.y - a.y) -
						(b.y - a.y) * (c.x - a.x);
					const bool counterClockwise = twiceArea > 0.0f;
					const bool keep = front ? counterClockwise
						: !counterClockwise;
					out.indices.push_back(static_cast<unsigned int>(base) + i0);
					out.indices.push_back(static_cast<unsigned int>(base) +
						(keep ? i1 : i2));
					out.indices.push_back(static_cast<unsigned int>(base) +
						(keep ? i2 : i1));
				}
			}
			// --- the side walls: the outer loop plus every hole -------------
			std::vector<Point> outer = ShapeCollider::openLoop(region.outer);
			orientLoop(outer, true);
			sweepWalls(out, outer, frontZ, backZ, smoothSides);
			for(std::size_t hole = 0; hole < region.holes.size(); ++hole)
			{
				std::vector<Point> inner =
					ShapeCollider::openLoop(region.holes[hole]);
				if(inner.size() < 3)
				{
					continue;
				}
				// a hole runs the OTHER way, so its (dy, -dx) normal points into
				// the cut-out - which is outward from the solid
				orientLoop(inner, false);
				sweepWalls(out, inner, frontZ, backZ, smoothSides);
			}
		}
		MeshBuilder::closeSection(out);
		if(out.indices.empty())
		{
			return refuse(out, outError,
				"extrude produced no geometry (every region degenerated)");
		}
		MeshBuilder::computeTangents(out);
		String validationError;
		if(!MeshBuilder::validate(out, &validationError))
		{
			return refuse(out, outError, validationError.c_str());
		}
		return true;
	}
	//---------------------------------------------------------
	bool MeshExtrude::revolveShape(Mesh & out,
		std::vector<VectorTessellator::Region> const & regions,
		int segments, float sweepDegrees, String * outError)
	{
		out.clear();
		std::vector<Point> profile;
		for(std::size_t each = 0; each < regions.size(); ++each)
		{
			if(ShapeCollider::isSolidRegion(regions[each]))
			{
				profile = ShapeCollider::openLoop(regions[each].outer);
				break;
			}
		}
		if(profile.size() < 2)
		{
			return refuse(out, outError,
				"revolve found no solid fill region to use as a profile");
		}
		for(std::size_t each = 0; each < profile.size(); ++each)
		{
			if(!MeshBuilder::isFinite(profile[each].x) ||
				!MeshBuilder::isFinite(profile[each].y))
			{
				return refuse(out, outError,
					"a revolve profile point is not finite");
			}
			if(profile[each].x < 0.0f)
			{
				return refuse(out, outError,
					"a revolve profile must stay on the x >= 0 half-plane "
					"(a point left of the Y axis would sweep through itself)");
			}
		}
		// walk the profile from its highest point DOWNWARD: the lathe's row
		// order decides which side faces out, and top-to-bottom is outward for
		// a silhouette (the same convention the round primitives use)
		std::size_t highest = 0;
		for(std::size_t each = 1; each < profile.size(); ++each)
		{
			if(profile[each].y > profile[highest].y)
			{
				highest = each;
			}
		}
		std::rotate(profile.begin(),
			profile.begin() + static_cast<std::ptrdiff_t>(highest),
			profile.end());
		if(profile.size() >= 2 && profile[1].y > profile.back().y)
		{
			// the loop climbs from the start: walk it the other way round
			std::reverse(profile.begin() + 1, profile.end());
		}
		// the profile normal at each point: perpendicular to the local
		// silhouette direction, pointing away from the axis for a downward walk
		std::vector<MeshShapes::ProfileRow> rows;
		rows.reserve(profile.size());
		const std::size_t count = profile.size();
		for(std::size_t each = 0; each < count; ++each)
		{
			Vec3f normal;
			int contributions = 0;
			if(each > 0)
			{
				const float dr = profile[each].x - profile[each - 1].x;
				const float dy = profile[each].y - profile[each - 1].y;
				normal = MeshBuilder::add(normal,
					MeshBuilder::normalise(Vec3f(-dy, dr, 0.0f),
						Vec3f(1.0f, 0.0f, 0.0f)));
				++contributions;
			}
			if(each + 1 < count)
			{
				const float dr = profile[each + 1].x - profile[each].x;
				const float dy = profile[each + 1].y - profile[each].y;
				normal = MeshBuilder::add(normal,
					MeshBuilder::normalise(Vec3f(-dy, dr, 0.0f),
						Vec3f(1.0f, 0.0f, 0.0f)));
				++contributions;
			}
			if(contributions == 0)
			{
				normal = Vec3f(1.0f, 0.0f, 0.0f);
			}
			normal = MeshBuilder::normalise(normal, Vec3f(1.0f, 0.0f, 0.0f));
			rows.push_back(MeshShapes::ProfileRow(profile[each].x,
				profile[each].y, normal.x, normal.y));
		}
		// the lathe every round primitive shares - one definition of the sweep
		return MeshShapes::revolveProfile(out, rows.data(), rows.size(),
			segments, sweepDegrees, outError);
	}
}
