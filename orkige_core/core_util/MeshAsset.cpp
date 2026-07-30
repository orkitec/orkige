/********************************************************************
	created:	Thursday 2026/07/30 at 09:30
	filename: 	MeshAsset.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "core_util/MeshAsset.h"

#include "core_util/MeshExtrude.h"
#include "core_util/MeshShapes.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace Orkige
{
	namespace
	{
		typedef MeshBuilder::Mesh Mesh;
		typedef MeshBuilder::Vec2f Vec2f;
		typedef MeshBuilder::Vec3f Vec3f;

		String lowered(String const & text)
		{
			String out = text;
			for(std::size_t each = 0; each < out.size(); ++each)
			{
				out[each] = static_cast<char>(std::tolower(
					static_cast<unsigned char>(out[each])));
			}
			return out;
		}
		//! strip a `#` line comment and the surrounding whitespace
		String cleanLine(String const & line)
		{
			String out = line;
			const std::size_t comment = out.find('#');
			if(comment != String::npos)
			{
				out.erase(comment);
			}
			const std::size_t first = out.find_first_not_of(" \t\r\n");
			if(first == String::npos)
			{
				return String();
			}
			const std::size_t last = out.find_last_not_of(" \t\r\n");
			return out.substr(first, last - first + 1);
		}
		bool tokenIsNumber(String const & token)
		{
			if(token.empty())
			{
				return false;
			}
			char* end = NULL;
			std::strtod(token.c_str(), &end);
			return end != NULL && *end == '\0' && end != token.c_str();
		}
		//! @brief a cursor over one line's tokens: every read either succeeds or
		//! records the FIRST problem, so a caller checks `failed` once at the end
		//! instead of after every field
		struct Cursor
		{
			std::vector<String>	tokens;
			std::size_t			at;
			bool				failed;
			String				problem;

			Cursor() : at(0), failed(false) {}
			bool done() const { return this->at >= this->tokens.size(); }
			String const & peek() const { return this->tokens[this->at]; }
			bool peekIsNumber() const
			{
				return !this->done() && tokenIsNumber(this->tokens[this->at]);
			}
			void fail(String const & reason)
			{
				if(!this->failed)
				{
					this->failed = true;
					this->problem = reason;
				}
			}
			String word(char const * what)
			{
				if(this->done())
				{
					this->fail(String("missing ") + what);
					return String();
				}
				return this->tokens[this->at++];
			}
			float number(char const * what)
			{
				if(this->done())
				{
					this->fail(String("missing ") + what);
					return 0.0f;
				}
				String const & token = this->tokens[this->at];
				if(!tokenIsNumber(token))
				{
					this->fail(String("'") + token + "' is not a number (" +
						what + ")");
					return 0.0f;
				}
				++this->at;
				return static_cast<float>(std::strtod(token.c_str(), NULL));
			}
			int integer(char const * what)
			{
				const float value = this->number(what);
				return static_cast<int>(value);
			}
			bool flag(char const * what)
			{
				const float value = this->number(what);
				return value != 0.0f;
			}
		};
		//! the per-shape placement/attribution modifiers every line may carry
		struct Modifiers
		{
			Vec3f		translation;
			Vec3f		rotation;
			Vec3f		scale;
			String		material;
			bool		hasMaterial;
			bool		hasUv;
			MeshBuilder::UvMode	uvMode;
			Vec2f		uvScale;
			int			normalMode;		//!< 0 = keep, 1 = smooth, 2 = flat
			float		smoothAngle;
			Modifiers() : scale(1.0f, 1.0f, 1.0f), hasMaterial(false),
				hasUv(false), uvMode(MeshBuilder::UV_BOX), uvScale(1.0f, 1.0f),
				normalMode(0), smoothAngle(60.0f) {}
		};
		bool parseUvMode(String const & name, MeshBuilder::UvMode & out)
		{
			const String key = lowered(name);
			if(key == "xz") { out = MeshBuilder::UV_PLANAR_XZ; return true; }
			if(key == "xy") { out = MeshBuilder::UV_PLANAR_XY; return true; }
			if(key == "zy") { out = MeshBuilder::UV_PLANAR_ZY; return true; }
			if(key == "box") { out = MeshBuilder::UV_BOX; return true; }
			if(key == "cylindrical")
			{
				out = MeshBuilder::UV_CYLINDRICAL;
				return true;
			}
			if(key == "spherical")
			{
				out = MeshBuilder::UV_SPHERICAL;
				return true;
			}
			return false;
		}
		//! @brief consume one trailing modifier key. Returns false when the key
		//! is not a modifier at all (the caller then reports it as unknown).
		bool readModifier(Cursor & cursor, String const & key,
			Modifiers & modifiers, bool & seenAt, bool & seenRotate,
			bool & seenScale, bool & seenUv, bool & seenNormals)
		{
			if(key == "at")
			{
				if(seenAt) { cursor.fail("'at' given twice"); return true; }
				seenAt = true;
				modifiers.translation.x = cursor.number("at x");
				modifiers.translation.y = cursor.number("at y");
				modifiers.translation.z = cursor.number("at z");
				return true;
			}
			if(key == "rotate")
			{
				if(seenRotate)
				{
					cursor.fail("'rotate' given twice");
					return true;
				}
				seenRotate = true;
				modifiers.rotation.x = cursor.number("rotate x");
				modifiers.rotation.y = cursor.number("rotate y");
				modifiers.rotation.z = cursor.number("rotate z");
				return true;
			}
			if(key == "scale")
			{
				if(seenScale)
				{
					cursor.fail("'scale' given twice");
					return true;
				}
				seenScale = true;
				const float first = cursor.number("scale");
				if(cursor.peekIsNumber())
				{
					modifiers.scale.x = first;
					modifiers.scale.y = cursor.number("scale y");
					modifiers.scale.z = cursor.number("scale z");
				}
				else
				{
					modifiers.scale = Vec3f(first, first, first);
				}
				return true;
			}
			if(key == "material")
			{
				if(modifiers.hasMaterial)
				{
					cursor.fail("'material' given twice");
					return true;
				}
				modifiers.material = cursor.word("material name");
				modifiers.hasMaterial = !cursor.failed;
				return true;
			}
			if(key == "uv")
			{
				if(seenUv) { cursor.fail("'uv' given twice"); return true; }
				seenUv = true;
				const String mode = cursor.word("uv mode");
				if(cursor.failed) { return true; }
				if(!parseUvMode(mode, modifiers.uvMode))
				{
					cursor.fail("'" + mode + "' is not a uv mode (xz, xy, zy, "
						"box, cylindrical, spherical)");
					return true;
				}
				modifiers.hasUv = true;
				if(cursor.peekIsNumber())
				{
					modifiers.uvScale.x = cursor.number("uv scale u");
					modifiers.uvScale.y = cursor.number("uv scale v");
				}
				return true;
			}
			if(key == "smooth")
			{
				if(seenNormals)
				{
					cursor.fail("'smooth' and 'flat' cannot both apply");
					return true;
				}
				seenNormals = true;
				modifiers.normalMode = 1;
				if(cursor.peekIsNumber())
				{
					modifiers.smoothAngle = cursor.number("smooth angle");
				}
				return true;
			}
			if(key == "flat")
			{
				if(seenNormals)
				{
					cursor.fail("'smooth' and 'flat' cannot both apply");
					return true;
				}
				seenNormals = true;
				modifiers.normalMode = 2;
				return true;
			}
			return false;
		}
	}
	//---------------------------------------------------------
	bool MeshAsset::isMeshAssetName(String const & fileName)
	{
		if(fileName.size() < 6)
		{
			return false;
		}
		return lowered(fileName.substr(fileName.size() - 6)) == ".omesh";
	}
	//---------------------------------------------------------
	bool MeshAsset::checkSyntax(String const & text, String * outError)
	{
		//! a placeholder outline so an `extrude`/`revolve` line's GRAMMAR and
		//! parameters are checked without a project to resolve against
		struct StubShapes : public ShapeSource
		{
			bool loadShape(String const &,
				std::vector<VectorTessellator::Region> & outRegions) const
				override
			{
				VectorTessellator::Region region;
				region.kind = VectorTessellator::REGION_FILL;
				region.outer.push_back(VectorTessellator::Point(0.0f, 0.0f));
				region.outer.push_back(VectorTessellator::Point(1.0f, 0.0f));
				region.outer.push_back(VectorTessellator::Point(1.0f, 1.0f));
				region.outer.push_back(VectorTessellator::Point(0.0f, 1.0f));
				outRegions.push_back(region);
				return true;
			}
		};
		StubShapes stub;
		Mesh scratch;
		return MeshAsset::parse(text, scratch, &stub, outError);
	}
	//---------------------------------------------------------
	StringVector MeshAsset::shapeReferences(String const & text)
	{
		StringVector out;
		std::istringstream lines(text);
		String line;
		while(std::getline(lines, line))
		{
			std::istringstream tokens(cleanLine(line));
			String token;
			bool wantShape = false;
			while(tokens >> token)
			{
				if(wantShape)
				{
					if(std::find(out.begin(), out.end(), token) == out.end())
					{
						out.push_back(token);
					}
					wantShape = false;
					continue;
				}
				wantShape = (lowered(token) == "shape");
			}
		}
		return out;
	}
	//---------------------------------------------------------
	bool MeshAsset::parse(String const & text, Mesh & out,
		ShapeSource const * shapes, String * outError)
	{
		out.clear();
		std::istringstream lines(text);
		String rawLine;
		unsigned int lineNumber = 0;
		bool sawAnyDirective = false;
		unsigned int shapeCount = 0;
		String defaultMaterial;
		while(std::getline(lines, rawLine))
		{
			++lineNumber;
			const String line = cleanLine(rawLine);
			if(line.empty())
			{
				continue;
			}
			Cursor cursor;
			{
				std::istringstream tokens(line);
				String token;
				while(tokens >> token)
				{
					cursor.tokens.push_back(token);
				}
			}
			const String directive = lowered(cursor.word("directive"));
			// --- version -------------------------------------------------
			if(directive == "version")
			{
				if(sawAnyDirective)
				{
					out.clear();
					if(outError)
					{
						std::ostringstream message;
						message << "line " << lineNumber
							<< ": 'version' must be the first directive";
						*outError = message.str();
					}
					return false;
				}
				sawAnyDirective = true;
				const int version = cursor.integer("version number");
				if(!cursor.failed && version != 1)
				{
					cursor.fail("only .omesh version 1 is supported");
				}
				if(!cursor.failed && !cursor.done())
				{
					cursor.fail("trailing '" + cursor.peek() +
						"' after the version");
				}
				if(cursor.failed)
				{
					out.clear();
					if(outError)
					{
						std::ostringstream message;
						message << "line " << lineNumber << ": "
							<< cursor.problem;
						*outError = message.str();
					}
					return false;
				}
				continue;
			}
			sawAnyDirective = true;
			// --- a standalone `material NAME` line -----------------------
			if(directive == "material" && cursor.tokens.size() == 2)
			{
				defaultMaterial = cursor.tokens[1];
				continue;
			}
			// --- a shape line --------------------------------------------
			Mesh piece;
			Modifiers modifiers;
			bool knownShape = true;
			bool built = false;
			String shapeError;
			if(directive == "box" || directive == "wedge")
			{
				const float sx = cursor.number("size x");
				const float sy = cursor.number("size y");
				const float sz = cursor.number("size z");
				if(!cursor.failed)
				{
					built = (directive == "box")
						? MeshShapes::box(piece, sx, sy, sz, &shapeError)
						: MeshShapes::wedge(piece, sx, sy, sz, &shapeError);
				}
			}
			else if(directive == "roundedbox")
			{
				const float sx = cursor.number("size x");
				const float sy = cursor.number("size y");
				const float sz = cursor.number("size z");
				float radius = 0.0f;
				int segments = 4;
				bool seenRadius = false;
				bool seenSegments = false;
				while(!cursor.failed && !cursor.done())
				{
					const String key = lowered(cursor.peek());
					if(key == "radius" && !seenRadius)
					{
						++cursor.at;
						seenRadius = true;
						radius = cursor.number("radius");
					}
					else if(key == "segments" && !seenSegments)
					{
						++cursor.at;
						seenSegments = true;
						segments = cursor.integer("segments");
					}
					else
					{
						break;
					}
				}
				if(!cursor.failed && !seenRadius)
				{
					cursor.fail("roundedbox needs a 'radius'");
				}
				if(!cursor.failed)
				{
					built = MeshShapes::roundedBox(piece, sx, sy, sz, radius,
						segments, &shapeError);
				}
			}
			else if(directive == "plane")
			{
				const float sx = cursor.number("size x");
				const float sz = cursor.number("size z");
				int segmentsX = 1;
				int segmentsZ = 1;
				if(!cursor.failed && !cursor.done() &&
					lowered(cursor.peek()) == "segments")
				{
					++cursor.at;
					segmentsX = cursor.integer("segments x");
					segmentsZ = cursor.integer("segments z");
				}
				if(!cursor.failed)
				{
					built = MeshShapes::plane(piece, sx, sz, segmentsX,
						segmentsZ, &shapeError);
				}
			}
			else if(directive == "stairs")
			{
				const float sx = cursor.number("size x");
				const float sy = cursor.number("size y");
				const float sz = cursor.number("size z");
				int steps = 8;
				if(!cursor.failed && !cursor.done() &&
					lowered(cursor.peek()) == "steps")
				{
					++cursor.at;
					steps = cursor.integer("steps");
				}
				if(!cursor.failed)
				{
					built = MeshShapes::stairs(piece, sx, sy, sz, steps,
						&shapeError);
				}
			}
			else if(directive == "sphere" || directive == "icosphere" ||
				directive == "cylinder" || directive == "cone" ||
				directive == "capsule" || directive == "torus" ||
				directive == "tube" || directive == "disc" ||
				directive == "arch" || directive == "extrude" ||
				directive == "revolve")
			{
				// the named-parameter shapes: read key/value pairs until a
				// token is no longer one of THIS shape's parameters
				float radius = 0.0f, height = 0.0f, inner = 0.0f, tube = 0.0f;
				float depth = 0.0f, span = 0.0f, legs = 0.0f, thickness = 0.0f;
				float sweep = 360.0f;
				int segments = -1, rings = -1, tubeSegments = -1;
				int subdivisions = 2;
				bool caps = true;
				bool smoothSides = false;
				String shapeRef;
				bool seen[16] = { false };
				while(!cursor.failed && !cursor.done())
				{
					const String key = lowered(cursor.peek());
					int slot = -1;
					if(key == "radius") slot = 0;
					else if(key == "height") slot = 1;
					else if(key == "inner") slot = 2;
					else if(key == "tube") slot = 3;
					else if(key == "depth") slot = 4;
					else if(key == "span") slot = 5;
					else if(key == "legs") slot = 6;
					else if(key == "thickness") slot = 7;
					else if(key == "segments") slot = 8;
					else if(key == "rings") slot = 9;
					else if(key == "tubesegments") slot = 10;
					else if(key == "subdivisions") slot = 11;
					else if(key == "caps") slot = 12;
					else if(key == "shape") slot = 13;
					else if(key == "sweep") slot = 14;
					else if(key == "smoothsides") slot = 15;
					if(slot < 0)
					{
						break;	// a trailing modifier (or an unknown key)
					}
					if(seen[slot])
					{
						cursor.fail("'" + key + "' given twice");
						break;
					}
					seen[slot] = true;
					++cursor.at;
					switch(slot)
					{
					case 0: radius = cursor.number("radius"); break;
					case 1: height = cursor.number("height"); break;
					case 2: inner = cursor.number("inner"); break;
					case 3: tube = cursor.number("tube"); break;
					case 4: depth = cursor.number("depth"); break;
					case 5: span = cursor.number("span"); break;
					case 6: legs = cursor.number("legs"); break;
					case 7: thickness = cursor.number("thickness"); break;
					case 8: segments = cursor.integer("segments"); break;
					case 9: rings = cursor.integer("rings"); break;
					case 10: tubeSegments = cursor.integer("tubesegments");
						break;
					case 11: subdivisions = cursor.integer("subdivisions");
						break;
					case 12: caps = cursor.flag("caps"); break;
					case 13: shapeRef = cursor.word("shape reference"); break;
					case 14: sweep = cursor.number("sweep"); break;
					default: smoothSides = true; break;
					}
				}
				if(!cursor.failed)
				{
					if(directive == "sphere")
					{
						built = MeshShapes::uvSphere(piece, radius,
							segments < 0 ? 24 : segments,
							rings < 0 ? 16 : rings, &shapeError);
					}
					else if(directive == "icosphere")
					{
						built = MeshShapes::icosphere(piece, radius,
							subdivisions, &shapeError);
					}
					else if(directive == "cylinder")
					{
						built = MeshShapes::cylinder(piece, radius, height,
							segments < 0 ? 24 : segments, caps, &shapeError);
					}
					else if(directive == "cone")
					{
						built = MeshShapes::cone(piece, radius, height,
							segments < 0 ? 24 : segments, caps, &shapeError);
					}
					else if(directive == "capsule")
					{
						built = MeshShapes::capsule(piece, radius, height,
							segments < 0 ? 24 : segments,
							rings < 0 ? 8 : rings, &shapeError);
					}
					else if(directive == "torus")
					{
						built = MeshShapes::torus(piece, radius, tube,
							segments < 0 ? 32 : segments,
							tubeSegments < 0 ? 16 : tubeSegments, &shapeError);
					}
					else if(directive == "tube")
					{
						built = MeshShapes::tube(piece, radius, inner, height,
							segments < 0 ? 24 : segments, caps, &shapeError);
					}
					else if(directive == "disc")
					{
						built = MeshShapes::disc(piece, radius, inner,
							segments < 0 ? 24 : segments, &shapeError);
					}
					else if(directive == "arch")
					{
						built = MeshShapes::arch(piece, span, legs, thickness,
							depth, segments < 0 ? 12 : segments, &shapeError);
					}
					else
					{
						// extrude / revolve: the shape reference is resolved
						// through the injected source (the parser reads no file)
						if(shapeRef.empty())
						{
							cursor.fail(directive +
								" needs a 'shape <file.oshape>'");
						}
						else
						{
							std::vector<VectorTessellator::Region> regions;
							if(!shapes || !shapes->loadShape(shapeRef, regions))
							{
								cursor.fail("shape '" + shapeRef +
									"' could not be loaded");
							}
							else if(directive == "extrude")
							{
								built = MeshExtrude::extrudeShape(piece,
									regions, depth, smoothSides, &shapeError);
							}
							else
							{
								built = MeshExtrude::revolveShape(piece,
									regions, segments < 0 ? 32 : segments,
									sweep, &shapeError);
							}
						}
					}
				}
			}
			else
			{
				knownShape = false;
			}
			if(!knownShape)
			{
				out.clear();
				if(outError)
				{
					std::ostringstream message;
					message << "line " << lineNumber << ": unknown directive '"
						<< cursor.tokens[0] << "'";
					*outError = message.str();
				}
				return false;
			}
			// --- the trailing modifiers ----------------------------------
			bool seenAt = false, seenRotate = false, seenScale = false;
			bool seenUv = false, seenNormals = false;
			while(!cursor.failed && !cursor.done())
			{
				const String key = lowered(cursor.word("modifier"));
				if(cursor.failed)
				{
					break;
				}
				if(!readModifier(cursor, key, modifiers, seenAt, seenRotate,
					seenScale, seenUv, seenNormals))
				{
					cursor.fail("unknown key '" + key + "' on a " + directive);
				}
			}
			if(cursor.failed || !built)
			{
				out.clear();
				if(outError)
				{
					std::ostringstream message;
					message << "line " << lineNumber << ": "
						<< (cursor.failed ? cursor.problem : shapeError);
					*outError = message.str();
				}
				return false;
			}
			// --- finish the piece and merge it in ------------------------
			if(modifiers.normalMode == 1)
			{
				MeshBuilder::computeSmoothNormals(piece, modifiers.smoothAngle);
			}
			else if(modifiers.normalMode == 2)
			{
				MeshBuilder::computeFlatNormals(piece);
			}
			if(modifiers.hasUv)
			{
				MeshBuilder::applyUV(piece, modifiers.uvMode,
					modifiers.uvScale);
			}
			if(modifiers.normalMode != 0 || modifiers.hasUv)
			{
				// normals or UVs moved, so the tangent frame must follow
				MeshBuilder::computeTangents(piece);
			}
			const String material = modifiers.hasMaterial
				? modifiers.material : defaultMaterial;
			MeshBuilder::append(out, piece, MeshBuilder::Xform::fromTRS(
				modifiers.translation, modifiers.rotation, modifiers.scale),
				material);
			++shapeCount;
		}
		if(shapeCount == 0)
		{
			out.clear();
			if(outError)
			{
				*outError = "line 1: the .omesh carries no shape directive";
			}
			return false;
		}
		String validationError;
		if(!MeshBuilder::validate(out, &validationError))
		{
			out.clear();
			if(outError)
			{
				*outError = "line 1: " + validationError;
			}
			return false;
		}
		return true;
	}
}
