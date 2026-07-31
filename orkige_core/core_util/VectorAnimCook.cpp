/********************************************************************
	created:	Thursday 2026/07/31 at 10:00
	filename: 	VectorAnimCook.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file VectorAnimCook.cpp
//! @brief the vector-animation cook's document walk, block conversion, rig
//! assembly and text emission (@see VectorAnimCook.h)

#include "core_util/VectorAnimCookDetail.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>

namespace Orkige
{
	using namespace VectorAnimCookDetail;

	namespace
	{
		//! the provenance banner every cooked asset carries
		char const * const OANIM_BANNER =
			"# orkige vector animation v%d - cooked from Lottie JSON by "
			"Util/cook_vector_anim.py";
		char const * const OSHAPE_BANNER =
			"# orkige vector shape v2 - cooked from a static Lottie "
			"document by Util/cook_vector_anim.py";

		//! the layer kinds the cook can name in a refusal
		String layerTypeName(JsonValue const * ty)
		{
			if (ty != nullptr && ty->isNumber())
			{
				switch (static_cast<int>(ty->asNumber()))
				{
				case 0: return "precomp";
				case 1: return "solid";
				case 2: return "image";
				case 3: return "null";
				case 4: return "shape";
				case 5: return "text";
				case 6: return "audio";
				case 7: return "video placeholder";
				case 8: return "image sequence";
				case 9: return "video";
				case 13: return "camera";
				default: break;
				}
			}
			return "type " + jsonStr(ty);
		}
		//! the shape items that always refuse, with the name of the feature
		char const * shapeItemError(String const & ty)
		{
			if (ty == "rp") { return "repeater"; }
			if (ty == "zz") { return "zig-zag"; }
			if (ty == "op") { return "offset path"; }
			if (ty == "tw") { return "twist"; }
			return nullptr;
		}

		//=====================================================
		// document walk: subset validation, precomp inlining
		//=====================================================
		//-----------------------------------------------------
		void checkExpressions(Context & ctx, JsonValue const * prop,
			String const & layerName, String const & what)
		{
			if (hasExpression(prop))
			{
				ctx.addError(formatText("expression on %s of layer '%s' - "
					"expressions are not supported; bake them to keyframes",
					what.c_str(), layerName.c_str()));
			}
		}
		//-----------------------------------------------------
		//! the transform-group property, honouring a resolved link expression
		JsonValue const * ksProp(Context const & ctx, JsonValue const * ks,
			char const * name)
		{
			auto found = ctx.linkOverrides.find(std::make_pair(ks,
				String(name)));
			if (found != ctx.linkOverrides.end())
			{
				return found->second.isNull() ? nullptr : &found->second;
			}
			return member(ks, name);
		}
		//-----------------------------------------------------
		void validateTransform(Context & ctx, JsonValue const * ks,
			String const & layerName)
		{
			if (ks == nullptr || !ks->isObject())
			{
				return;
			}
			static char const * const NAMES[5] = { "p", "a", "s", "r", "o" };
			static char const * const WHAT[5] = { "position", "anchor",
				"scale", "rotation", "opacity" };
			for (int index = 0; index < 5; ++index)
			{
				JsonValue const * prop = ksProp(ctx, ks, NAMES[index]);
				checkExpressions(ctx, prop, layerName, WHAT[index]);
				if (index == 0 && prop != nullptr && prop->isObject() &&
					truthy(member(prop, "s")))
				{
					checkExpressions(ctx, member(prop, "x"), layerName,
						"position.x");
					checkExpressions(ctx, member(prop, "y"), layerName,
						"position.y");
				}
			}
			JsonValue const * skew = ksProp(ctx, ks, "sk");
			if (isAnimated(skew, 1) || std::fabs(staticValue(skew, 1,
				std::vector<double>(1, 0.0))[0]) > EPS)
			{
				ctx.addError(formatText("skew on layer '%s' - not supported",
					layerName.c_str()));
			}
		}
		//-----------------------------------------------------
		void validateStrokeDashes(Context & ctx, JsonValue const * style,
			String const & layerName)
		{
			JsonValue const * entries = member(style, "d");
			if (entries == nullptr || !entries->isArray() ||
				entries->size() == 0)
			{
				return;
			}
			int patternIndex = 0;
			bool offsetSeen = false;
			size_t count = entries->size();
			for (size_t index = 0; index < count; ++index)
			{
				JsonValue const & entry = entries->at(index);
				JsonValue const * type = member(&entry, "n");
				String dashType = type != nullptr ? type->asString()
					: String("d");
				checkExpressions(ctx, member(&entry, "v"), layerName,
					"stroke dash " + dashType);
				if (dashType == "o")
				{
					if (offsetSeen || index != count - 1)
					{
						ctx.addError(formatText("stroke dash offset on layer "
							"'%s' must appear once at the end",
							layerName.c_str()));
					}
					offsetSeen = true;
					continue;
				}
				char const * expected = patternIndex % 2 == 0 ? "d" : "g";
				if (dashType != expected)
				{
					ctx.addError(formatText("stroke dash pattern on layer '%s' "
						"must alternate dash/gap starting with dash",
						layerName.c_str()));
					return;
				}
				++patternIndex;
			}
			if (patternIndex == 0)
			{
				ctx.addError(formatText("stroke dash pattern on layer '%s' has "
					"no dash/gap entries", layerName.c_str()));
			}
		}
		//-----------------------------------------------------
		void validateGradientStyle(Context & ctx, JsonValue const * style,
			String const & layerName)
		{
			JsonValue const * gradient = member(style, "g");
			checkExpressions(ctx, member(gradient, "k"), layerName,
				"gradient stops");
			checkExpressions(ctx, member(style, "s"), layerName,
				"gradient start");
			checkExpressions(ctx, member(style, "e"), layerName,
				"gradient end");
			checkExpressions(ctx, member(style, "o"), layerName,
				"gradient opacity");
			checkExpressions(ctx, member(style, "h"), layerName,
				"gradient highlight");
			checkExpressions(ctx, member(style, "a"), layerName,
				"gradient highlight angle");
		}
		//-----------------------------------------------------
		//! the item's `ty` as a string ("" when absent or not a string)
		String itemType(JsonValue const * item)
		{
			JsonValue const * type = member(item, "ty");
			return type != nullptr && type->isString() ? type->asString()
				: String();
		}
		//-----------------------------------------------------
		void walkShapeItems(Context & ctx, JsonValue const * items,
			String const & layerName,
			std::vector<JsonValue const *> groupAffines,
			std::vector<JsonValue const *> groupOpacities,
			std::vector<Block> & blocks, JsonValue const * inheritedTrim,
			std::vector<JsonValue const *> const & inheritedModifiers)
		{
			if (items == nullptr || !items->isArray())
			{
				return;
			}
			size_t count = items->size();
			// the group transform rides as the (by convention last) `tr` item
			// but applies to the WHOLE group: bind it before walking the paints
			for (size_t index = 0; index < count; ++index)
			{
				JsonValue const * item = &items->at(index);
				if (itemType(item) != "tr" || truthy(member(item, "hd")))
				{
					continue;
				}
				// Group transforms are baked into the shape poses. This keeps
				// the runtime rig small while preserving the hierarchy used by
				// authored character parts.
				static char const * const NAMES[4] = { "p", "a", "s", "r" };
				static char const * const WHAT[4] = { "position", "anchor",
					"scale", "rotation" };
				for (int which = 0; which < 4; ++which)
				{
					JsonValue const * prop = member(item, NAMES[which]);
					checkExpressions(ctx, prop, layerName,
						String("group ") + WHAT[which]);
					if (which == 0 && prop != nullptr && prop->isObject() &&
						truthy(member(prop, "s")))
					{
						checkExpressions(ctx, member(prop, "x"), layerName,
							"group position.x");
						checkExpressions(ctx, member(prop, "y"), layerName,
							"group position.y");
					}
				}
				JsonValue const * opacity = member(item, "o");
				checkExpressions(ctx, opacity, layerName, "group opacity");
				if (opacity != nullptr)
				{
					groupOpacities.push_back(opacity);
				}
				groupAffines.push_back(item);
			}

			// A trim in a containing group applies to preceding nested path
			// groups. The common simultaneous mode (m=1) is preserved as one
			// shared length domain by attaching the modifier to every affected
			// stroke block.
			JsonValue const * trim = inheritedTrim;
			std::vector<JsonValue const *> modifiers = inheritedModifiers;
			for (size_t index = 0; index < count; ++index)
			{
				JsonValue const * item = &items->at(index);
				String ty = itemType(item);
				if (truthy(member(item, "hd")))
				{
					continue;
				}
				if (ty == "tm")
				{
					if (intOr(item, "m", 1) != 1)
					{
						ctx.addError(formatText("sequential trim paths on "
							"layer '%s' are not supported; use parallel trim "
							"or bake the result", layerName.c_str()));
					}
					trim = item;
				}
				else if (ty == "rd")
				{
					checkExpressions(ctx, member(item, "r"), layerName,
						"rounded corners");
					modifiers.push_back(item);
				}
				else if (ty == "pb")
				{
					checkExpressions(ctx, member(item, "a"), layerName,
						"pucker/bloat");
					modifiers.push_back(item);
				}
			}

			std::vector<Block::PathRef> paths;
			for (size_t index = 0; index < count; ++index)
			{
				JsonValue const * item = &items->at(index);
				String ty = itemType(item);
				JsonValue const * nameValue = member(item, "nm");
				String name = nameValue != nullptr && nameValue->isString()
					? nameValue->asString() : String();
				if (truthy(member(item, "hd")) || ty == "tr")
				{
					continue;	// hidden items never render; tr is bound above
				}
				char const * refused = shapeItemError(ty);
				if (refused != nullptr)
				{
					String suffix = name.empty() ? String()
						: formatText(" (item '%s')", name.c_str());
					ctx.addError(formatText("%s on layer '%s'%s - not "
						"supported", refused, layerName.c_str(),
						suffix.c_str()));
				}
				else if (ty == "gr")
				{
					walkShapeItems(ctx, member(item, "it"), layerName,
						groupAffines, groupOpacities, blocks, trim, modifiers);
				}
				else if (ty == "sh")
				{
					checkExpressions(ctx, member(item, "ks"), layerName,
						"path");
					paths.push_back(Block::PathRef("sh", item));
				}
				else if (ty == "el")
				{
					checkExpressions(ctx, member(item, "p"), layerName,
						"ellipse");
					checkExpressions(ctx, member(item, "s"), layerName,
						"ellipse");
					paths.push_back(Block::PathRef("el", item));
				}
				else if (ty == "rc")
				{
					checkExpressions(ctx, member(item, "p"), layerName, "rect");
					checkExpressions(ctx, member(item, "s"), layerName, "rect");
					checkExpressions(ctx, member(item, "r"), layerName, "rect");
					paths.push_back(Block::PathRef("rc", item));
				}
				else if (ty == "sr")
				{
					static char const * const STAR[7] = { "pt", "p", "r", "or",
						"os", "ir", "is" };
					for (char const * which : STAR)
					{
						checkExpressions(ctx, member(item, which), layerName,
							String("polystar ") + which);
					}
					paths.push_back(Block::PathRef("sr", item));
				}
				else if (ty == "fl")
				{
					checkExpressions(ctx, member(item, "c"), layerName,
						"fill colour");
					checkExpressions(ctx, member(item, "o"), layerName,
						"fill opacity");
					if (paths.empty())
					{
						continue;	// a fill with nothing to style is invisible
					}
					Block block;
					block.paths = paths;
					block.fill = item;
					block.kind = "fill";
					block.affines = groupAffines;
					block.opacities = groupOpacities;
					block.modifiers = modifiers;
					block.layer = layerName;
					blocks.push_back(block);
				}
				else if (ty == "gf")
				{
					validateGradientStyle(ctx, item, layerName);
					if (!paths.empty())
					{
						Block block;
						block.paths = paths;
						block.fill = item;
						block.kind = "gradient_fill";
						block.affines = groupAffines;
						block.opacities = groupOpacities;
						block.modifiers = modifiers;
						block.layer = layerName;
						blocks.push_back(block);
					}
				}
				else if (ty == "st")
				{
					checkExpressions(ctx, member(item, "c"), layerName,
						"stroke c");
					checkExpressions(ctx, member(item, "o"), layerName,
						"stroke o");
					checkExpressions(ctx, member(item, "w"), layerName,
						"stroke w");
					validateStrokeDashes(ctx, item, layerName);
					if (!paths.empty())
					{
						Block block;
						block.paths = paths;
						block.fill = item;
						block.kind = "stroke";
						block.trim = trim;
						block.affines = groupAffines;
						block.opacities = groupOpacities;
						block.modifiers = modifiers;
						block.layer = layerName;
						blocks.push_back(block);
					}
				}
				else if (ty == "gs")
				{
					validateGradientStyle(ctx, item, layerName);
					validateStrokeDashes(ctx, item, layerName);
					if (!paths.empty())
					{
						Block block;
						block.paths = paths;
						block.fill = item;
						block.kind = "gradient_stroke";
						block.trim = trim;
						block.affines = groupAffines;
						block.opacities = groupOpacities;
						block.modifiers = modifiers;
						block.layer = layerName;
						blocks.push_back(block);
					}
				}
				else if (ty == "tm" || ty == "rd" || ty == "pb")
				{
					continue;	// consumed above, inherited by nested groups
				}
				else if (ty == "mm")
				{
					// Mode 1 is additive merge. Keeping its input contours is
					// equivalent for an opaque fill and for subsequent strokes;
					// the tessellator handles the individual contours. Boolean
					// subtract, intersect and exclusion need a clipping stage.
					if (intOr(item, "mm", 1) != 1)
					{
						ctx.addError(formatText("merge paths mode %s on layer "
							"'%s' - only additive merge is supported",
							jsonStr(member(item, "mm")).c_str(),
							layerName.c_str()));
					}
				}
				else
				{
					ctx.addError(formatText("unsupported shape item '%s' on "
						"layer '%s' - not in the cook subset", ty.c_str(),
						layerName.c_str()));
				}
			}
		}

		//=====================================================
		// link expressions
		//=====================================================
		//-----------------------------------------------------
		//! @brief match the declarative layer-transform link expression at
		//! `text`, anywhere in the string.
		//! @remarks It reads `thisComp.layer("NAME").transform.PROPERTY`, the
		//! one expression form that is a reference rather than a program and
		//! maps losslessly to the rig; everything else stays unresolved and is
		//! refused honestly by validation.
		bool matchLinkExpression(String const & text, String & layerName,
			String & propertyName)
		{
			static char const * const HEAD = "thisComp.layer(";
			static char const * const MIDDLE = ").transform.";
			static char const * const PROPERTIES[5] = { "position",
				"anchorPoint", "scale", "rotation", "opacity" };
			size_t headLength = std::strlen(HEAD);
			for (size_t start = 0; start + headLength <= text.size(); ++start)
			{
				if (text.compare(start, headLength, HEAD) != 0)
				{
					continue;
				}
				size_t cursor = start + headLength;
				while (cursor < text.size() &&
					std::strchr(" \t\n\r\f\v", text[cursor]) != nullptr &&
					text[cursor] != '\0')
				{
					++cursor;
				}
				if (cursor >= text.size() ||
					(text[cursor] != '\'' && text[cursor] != '"'))
				{
					continue;
				}
				char quote = text[cursor];
				size_t nameStart = cursor + 1;
				// the capture is non-greedy: try the earliest closing quote
				// that lets the rest of the pattern match
				for (size_t nameEnd = nameStart; nameEnd < text.size();
					++nameEnd)
				{
					if (text[nameEnd] == '\n')
					{
						break;			// `.` never matches a newline
					}
					if (text[nameEnd] != quote)
					{
						continue;
					}
					size_t after = nameEnd + 1;
					while (after < text.size() &&
						std::strchr(" \t\n\r\f\v", text[after]) != nullptr &&
						text[after] != '\0')
					{
						++after;
					}
					if (text.compare(after, std::strlen(MIDDLE), MIDDLE) != 0)
					{
						continue;
					}
					size_t propertyStart = after + std::strlen(MIDDLE);
					for (char const * property : PROPERTIES)
					{
						size_t length = std::strlen(property);
						if (text.compare(propertyStart, length, property) != 0)
						{
							continue;
						}
						size_t end = propertyStart + length;
						if (end < text.size())
						{
							char next = text[end];
							bool word = (next >= 'a' && next <= 'z') ||
								(next >= 'A' && next <= 'Z') ||
								(next >= '0' && next <= '9') || next == '_';
							if (word)
							{
								continue;	// the \b word boundary failed
							}
						}
						layerName = text.substr(nameStart,
							nameEnd - nameStart);
						propertyName = property;
						return true;
					}
				}
			}
			return false;
		}
		//-----------------------------------------------------
		//! @brief bake the declarative link expression form into a copy of the
		//! referenced property, so normal channel conversion sees plain
		//! keyframes.
		void resolveLinkComp(Context & ctx, JsonValue const * layers)
		{
			if (layers == nullptr || !layers->isArray())
			{
				return;
			}
			std::map<String, JsonValue const *> byName;
			for (size_t index = 0; index < layers->size(); ++index)
			{
				JsonValue const & layer = layers->at(index);
				if (!layer.isObject())
				{
					continue;
				}
				JsonValue const * name = member(&layer, "nm");
				byName[name != nullptr && name->isString()
					? name->asString() : String()] = &layer;
			}
			static char const * const KEYS[5] = { "p", "a", "s", "r", "o" };
			for (size_t index = 0; index < layers->size(); ++index)
			{
				JsonValue const & layer = layers->at(index);
				if (!layer.isObject())
				{
					continue;
				}
				JsonValue const * ks = member(&layer, "ks");
				if (ks == nullptr || !ks->isObject())
				{
					continue;
				}
				for (char const * key : KEYS)
				{
					JsonValue const * prop = member(ks, key);
					if (prop == nullptr || !prop->isObject())
					{
						continue;
					}
					JsonValue const & expression = prop->get("x");
					if (!expression.isString())
					{
						continue;
					}
					String targetName;
					String propertyName;
					if (!matchLinkExpression(expression.asString(), targetName,
						propertyName))
					{
						continue;
					}
					auto found = byName.find(targetName);
					if (found == byName.end())
					{
						continue;
					}
					JsonValue const * targetKs = member(found->second, "ks");
					if (targetKs == nullptr || !targetKs->isObject())
					{
						continue;
					}
					char const * sourceKey = "p";
					if (propertyName == "anchorPoint") { sourceKey = "a"; }
					else if (propertyName == "scale") { sourceKey = "s"; }
					else if (propertyName == "rotation") { sourceKey = "r"; }
					else if (propertyName == "opacity") { sourceKey = "o"; }
					JsonValue const * source = ksProp(ctx, targetKs, sourceKey);
					if (source != nullptr)
					{
						ctx.linkOverrides[std::make_pair(ks, String(key))] =
							*source;
					}
				}
			}
		}
		//-----------------------------------------------------
		void resolveLinkExpressions(Context & ctx)
		{
			resolveLinkComp(ctx, member(&ctx.document, "layers"));
			JsonValue const * assets = member(&ctx.document, "assets");
			if (assets != nullptr && assets->isArray())
			{
				for (size_t index = 0; index < assets->size(); ++index)
				{
					JsonValue const & asset = assets->at(index);
					if (asset.isObject())
					{
						resolveLinkComp(ctx, member(&asset, "layers"));
					}
				}
			}
		}

		//=====================================================
		// solid / image layers
		//=====================================================
		//-----------------------------------------------------
		//! a static `{"a":0,"k":<value>}` property held by the context
		JsonValue const * synthProp(Context & ctx, JsonValue value)
		{
			JsonValue prop = JsonValue::object();
			prop.set("a", JsonValue(0.0));
			prop.set("k", std::move(value));
			ctx.synthetic.push_back(std::move(prop));
			return &ctx.synthetic.back();
		}
		//-----------------------------------------------------
		JsonValue const * synthRect(Context & ctx, double w, double h)
		{
			JsonValue center = JsonValue::array();
			center.push(JsonValue(w * 0.5));
			center.push(JsonValue(h * 0.5));
			JsonValue size = JsonValue::array();
			size.push(JsonValue(w));
			size.push(JsonValue(h));
			JsonValue rect = JsonValue::object();
			rect.set("ty", JsonValue("rc"));
			rect.set("p", *synthProp(ctx, std::move(center)));
			rect.set("s", *synthProp(ctx, std::move(size)));
			rect.set("r", *synthProp(ctx, JsonValue(0.0)));
			ctx.synthetic.push_back(std::move(rect));
			return &ctx.synthetic.back();
		}
		//-----------------------------------------------------
		JsonValue const * synthFill(Context & ctx, double r, double g, double b)
		{
			JsonValue colour = JsonValue::array();
			colour.push(JsonValue(r));
			colour.push(JsonValue(g));
			colour.push(JsonValue(b));
			colour.push(JsonValue(1.0));
			JsonValue fill = JsonValue::object();
			fill.set("ty", JsonValue("fl"));
			fill.set("c", *synthProp(ctx, std::move(colour)));
			fill.set("o", *synthProp(ctx, JsonValue(100.0)));
			ctx.synthetic.push_back(std::move(fill));
			return &ctx.synthetic.back();
		}
		//-----------------------------------------------------
		//! a solid layer as one static rect block filled with its colour
		Block solidBlock(Context & ctx, JsonValue const * raw,
			String const & name)
		{
			double sw = numberOr(raw, "sw", 0.0);
			double sh = numberOr(raw, "sh", 0.0);
			JsonValue const * colourValue = member(raw, "sc");
			String colour = colourValue != nullptr && colourValue->isString()
				? colourValue->asString() : String("#000000");
			while (!colour.empty() && colour[0] == '#')
			{
				colour.erase(colour.begin());
			}
			double rgb[3] = { 0.0, 0.0, 0.0 };
			bool readable = colour.size() >= 6;
			for (int channel = 0; channel < 3 && readable; ++channel)
			{
				char const * digits = "0123456789abcdefABCDEF";
				char high = colour[static_cast<size_t>(channel) * 2];
				char low = colour[static_cast<size_t>(channel) * 2 + 1];
				if (std::strchr(digits, high) == nullptr || high == '\0' ||
					std::strchr(digits, low) == nullptr || low == '\0')
				{
					readable = false;
					break;
				}
				char pair[3] = { high, low, '\0' };
				rgb[channel] = static_cast<double>(
					strtol(pair, nullptr, 16)) / 255.0;
			}
			if (!readable)
			{
				ctx.addError(formatText("solid layer '%s' has an unreadable "
					"colour '%s'", name.c_str(),
					jsonStr(colourValue).c_str()));
				rgb[0] = rgb[1] = rgb[2] = 0.0;
			}
			Block block;
			block.paths.push_back(Block::PathRef("rc",
				synthRect(ctx, sw, sh)));
			block.fill = synthFill(ctx, rgb[0], rgb[1], rgb[2]);
			block.layer = name;
			return block;
		}
		//-----------------------------------------------------
		//! decode a base64 payload; false on malformed input
		bool decodeBase64(String const & text, std::vector<unsigned char> & out)
		{
			static char const * const ALPHABET =
				"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
				"0123456789+/";
			int bits = 0;
			int accumulator = 0;
			for (char c : text)
			{
				if (c == '=' || c == '\n' || c == '\r')
				{
					continue;
				}
				char const * found = std::strchr(ALPHABET, c);
				if (found == nullptr || c == '\0')
				{
					return false;
				}
				accumulator = (accumulator << 6) |
					static_cast<int>(found - ALPHABET);
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					out.push_back(static_cast<unsigned char>(
						(accumulator >> bits) & 0xFF));
				}
			}
			return true;
		}
		//-----------------------------------------------------
		//! @brief an image layer as one static rect block carrying a texture
		//! paint: the image pasted into its layer-local w x h rect (top-left at
		//! the layer origin, like a solid), animated by the layer transform
		//! channels. The referenced file is recorded for the caller to
		//! materialize beside the cooked output; the emitted texture name is
		//! its bare file name.
		bool imageBlock(Context & ctx, JsonValue const * raw,
			String const & name,
			std::map<String, JsonValue const *> const & assets, Block & out)
		{
			JsonValue const * refValue = member(raw, "refId");
			String ref = refValue != nullptr && refValue->isString()
				? refValue->asString() : String();
			auto found = assets.find(ref);
			if (refValue == nullptr || found == assets.end())
			{
				ctx.addError(formatText("image layer '%s' references missing "
					"image asset '%s'", name.c_str(),
					jsonStr(refValue).c_str()));
				return false;
			}
			JsonValue const * asset = found->second;
			double w = numberOr(asset, "w", 0.0);
			double h = numberOr(asset, "h", 0.0);
			JsonValue const * pathValue = member(asset, "p");
			String p = pathValue != nullptr && pathValue->isString()
				? pathValue->asString() : String();
			if (w <= 0.0 || h <= 0.0 || p.empty())
			{
				ctx.addError(formatText("image asset '%s' of layer '%s' needs "
					"w/h and a file reference (p)", ref.c_str(), name.c_str()));
				return false;
			}
			VectorAnimCook::Image entry;
			String imageName;
			if (p.compare(0, 5, "data:") == 0)
			{
				static char const * const PREFIX = "data:image/png;base64,";
				if (p.compare(0, std::strlen(PREFIX), PREFIX) != 0)
				{
					ctx.addError(formatText("embedded image on layer '%s' must "
						"be a base64 PNG data URI", name.c_str()));
					return false;
				}
				if (!decodeBase64(p.substr(std::strlen(PREFIX)), entry.data))
				{
					ctx.addError(formatText("embedded image on layer '%s' has "
						"unreadable base64 data", name.c_str()));
					return false;
				}
				entry.embedded = true;
				imageName = sanitizeName(ref,
					formatText("image%d",
						static_cast<int>(ctx.images.size()))) + ".png";
			}
			else
			{
				String slashed = p;
				for (char & c : slashed)
				{
					if (c == '\\') { c = '/'; }
				}
				size_t slash = slashed.rfind('/');
				imageName = slash == String::npos ? slashed
					: slashed.substr(slash + 1);
				JsonValue const * base = member(asset, "u");
				entry.source = (base != nullptr && base->isString()
					? base->asString() : String()) + p;
			}
			for (char c : imageName)
			{
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
					c == '\f' || c == '\v')
				{
					ctx.addError(formatText("image file name '%s' on layer "
						"'%s' contains whitespace - the texture grammar is one "
						"token", imageName.c_str(), name.c_str()));
					return false;
				}
			}
			entry.name = imageName;
			bool known = false;
			for (VectorAnimCook::Image const & existing : ctx.images)
			{
				if (existing.name != imageName)
				{
					continue;
				}
				if (existing.data != entry.data ||
					existing.embedded != entry.embedded ||
					existing.source != entry.source)
				{
					ctx.addError(formatText("two different image assets share "
						"the file name '%s'", imageName.c_str()));
					return false;
				}
				known = true;
				break;
			}
			if (!known)
			{
				ctx.images.push_back(entry);
			}
			out = Block();
			out.paths.push_back(Block::PathRef("rc", synthRect(ctx, w, h)));
			out.fill = synthFill(ctx, 1.0, 1.0, 1.0);
			out.texture = imageName;
			out.layer = name;
			return true;
		}

		//=====================================================
		// the flat, precomp-inlined layer list
		//=====================================================
		//-----------------------------------------------------
		void walkLayers(Context & ctx, JsonValue const * layers,
			String const & prefix, double offset, FlatEntry * parentOfRoot,
			std::vector<String> const & refStack,
			std::map<String, JsonValue const *> const & assets, double compIp)
		{
			if (layers == nullptr || !layers->isArray())
			{
				return;
			}
			std::map<double, FlatEntry *> byInd;
			std::vector<std::pair<FlatEntry *, JsonValue const *> >
				localEntries;
			for (size_t index = 0; index < layers->size(); ++index)
			{
				JsonValue const * raw = &layers->at(index);
				JsonValue const * tyValue = member(raw, "ty");
				int ty = tyValue != nullptr && tyValue->isNumber()
					? static_cast<int>(tyValue->asNumber()) : -1;
				String name = sanitizeName(member(raw, "nm"),
					formatText("layer%d", static_cast<int>(ctx.flat.size())));
				if (!prefix.empty())
				{
					name = prefix + "/" + name;
				}
				if (truthy(member(raw, "hd")))
				{
					continue;		// hidden layers never render
				}
				if (tyValue == nullptr || !tyValue->isNumber() ||
					ty < 0 || ty > 4)
				{
					ctx.addError(formatText("unsupported %s layer '%s' - only "
						"shape, null, solid, image and untimed precomp layers "
						"cook", layerTypeName(tyValue).c_str(), name.c_str()));
					continue;
				}
				if (intOr(raw, "ddd", 0) == 1)
				{
					ctx.addError(formatText("3D layer '%s' - only 2D layers "
						"cook", name.c_str()));
					continue;
				}
				if (std::fabs(numberOr(raw, "sr", 1.0) - 1.0) > EPS)
				{
					ctx.addError(formatText("time stretch on layer '%s' - not "
						"supported; bake the retime into keyframes",
						name.c_str()));
					continue;
				}
				if (member(raw, "tm") != nullptr)
				{
					ctx.addError(formatText("time remap on layer '%s' - not "
						"supported; bake the retime into keyframes",
						name.c_str()));
					continue;
				}
				if (truthy(member(raw, "tt")) || truthy(member(raw, "td")))
				{
					ctx.addError(formatText("track matte on layer '%s' - not "
						"supported", name.c_str()));
					continue;
				}
				std::vector<JsonValue const *> masks;
				JsonValue const * maskList = member(raw, "masksProperties");
				if (maskList != nullptr && maskList->isArray())
				{
					for (size_t m = 0; m < maskList->size(); ++m)
					{
						masks.push_back(&maskList->at(m));
					}
				}
				for (JsonValue const * mask : masks)
				{
					JsonValue const * modeValue = member(mask, "mode");
					String mode = modeValue != nullptr && modeValue->isString()
						? modeValue->asString() : String("a");
					bool inverted = truthy(member(mask, "inv"));
					if (mode != "a" || inverted)
					{
						ctx.addError(formatText("mask mode '%s'%s on layer "
							"'%s' - this vector cook currently supports "
							"additive non-inverted masks", mode.c_str(),
							inverted ? " inverted" : "", name.c_str()));
					}
					checkExpressions(ctx, member(mask, "pt"), name,
						"mask path");
					if (std::fabs(staticValue(member(mask, "x"), 1,
						std::vector<double>(1, 0.0))[0]) > EPS)
					{
						ctx.addError(formatText("expanded mask on layer '%s' - "
							"not supported", name.c_str()));
					}
				}
				if (truthy(member(raw, "ef")))
				{
					ctx.addError(formatText("layer effects on layer '%s' - not "
						"supported", name.c_str()));
					continue;
				}
				if (intOr(raw, "ao", 0) == 1)
				{
					ctx.addError(formatText("auto-orient on layer '%s' - not "
						"supported", name.c_str()));
					continue;
				}
				JsonValue const * ks = member(raw, "ks");
				validateTransform(ctx, ks, name);

				ctx.entryPool.push_back(FlatEntry());
				FlatEntry * entry = &ctx.entryPool.back();
				entry->name = name;
				entry->ty = ty;
				entry->ks = ks;
				entry->offset = offset;
				entry->windowStart = numberOr(raw, "ip", compIp) + offset;
				entry->windowEnd = numberOr(raw, "op", 1e30) + offset;
				entry->parent = parentOfRoot;
				entry->inheritOpacity = false;
				// Paint order is top-first and parents commonly appear later in
				// the layer array. Resolve after this composition has been
				// walked instead of requiring a backward reference.
				localEntries.push_back(std::make_pair(entry, raw));

				JsonValue const * indValue = member(raw, "ind");
				if (ty == 4)
				{
					walkShapeItems(ctx, member(raw, "shapes"), name,
						std::vector<JsonValue const *>(),
						std::vector<JsonValue const *>(), entry->blocks,
						nullptr, std::vector<JsonValue const *>());
					for (Block & block : entry->blocks)
					{
						block.masks = masks;
					}
				}
				else if (ty == 1)
				{
					entry->blocks.push_back(solidBlock(ctx, raw, name));
				}
				else if (ty == 2)
				{
					if (!masks.empty())
					{
						ctx.addError(formatText("mask on image layer '%s' - "
							"not supported (a textured cutout region is "
							"clipped by its contour only)", name.c_str()));
						continue;
					}
					Block block;
					if (imageBlock(ctx, raw, name, assets, block))
					{
						entry->blocks.push_back(block);
					}
				}
				else if (ty == 0)
				{
					JsonValue const * refValue = member(raw, "refId");
					String ref = refValue != nullptr && refValue->isString()
						? refValue->asString() : String();
					auto found = assets.find(ref);
					JsonValue const * asset = found == assets.end()
						? nullptr : found->second;
					JsonValue const * childLayers = member(asset, "layers");
					if (refValue == nullptr || asset == nullptr ||
						childLayers == nullptr || !childLayers->isArray())
					{
						ctx.addError(formatText("precomp layer '%s' references "
							"missing composition '%s'", name.c_str(),
							jsonStr(refValue).c_str()));
						continue;
					}
					if (std::find(refStack.begin(), refStack.end(), ref) !=
						refStack.end())
					{
						ctx.addError(formatText("precomp layer '%s' creates a "
							"composition cycle ('%s')", name.c_str(),
							ref.c_str()));
						continue;
					}
					// the precomp layer becomes a transform carrier whose
					// opacity multiplies down to every inlined child
					entry->inheritOpacity = true;
					ctx.flat.push_back(entry);
					if (indValue != nullptr)
					{
						byInd[indValue->asNumber()] = entry;
					}
					double childOffset = offset + numberOr(raw, "st", 0.0);
					std::vector<String> childStack = refStack;
					childStack.push_back(ref);
					walkLayers(ctx, childLayers, name, childOffset, entry,
						childStack, assets, compIp);
					continue;
				}
				ctx.flat.push_back(entry);
				if (indValue != nullptr)
				{
					byInd[indValue->asNumber()] = entry;
				}
			}

			for (std::pair<FlatEntry *, JsonValue const *> const & pair :
				localEntries)
			{
				JsonValue const * parentInd = member(pair.second, "parent");
				if (parentInd == nullptr)
				{
					continue;
				}
				auto found = byInd.find(parentInd->asNumber());
				if (found != byInd.end())
				{
					pair.first->parent = found->second;
				}
				else
				{
					ctx.addError(formatText("layer '%s' parents a missing or "
						"unsupported layer (ind %s)", pair.first->name.c_str(),
						jsonStr(parentInd).c_str()));
				}
			}
		}
		//-----------------------------------------------------
		void flattenLayers(Context & ctx)
		{
			std::map<String, JsonValue const *> assets;
			JsonValue const * assetList = member(&ctx.document, "assets");
			if (assetList != nullptr && assetList->isArray())
			{
				for (size_t index = 0; index < assetList->size(); ++index)
				{
					JsonValue const & asset = assetList->at(index);
					if (!asset.isObject())
					{
						continue;
					}
					JsonValue const * id = member(&asset, "id");
					assets[id != nullptr && id->isString() ? id->asString()
						: String()] = &asset;
				}
			}
			double compIp = numberOr(&ctx.document, "ip", 0.0);
			walkLayers(ctx, member(&ctx.document, "layers"), String(), -compIp,
				nullptr, std::vector<String>(), assets, compIp);
		}

		//=====================================================
		// channel conversion
		//=====================================================
		//! how a cooked value is derived from a raw source value
		enum ValueMap
		{
			VM_PLACE,		//!< y-flip + world scale (position/anchor)
			VM_SCALE,		//!< percent -> unit factor
			VM_ROTATION,	//!< degrees clockwise -> counter-clockwise
			VM_OPACITY		//!< percent -> clamped 0..1
		};
		//-----------------------------------------------------
		std::vector<double> mapValue(ValueMap map,
			std::vector<double> const & value, double scale)
		{
			std::vector<double> out;
			switch (map)
			{
			case VM_PLACE:
				out.push_back(value[0] * scale);
				out.push_back(-value[1] * scale);
				break;
			case VM_SCALE:
				out.push_back(value[0] / 100.0);
				out.push_back(value[1] / 100.0);
				break;
			case VM_ROTATION:
				out.push_back(-value[0]);
				break;
			case VM_OPACITY:
				out.push_back(std::min(std::max(value[0] / 100.0, 0.0), 1.0));
				break;
			}
			return out;
		}
		//-----------------------------------------------------
		//! the densify frame ladder: integer frames spanning [start, end],
		//! clamped to the timeline, endpoints included, strictly increasing
		std::vector<double> densifyFrames(double start, double end,
			double duration)
		{
			start = std::max(0.0, std::min(start, duration));
			end = std::max(0.0, std::min(end, duration));
			std::vector<double> frames;
			if (end <= start + EPS)
			{
				frames.push_back(start);
				return frames;
			}
			frames.push_back(start);
			double frame = std::floor(start) + 1.0;
			while (frame < end - EPS)
			{
				if (frame > start + EPS)
				{
					frames.push_back(frame);
				}
				frame += 1.0;
			}
			frames.push_back(end);
			return frames;
		}
		//-----------------------------------------------------
		//! @brief convert one transform channel.
		//! @remarks Direct mapping preserves the source's cubic value-bezier
		//! easing 1:1. When the runtime grammar cannot express a case (spatial
		//! position tangents, split x/y position, per-dimension easing, keys
		//! outside the timeline), the channel is DENSIFIED: sampled at every
		//! integer frame across its animated span and emitted as linear keys.
		ChannelOut convertChannel(JsonValue const * prop, int dim,
			double duration, double offset, ValueMap map, double scale,
			std::vector<double> const & absentValue,
			bool split, JsonValue const * splitX, JsonValue const * splitY)
		{
			ChannelOut out;
			out.present = true;
			if (split)
			{
				bool xAnimated = isAnimated(splitX, 1);
				bool yAnimated = isAnimated(splitY, 1);
				if (!xAnimated && !yAnimated)
				{
					std::vector<double> value;
					value.push_back(staticValue(splitX, 1,
						std::vector<double>(1, 0.0))[0]);
					value.push_back(staticValue(splitY, 1,
						std::vector<double>(1, 0.0))[0]);
					ChanKey key;
					key.frame = 0.0;
					key.values = mapValue(map, value, scale);
					out.keys.push_back(key);
					out.animated = false;
					return out;
				}
				// split dimensions cannot share one easing spec: densify
				std::vector<PropKey> xKeys;
				std::vector<PropKey> yKeys;
				if (xAnimated) { propKeys(splitX, 1, xKeys); }
				if (yAnimated) { propKeys(splitY, 1, yKeys); }
				double lowest = 0.0;
				double highest = 0.0;
				bool first = true;
				for (std::vector<PropKey> const * keys : { &xKeys, &yKeys })
				{
					for (PropKey const & key : *keys)
					{
						double time = key.t + offset;
						if (first || time < lowest) { lowest = time; }
						if (first || time > highest) { highest = time; }
						first = false;
					}
				}
				std::vector<double> frames = densifyFrames(lowest, highest,
					duration);
				for (double frame : frames)
				{
					std::vector<double> value;
					value.push_back(xKeys.empty()
						? staticValue(splitX, 1,
							std::vector<double>(1, 0.0))[0]
						: sampleKeys(xKeys, frame - offset, 1)[0]);
					value.push_back(yKeys.empty()
						? staticValue(splitY, 1,
							std::vector<double>(1, 0.0))[0]
						: sampleKeys(yKeys, frame - offset, 1)[0]);
					ChanKey key;
					key.frame = frame;
					key.values = mapValue(map, value, scale);
					out.keys.push_back(key);
				}
				out.animated = true;
				return out;
			}

			if (!isAnimated(prop, dim))
			{
				std::vector<double> fallback = prop == nullptr
					? absentValue : std::vector<double>(
						static_cast<size_t>(dim), 0.0);
				ChanKey key;
				key.frame = 0.0;
				key.values = mapValue(map, staticValue(prop, dim, fallback),
					scale);
				out.keys.push_back(key);
				out.animated = false;
				return out;
			}

			std::vector<PropKey> keys;
			propKeys(prop, dim, keys);
			bool expressible = !(dim == 2 && hasSpatialTangents(keys));
			if (expressible)
			{
				for (size_t index = 0; index + 1 < keys.size(); ++index)
				{
					Ease ease;
					if (!segmentEase(keys[index].h, keys[index].easeOut,
						keys[index].easeIn, dim, ease))
					{
						expressible = false;	// per-dimension easing mismatch
						break;
					}
				}
			}
			if (expressible)
			{
				for (PropKey const & key : keys)
				{
					double frame = key.t + offset;
					if (frame < -EPS || frame > duration + EPS)
					{
						expressible = false;	// keys outside the timeline
						break;
					}
				}
			}
			if (expressible)
			{
				for (size_t index = 0; index < keys.size(); ++index)
				{
					ChanKey key;
					key.frame = std::min(std::max(keys[index].t + offset, 0.0),
						duration);
					key.values = mapValue(map, keys[index].s, scale);
					if (index + 1 < keys.size())
					{
						segmentEase(keys[index].h, keys[index].easeOut,
							keys[index].easeIn, dim, key.ease);
					}
					out.keys.push_back(key);
				}
				out.animated = true;
				return out;
			}

			std::vector<double> frames = densifyFrames(keys.front().t + offset,
				keys.back().t + offset, duration);
			for (double frame : frames)
			{
				ChanKey key;
				key.frame = frame;
				key.values = mapValue(map, sampleKeys(keys, frame - offset,
					dim), scale);
				out.keys.push_back(key);
			}
			out.animated = true;
			return out;
		}
		//-----------------------------------------------------
		//! sample already-COOKED channel keys at a frame
		std::vector<double> sampleChannelKeys(
			std::vector<ChanKey> const & keys, double frame)
		{
			if (keys.empty())
			{
				return std::vector<double>(1, 1.0);
			}
			if (frame <= keys.front().frame)
			{
				return keys.front().values;
			}
			if (frame >= keys.back().frame)
			{
				return keys.back().values;
			}
			for (size_t index = 0; index + 1 < keys.size(); ++index)
			{
				ChanKey const & first = keys[index];
				ChanKey const & second = keys[index + 1];
				if (!(first.frame <= frame && frame <= second.frame))
				{
					continue;
				}
				if (first.ease.mode == Ease::HOLD)
				{
					return first.values;
				}
				double u = (frame - first.frame) /
					std::max(second.frame - first.frame, EPS);
				if (first.ease.mode == Ease::BEZIER)
				{
					u = bezierEase(first.ease.ox, first.ease.oy, first.ease.ix,
						first.ease.iy, u);
				}
				std::vector<double> out;
				size_t count = std::min(first.values.size(),
					second.values.size());
				for (size_t v = 0; v < count; ++v)
				{
					out.push_back(first.values[v] +
						u * (second.values[v] - first.values[v]));
				}
				return out;
			}
			return keys.back().values;
		}
		//-----------------------------------------------------
		//! @brief bake a layer's in/out window into its opacity channel: 0
		//! outside [start, end), hold-stepped at the boundaries. A static
		//! opacity stays three hold keys; an animated one is densified inside
		//! the window.
		ChannelOut applyWindow(ChannelOut const & opacity, double windowStart,
			double windowEnd, double duration)
		{
			double winStart = std::max(0.0, windowStart);
			double winEnd = std::min(duration, windowEnd);
			if (winStart <= EPS && winEnd >= duration - EPS)
			{
				return opacity;
			}
			ChannelOut out;
			out.present = true;
			if (winEnd <= winStart + EPS)
			{
				ChanKey key;
				key.frame = 0.0;
				key.values.push_back(0.0);
				out.keys.push_back(key);
				out.animated = false;
				return out;			// never visible
			}
			Ease hold;
			hold.mode = Ease::HOLD;
			if (!opacity.animated)
			{
				std::vector<double> value = opacity.keys.empty()
					? std::vector<double>(1, 1.0) : opacity.keys.front().values;
				if (winStart > EPS)
				{
					ChanKey key;
					key.frame = 0.0;
					key.values.push_back(0.0);
					key.ease = hold;
					out.keys.push_back(key);
				}
				ChanKey key;
				key.frame = winStart;
				key.values = value;
				key.ease = hold;
				out.keys.push_back(key);
			}
			else
			{
				if (winStart > EPS)
				{
					ChanKey key;
					key.frame = 0.0;
					key.values.push_back(0.0);
					key.ease = hold;
					out.keys.push_back(key);
				}
				// sample the authored curve per frame inside the window
				bool trailingZero = winEnd < duration - EPS;
				std::vector<double> frames = densifyFrames(winStart, winEnd,
					duration);
				for (double frame : frames)
				{
					if (trailingZero && frame >= winEnd - EPS)
					{
						break;
					}
					ChanKey key;
					key.frame = frame;
					key.values = sampleChannelKeys(opacity.keys, frame);
					out.keys.push_back(key);
				}
				if (!out.keys.empty() && out.keys.back().frame < winEnd - EPS)
				{
					out.keys.back().ease = hold;
				}
			}
			if (winEnd < duration - EPS)
			{
				ChanKey key;
				key.frame = winEnd;
				key.values.push_back(0.0);
				key.ease = hold;
				out.keys.push_back(key);
			}
			out.animated = true;
			return out;
		}

		//=====================================================
		// paints
		//=====================================================
		//-----------------------------------------------------
		//! the animatable opacity chain of a block: the fill's own opacity plus
		//! every enclosing group's opacity (all 0..100)
		std::vector<JsonValue const *> fillAlphaProps(Block const & block)
		{
			std::vector<JsonValue const *> props;
			JsonValue const * own = member(block.fill, "o");
			if (own != nullptr)
			{
				props.push_back(own);
			}
			for (JsonValue const * prop : block.opacities)
			{
				if (prop != nullptr)
				{
					props.push_back(prop);
				}
			}
			return props;
		}
		//-----------------------------------------------------
		//! the block's straight RGBA at a source-time frame
		Paint sampleFillRgba(Block const & block, double frame)
		{
			JsonValue const * colourProp = member(block.fill, "c");
			std::vector<double> value;
			size_t componentCount = 4;
			if (isAnimated(colourProp, 4))
			{
				std::vector<PropKey> keys;
				propKeys(colourProp, 4, keys);
				JsonValue const * raw = member(colourProp, "k");
				JsonValue const * firstValue = raw != nullptr && raw->size() > 0
					? member(&raw->at(0), "s") : nullptr;
				componentCount = firstValue != nullptr
					? asList(firstValue).size() : 3;
				value = sampleKeys(keys, frame, 4);
			}
			else
			{
				std::vector<double> fallback;
				fallback.push_back(0.0);
				fallback.push_back(0.0);
				fallback.push_back(0.0);
				fallback.push_back(1.0);
				value = staticValue(colourProp, 4, fallback);
				componentCount = 4;
				if (colourProp != nullptr && colourProp->isObject() &&
					intOr(colourProp, "a", 0) == 0)
				{
					JsonValue const * raw = member(colourProp, "k");
					componentCount = raw != nullptr ? asList(raw).size() : 4;
				}
				else if (colourProp != nullptr && !colourProp->isObject())
				{
					componentCount = asList(colourProp).size();
				}
			}
			double alpha = componentCount >= 4 ? value[3] : 1.0;
			for (JsonValue const * prop : fillAlphaProps(block))
			{
				alpha *= sampleProp(prop, 1, std::vector<double>(1, 100.0),
					frame)[0] / 100.0;
			}
			Paint paint;
			paint.gradient = false;
			paint.r = std::min(std::max(value[0], 0.0), 1.0);
			paint.g = std::min(std::max(value[1], 0.0), 1.0);
			paint.b = std::min(std::max(value[2], 0.0), 1.0);
			paint.a = std::min(std::max(alpha, 0.0), 1.0);
			return paint;
		}
		//-----------------------------------------------------
		Paint sampleGradientPaint(Block const & block, double frame,
			Affine const & affine, double scale)
		{
			JsonValue const * style = block.fill;
			JsonValue const * gradient = member(style, "g");
			int count = std::max(2, intOr(gradient, "p", 2));
			JsonValue const * prop = member(gradient, "k");
			JsonValue const * raw = prop != nullptr && prop->isObject()
				? member(prop, "k") : prop;
			std::vector<double> values;
			if (prop != nullptr && prop->isObject() &&
				intOr(prop, "a", 0) == 1 && truthy(raw))
			{
				JsonValue const * firstValue = member(&raw->at(0), "s");
				int dimension = static_cast<int>(asList(firstValue).size());
				values = sampleProp(prop, dimension,
					std::vector<double>(static_cast<size_t>(dimension), 0.0),
					frame);
			}
			else
			{
				values = asList(raw);
			}
			while (static_cast<int>(values.size()) < count * 4)
			{
				values.push_back(0.0);
			}
			std::vector<GradStop> colourStops;
			for (int index = 0; index < count; ++index)
			{
				size_t base = static_cast<size_t>(index) * 4;
				GradStop stop;
				stop.at = std::min(std::max(values[base], 0.0), 1.0);
				stop.r = std::min(std::max(values[base + 1], 0.0), 1.0);
				stop.g = std::min(std::max(values[base + 2], 0.0), 1.0);
				stop.b = std::min(std::max(values[base + 3], 0.0), 1.0);
				stop.a = 1.0;
				colourStops.push_back(stop);
			}
			std::vector<std::pair<double, double> > alphaStops;
			for (size_t index = static_cast<size_t>(count) * 4;
				index + 1 < values.size(); index += 2)
			{
				alphaStops.push_back(std::make_pair(
					std::min(std::max(values[index], 0.0), 1.0),
					std::min(std::max(values[index + 1], 0.0), 1.0)));
			}
			auto alphaAt = [&alphaStops](double position)
			{
				if (alphaStops.empty())
				{
					return 1.0;
				}
				if (position <= alphaStops.front().first)
				{
					return alphaStops.front().second;
				}
				for (size_t index = 0; index + 1 < alphaStops.size(); ++index)
				{
					std::pair<double, double> const & left = alphaStops[index];
					std::pair<double, double> const & right =
						alphaStops[index + 1];
					if (position <= right.first)
					{
						double span = std::max(right.first - left.first, EPS);
						double u = (position - left.first) / span;
						return left.second + u * (right.second - left.second);
					}
				}
				return alphaStops.back().second;
			};
			double opacity = sampleProp(member(style, "o"), 1,
				std::vector<double>(1, 100.0), frame)[0] / 100.0;
			for (JsonValue const * groupOpacity : block.opacities)
			{
				opacity *= sampleProp(groupOpacity, 1,
					std::vector<double>(1, 100.0), frame)[0] / 100.0;
			}
			Paint paint;
			paint.gradient = true;
			for (GradStop const & stop : colourStops)
			{
				GradStop out = stop;
				out.a = std::min(std::max(alphaAt(stop.at) * opacity, 0.0),
					1.0);
				paint.stops.push_back(out);
			}
			auto transformed = [&affine, scale](std::vector<double> const & p)
			{
				double x = affine.a * p[0] + affine.b * p[1] + affine.tx;
				double y = affine.c * p[0] + affine.d * p[1] + affine.ty;
				return P2(x * scale, -y * scale);
			};
			std::vector<double> startValue = sampleProp(member(style, "s"), 2,
				std::vector<double>(2, 0.0), frame);
			std::vector<double> endValue = sampleProp(member(style, "e"), 2,
				std::vector<double>(2, 0.0), frame);
			bool radial = intOr(style, "t", 1) != 1;
			std::vector<double> focalValue = startValue;
			if (radial)
			{
				double highlight = sampleProp(member(style, "h"), 1,
					std::vector<double>(1, 0.0), frame)[0] / 100.0;
				highlight = std::min(std::max(highlight, -0.99), 0.99);
				double angle = sampleProp(member(style, "a"), 1,
					std::vector<double>(1, 0.0), frame)[0] *
					(3.141592653589793 / 180.0);
				double radius = pyHypot(endValue[0] - startValue[0],
					endValue[1] - startValue[1]);
				focalValue.clear();
				focalValue.push_back(startValue[0] +
					std::cos(angle) * radius * highlight);
				focalValue.push_back(startValue[1] +
					std::sin(angle) * radius * highlight);
			}
			paint.radial = radial;
			paint.start = transformed(startValue);
			paint.end = transformed(endValue);
			paint.focal = transformed(focalValue);
			return paint;
		}
		//-----------------------------------------------------
		Paint transparentPaint(Paint const & paint)
		{
			Paint out = paint;
			if (out.gradient)
			{
				for (GradStop & stop : out.stops)
				{
					stop.a = 0.0;
				}
			}
			else
			{
				out.a = 0.0;
			}
			return out;
		}

		//=====================================================
		// block sample timelines
		//=====================================================
		//! the emitted key times of one block, with their easings
		struct BlockTimes
		{
			std::vector<double>	times;
			std::vector<Ease>	eases;
		};
		//-----------------------------------------------------
		std::vector<double> maskKeyTimes(Block const & block, double offset)
		{
			std::vector<double> times;
			for (JsonValue const * mask : block.masks)
			{
				JsonValue const * prop = member(mask, "pt");
				if (isAnimatedPath(prop))
				{
					for (PathKey const & key : pathPropKeys(prop))
					{
						times.push_back(key.t + offset);
					}
				}
			}
			return times;
		}
		//-----------------------------------------------------
		//! output-frame key times for every animated group transform in a block
		std::vector<double> groupTransformKeyTimes(Block const & block,
			double offset)
		{
			std::vector<double> times;
			for (JsonValue const * transform : block.affines)
			{
				std::vector<std::pair<JsonValue const *, int> > props;
				JsonValue const * position = member(transform, "p");
				if (position != nullptr && position->isObject() &&
					truthy(member(position, "s")))
				{
					props.push_back(std::make_pair(member(position, "x"), 1));
					props.push_back(std::make_pair(member(position, "y"), 1));
				}
				else
				{
					props.push_back(std::make_pair(position, 2));
				}
				props.push_back(std::make_pair(member(transform, "a"), 2));
				props.push_back(std::make_pair(member(transform, "s"), 2));
				props.push_back(std::make_pair(member(transform, "r"), 1));
				for (std::pair<JsonValue const *, int> const & entry : props)
				{
					if (!isAnimated(entry.first, entry.second))
					{
						continue;
					}
					std::vector<PropKey> keys;
					propKeys(entry.first, entry.second, keys);
					for (PropKey const & key : keys)
					{
						times.push_back(key.t + offset);
					}
				}
			}
			return times;
		}
		//-----------------------------------------------------
		std::vector<double> pathModifierKeyTimes(Block const & block,
			double offset)
		{
			std::vector<double> times;
			for (JsonValue const * modifier : block.modifiers)
			{
				JsonValue const * type = member(modifier, "ty");
				bool rounded = type != nullptr && type->asString() == "rd";
				JsonValue const * prop = member(modifier, rounded ? "r" : "a");
				if (!isAnimated(prop, 1))
				{
					continue;
				}
				std::vector<PropKey> keys;
				propKeys(prop, 1, keys);
				for (PropKey const & key : keys)
				{
					times.push_back(key.t + offset);
				}
			}
			return times;
		}
		//-----------------------------------------------------
		void addPropTimes(std::vector<double> & times, JsonValue const * prop,
			int dim, double offset)
		{
			if (!isAnimated(prop, dim))
			{
				return;
			}
			std::vector<PropKey> keys;
			propKeys(prop, dim, keys);
			for (PropKey const & key : keys)
			{
				times.push_back(key.t + offset);
			}
		}
		//-----------------------------------------------------
		void addGradientTimes(std::vector<double> & times,
			JsonValue const * style, double offset)
		{
			JsonValue const * gradient = member(style, "g");
			int count = std::max(2, intOr(gradient, "p", 2));
			JsonValue const * gradientProp = member(gradient, "k");
			if (gradientProp != nullptr && gradientProp->isObject())
			{
				JsonValue const * raw = member(gradientProp, "k");
				if (intOr(gradientProp, "a", 0) == 1 && truthy(raw))
				{
					int first = static_cast<int>(
						asList(member(&raw->at(0), "s")).size());
					addPropTimes(times, gradientProp,
						std::max(first, count * 4), offset);
				}
			}
			addPropTimes(times, member(style, "s"), 2, offset);
			addPropTimes(times, member(style, "e"), 2, offset);
			if (intOr(style, "t", 1) != 1)
			{
				addPropTimes(times, member(style, "h"), 1, offset);
				addPropTimes(times, member(style, "a"), 1, offset);
			}
		}
		//-----------------------------------------------------
		//! a densified timeline over the union of the collected key times
		BlockTimes densifiedTimes(std::vector<double> const & times,
			double duration)
		{
			BlockTimes out;
			if (times.empty())
			{
				out.times.push_back(0.0);
				out.eases.push_back(Ease());
				return out;
			}
			double lowest = times.front();
			double highest = times.front();
			for (double time : times)
			{
				lowest = std::min(lowest, time);
				highest = std::max(highest, time);
			}
			out.times = densifyFrames(lowest, highest, duration);
			out.eases.assign(out.times.size(), Ease());
			return out;
		}
		//-----------------------------------------------------
		//! @brief the shape-key timeline of a STROKE block.
		//! @remarks Strokes are expanded to filled vector outlines during
		//! cooking. Any animated centreline, width, colour, opacity or trim
		//! therefore densifies over its union span, keeping the runtime grammar
		//! and interpolation small.
		BlockTimes strokeSampleTimes(Block const & block, double duration,
			double offset)
		{
			std::vector<double> times;
			for (Block::PathRef const & path : block.paths)
			{
				if (path.kind == "sh" &&
					isAnimatedPath(member(path.item, "ks")))
				{
					for (PathKey const & key :
						pathPropKeys(member(path.item, "ks")))
					{
						times.push_back(key.t + offset);
					}
				}
				else if (path.kind == "el" || path.kind == "rc")
				{
					addPropTimes(times, member(path.item, "p"), 2, offset);
					addPropTimes(times, member(path.item, "s"), 2, offset);
					addPropTimes(times, member(path.item, "r"), 1, offset);
				}
				else if (path.kind == "sr")
				{
					addPropTimes(times, member(path.item, "pt"), 1, offset);
					addPropTimes(times, member(path.item, "p"), 2, offset);
					addPropTimes(times, member(path.item, "r"), 1, offset);
					addPropTimes(times, member(path.item, "or"), 1, offset);
					addPropTimes(times, member(path.item, "os"), 1, offset);
					addPropTimes(times, member(path.item, "ir"), 1, offset);
					addPropTimes(times, member(path.item, "is"), 1, offset);
				}
			}
			JsonValue const * style = block.fill;
			if (block.kind == "gradient_stroke")
			{
				addGradientTimes(times, style, offset);
			}
			else
			{
				addPropTimes(times, member(style, "c"), 4, offset);
			}
			addPropTimes(times, member(style, "o"), 1, offset);
			addPropTimes(times, member(style, "w"), 1, offset);
			JsonValue const * dashes = member(style, "d");
			if (dashes != nullptr && dashes->isArray())
			{
				for (size_t index = 0; index < dashes->size(); ++index)
				{
					addPropTimes(times, member(&dashes->at(index), "v"), 1,
						offset);
				}
			}
			for (JsonValue const * prop : block.opacities)
			{
				addPropTimes(times, prop, 1, offset);
			}
			if (block.trim != nullptr && block.trim->isObject())
			{
				addPropTimes(times, member(block.trim, "s"), 1, offset);
				addPropTimes(times, member(block.trim, "e"), 1, offset);
				addPropTimes(times, member(block.trim, "o"), 1, offset);
			}
			std::vector<double> masks = maskKeyTimes(block, offset);
			times.insert(times.end(), masks.begin(), masks.end());
			std::vector<double> groups = groupTransformKeyTimes(block, offset);
			times.insert(times.end(), groups.begin(), groups.end());
			std::vector<double> modifiers = pathModifierKeyTimes(block, offset);
			times.insert(times.end(), modifiers.begin(), modifiers.end());
			return densifiedTimes(times, duration);
		}
		//-----------------------------------------------------
		BlockTimes gradientSampleTimes(Block const & block, double duration,
			double offset)
		{
			std::vector<double> times;
			// Include geometry animation by reusing the fill analyser, then add
			// the gradient-specific channels. Sampling all integer frames keeps
			// combined path/paint animation aligned.
			for (Block::PathRef const & path : block.paths)
			{
				if (path.kind == "sh" &&
					isAnimatedPath(member(path.item, "ks")))
				{
					for (PathKey const & key :
						pathPropKeys(member(path.item, "ks")))
					{
						times.push_back(key.t + offset);
					}
				}
				else if (path.kind == "el" || path.kind == "rc")
				{
					addPropTimes(times, member(path.item, "p"), 2, offset);
					addPropTimes(times, member(path.item, "s"), 2, offset);
					addPropTimes(times, member(path.item, "r"), 1, offset);
				}
			}
			addGradientTimes(times, block.fill, offset);
			addPropTimes(times, member(block.fill, "o"), 1, offset);
			for (JsonValue const * prop : block.opacities)
			{
				addPropTimes(times, prop, 1, offset);
			}
			std::vector<double> masks = maskKeyTimes(block, offset);
			times.insert(times.end(), masks.begin(), masks.end());
			std::vector<double> groups = groupTransformKeyTimes(block, offset);
			times.insert(times.end(), groups.begin(), groups.end());
			std::vector<double> modifiers = pathModifierKeyTimes(block, offset);
			times.insert(times.end(), modifiers.begin(), modifiers.end());
			return densifiedTimes(times, duration);
		}
		//-----------------------------------------------------
		//! @brief the shape-key timeline of a block. Direct when exactly one
		//! animated source exists, it is a `sh` path or scalar chain with
		//! expressible easing and in-range keys; otherwise the union span
		//! densifies to integer frames.
		BlockTimes blockSampleTimes(Block const & block, double duration,
			double offset)
		{
			if (block.kind == "stroke" || block.kind == "gradient_stroke")
			{
				return strokeSampleTimes(block, duration, offset);
			}
			if (block.kind == "gradient_fill")
			{
				return gradientSampleTimes(block, duration, offset);
			}

			std::vector<Block::PathRef> animatedPaths;
			for (Block::PathRef const & path : block.paths)
			{
				if (path.kind == "sh")
				{
					if (isAnimatedPath(member(path.item, "ks")))
					{
						animatedPaths.push_back(path);
					}
					continue;
				}
				if (isAnimated(member(path.item, "p"), 2) ||
					isAnimated(member(path.item, "s"), 2) ||
					isAnimated(member(path.item, "r"), 1))
				{
					animatedPaths.push_back(path);
				}
			}
			bool colourAnimated = isAnimated(member(block.fill, "c"), 4);
			bool alphaAnimated = false;
			for (JsonValue const * prop : fillAlphaProps(block))
			{
				if (isAnimated(prop, 1))
				{
					alphaAnimated = true;
					break;
				}
			}
			std::vector<double> maskTimes = maskKeyTimes(block, offset);
			std::vector<double> groupTimes = groupTransformKeyTimes(block,
				offset);
			std::vector<double> modifierTimes = pathModifierKeyTimes(block,
				offset);
			size_t sources = animatedPaths.size() + (colourAnimated ? 1 : 0) +
				(alphaAnimated ? 1 : 0) + (maskTimes.empty() ? 0 : 1) +
				(groupTimes.empty() ? 0 : 1) + (modifierTimes.empty() ? 0 : 1);
			if (sources == 0)
			{
				BlockTimes out;
				out.times.push_back(0.0);
				out.eases.push_back(Ease());
				return out;
			}

			if (sources == 1 && animatedPaths.size() == 1 &&
				animatedPaths[0].kind == "sh")
			{
				std::vector<PathKey> keys = pathPropKeys(
					member(animatedPaths[0].item, "ks"));
				bool ok = true;
				for (PathKey const & key : keys)
				{
					double time = key.t + offset;
					if (!(0.0 - EPS <= time && time <= duration + EPS))
					{
						ok = false;
					}
				}
				std::vector<Ease> eases;
				for (size_t index = 0; index + 1 < keys.size(); ++index)
				{
					Ease ease;
					if (!segmentEase(keys[index].h, keys[index].easeOut,
						keys[index].easeIn, 1, ease))
					{
						ok = false;
						break;
					}
					eases.push_back(ease);
				}
				if (ok && keys.size() >= 2)
				{
					BlockTimes out;
					for (PathKey const & key : keys)
					{
						out.times.push_back(key.t + offset);
					}
					out.eases = eases;
					out.eases.push_back(Ease());
					return out;
				}
			}
			else if (sources == 1 && animatedPaths.empty() &&
				maskTimes.empty() && groupTimes.empty() &&
				modifierTimes.empty())
			{
				JsonValue const * prop = colourAnimated
					? member(block.fill, "c") : nullptr;
				if (prop == nullptr)
				{
					for (JsonValue const * candidate : fillAlphaProps(block))
					{
						if (isAnimated(candidate, 1))
						{
							prop = candidate;
							break;
						}
					}
				}
				int dim = colourAnimated ? 4 : 1;
				std::vector<PropKey> keys;
				propKeys(prop, dim, keys);
				bool ok = true;
				for (PropKey const & key : keys)
				{
					double time = key.t + offset;
					if (!(0.0 - EPS <= time && time <= duration + EPS))
					{
						ok = false;
					}
				}
				std::vector<Ease> eases;
				for (size_t index = 0; index + 1 < keys.size(); ++index)
				{
					Ease ease;
					if (!segmentEase(keys[index].h, keys[index].easeOut,
						keys[index].easeIn, dim, ease))
					{
						ok = false;
						break;
					}
					eases.push_back(ease);
				}
				if (ok && keys.size() >= 2)
				{
					BlockTimes out;
					for (PropKey const & key : keys)
					{
						out.times.push_back(key.t + offset);
					}
					out.eases = eases;
					out.eases.push_back(Ease());
					return out;
				}
			}

			// densify: integer frames across the union of animated spans
			std::vector<double> times;
			for (Block::PathRef const & path : animatedPaths)
			{
				if (path.kind == "sh")
				{
					for (PathKey const & key :
						pathPropKeys(member(path.item, "ks")))
					{
						times.push_back(key.t + offset);
					}
					continue;
				}
				addPropTimes(times, member(path.item, "p"), 2, offset);
				addPropTimes(times, member(path.item, "s"), 2, offset);
				addPropTimes(times, member(path.item, "r"), 1, offset);
			}
			if (colourAnimated)
			{
				addPropTimes(times, member(block.fill, "c"), 4, offset);
			}
			for (JsonValue const * prop : fillAlphaProps(block))
			{
				addPropTimes(times, prop, 1, offset);
			}
			times.insert(times.end(), maskTimes.begin(), maskTimes.end());
			times.insert(times.end(), groupTimes.begin(), groupTimes.end());
			times.insert(times.end(), modifierTimes.begin(),
				modifierTimes.end());
			return densifiedTimes(times, duration);
		}

		//=====================================================
		// trim, dashes and masks
		//=====================================================
		//-----------------------------------------------------
		//! @brief apply a trim modifier to a flattened centreline and resample
		//! it at a fixed count. Fixed sampling is important: animated trim
		//! endpoints may move, but every `.oanim` key must keep identical
		//! topology.
		void trimPolyline(std::vector<P2> & points, bool & closed,
			JsonValue const * trim, double frame)
		{
			if (trim == nullptr || !trim->isObject() || points.size() < 2)
			{
				return;
			}
			double start = sampleProp(member(trim, "s"), 1,
				std::vector<double>(1, 0.0), frame)[0] / 100.0;
			double end = sampleProp(member(trim, "e"), 1,
				std::vector<double>(1, 100.0), frame)[0] / 100.0;
			double offset = sampleProp(member(trim, "o"), 1,
				std::vector<double>(1, 0.0), frame)[0] / 360.0;
			double span = end - start;
			if (std::fabs(span) >= 1.0 - 1e-5)
			{
				return;
			}
			auto pyMod = [](double value, double modulus)
			{
				double result = std::fmod(value, modulus);
				if (result != 0.0 && ((result < 0.0) != (modulus < 0.0)))
				{
					result += modulus;
				}
				return result;
			};
			start = pyMod(start + offset, 1.0);
			span = pyMod(span, 1.0);
			if (span <= 1e-5)
			{
				span = 1e-5;
			}
			std::vector<P2> chain = points;
			if (closed)
			{
				chain.push_back(points.front());
			}
			std::vector<double> lengths(1, 0.0);
			for (size_t index = 0; index + 1 < chain.size(); ++index)
			{
				lengths.push_back(lengths.back() +
					pyHypot(chain[index + 1].x - chain[index].x,
						chain[index + 1].y - chain[index].y));
			}
			double total = lengths.back();
			if (total <= EPS)
			{
				closed = false;
				return;
			}
			auto pointAt = [&](double fraction)
			{
				double distance = pyMod(fraction, 1.0) * total;
				for (size_t index = 0; index + 1 < chain.size(); ++index)
				{
					if (distance <= lengths[index + 1] + EPS)
					{
						double edge = lengths[index + 1] - lengths[index];
						double u = edge <= EPS ? 0.0
							: (distance - lengths[index]) / edge;
						P2 const & a = chain[index];
						P2 const & b = chain[index + 1];
						return P2(a.x + (b.x - a.x) * u,
							a.y + (b.y - a.y) * u);
					}
				}
				return chain.back();
			};
			int count = std::max(static_cast<int>(points.size()), 16);
			std::vector<P2> sampled;
			for (int index = 0; index < count; ++index)
			{
				sampled.push_back(pointAt(start +
					span * index / (count - 1)));
			}
			points = sampled;
			closed = false;
		}
		//-----------------------------------------------------
		//! @brief split a flattened centreline by the authored dash/gap
		//! pattern. `valid` false = the standard says this pattern is ignored.
		//! Returns false when the pattern expands past the cook's region cap.
		bool dashSegments(std::vector<P2> const & points, bool closed,
			JsonValue const * style, double frame, double transformScale,
			std::vector<std::vector<P2> > & out, bool & valid)
		{
			valid = false;
			out.clear();
			JsonValue const * entries = member(style, "d");
			if (entries == nullptr || !entries->isArray() ||
				entries->size() == 0)
			{
				return true;
			}
			std::vector<double> pattern;
			double offset = 0.0;
			for (size_t index = 0; index < entries->size(); ++index)
			{
				JsonValue const & entry = entries->at(index);
				double value = sampleProp(member(&entry, "v"), 1,
					std::vector<double>(1, 0.0), frame)[0] * transformScale;
				JsonValue const * type = member(&entry, "n");
				String dashType = type != nullptr ? type->asString()
					: String("d");
				if (dashType == "o")
				{
					offset = value;
				}
				else
				{
					pattern.push_back(value);
				}
			}
			bool negative = false;
			for (double value : pattern)
			{
				if (value < 0.0)
				{
					negative = true;
				}
			}
			if (pattern.empty() || negative || pySum(pattern) <= EPS)
			{
				return true;
			}
			valid = true;
			// an odd dash/gap sequence repeats with inverted roles
			if (pattern.size() % 2 == 1)
			{
				std::vector<double> doubled = pattern;
				pattern.insert(pattern.end(), doubled.begin(), doubled.end());
			}
			std::vector<P2> chain;
			std::vector<double> lengths;
			polylineChain(points, closed, chain, lengths);
			if (chain.size() < 2 || lengths.back() <= EPS)
			{
				return true;
			}
			double period = pySum(pattern);
			double phase = std::fmod(offset, period);
			if (phase != 0.0 && ((phase < 0.0) != (period < 0.0)))
			{
				phase += period;
			}
			size_t patternIndex = 0;
			size_t zeroGuard = 0;
			while (phase >= pattern[patternIndex] - EPS &&
				zeroGuard < pattern.size())
			{
				phase -= pattern[patternIndex];
				patternIndex = (patternIndex + 1) % pattern.size();
				++zeroGuard;
			}
			double remaining = std::max(pattern[patternIndex] - phase, 0.0);
			double cursor = 0.0;
			while (cursor < lengths.back() - EPS)
			{
				if (remaining <= EPS)
				{
					patternIndex = (patternIndex + 1) % pattern.size();
					remaining = pattern[patternIndex];
					continue;
				}
				double step = std::min(remaining, lengths.back() - cursor);
				if (patternIndex % 2 == 0 && step > EPS)
				{
					out.push_back(resampleOpen(sliceChain(chain, lengths,
						cursor, cursor + step), 8));
					if (out.size() > 512)
					{
						return false;
					}
				}
				cursor += step;
				remaining -= step;
			}
			return true;
		}
		//-----------------------------------------------------
		//! the per-key mask contours of a block, or false when there is none
		bool maskSeries(Context & ctx, Block const & block,
			std::vector<double> const & times, double offset, double tol,
			std::vector<std::vector<P2> > & out)
		{
			out.clear();
			if (block.masks.empty())
			{
				return false;
			}
			if (block.masks.size() != 1)
			{
				ctx.addError(formatText("layer '%s' uses %d additive masks - "
					"multiple-mask union is not yet supported",
					block.layer.c_str(),
					static_cast<int>(block.masks.size())));
				return false;
			}
			JsonValue const * prop = member(block.masks[0], "pt");
			bool animated = isAnimatedPath(prop);
			std::vector<PathKey> keys = animated ? pathPropKeys(prop)
				: std::vector<PathKey>();
			std::vector<BezPath> paths;
			for (double time : times)
			{
				paths.push_back(animated
					? samplePathKeys(keys, time - offset)
					: staticPathBez(prop));
			}
			bool degenerate = paths.empty();
			for (BezPath const & path : paths)
			{
				if (path.v.size() < 3)
				{
					degenerate = true;
				}
			}
			if (degenerate)
			{
				ctx.addError(formatText("empty mask path on layer '%s'",
					block.layer.c_str()));
				return false;
			}
			std::vector<int> counts = edgeSegmentCounts(paths, tol);
			std::vector<std::vector<P2> > contours;
			for (BezPath const & path : paths)
			{
				contours.push_back(flattenPath(path, counts));
			}
			for (std::vector<P2> const & contour : contours)
			{
				int direction = 0;
				size_t count = contour.size();
				for (size_t index = 0; index < count; ++index)
				{
					P2 const & a = contour[(index + count - 1) % count];
					P2 const & b = contour[index];
					P2 const & c = contour[(index + 1) % count];
					double cross = (b.x - a.x) * (c.y - b.y) -
						(b.y - a.y) * (c.x - b.x);
					if (std::fabs(cross) <= EPS)
					{
						continue;
					}
					int current = cross > 0 ? 1 : -1;
					if (direction != 0 && current != direction)
					{
						ctx.addError(formatText("non-convex mask on layer '%s' "
							"- general mask tessellation is not yet supported",
							block.layer.c_str()));
						return false;
					}
					direction = current;
				}
			}
			out = contours;
			return true;
		}
		//-----------------------------------------------------
		//! one region pose: an outer boundary plus its holes
		struct RegionPose
		{
			std::vector<P2>					outer;
			std::vector<std::vector<P2> >	holes;
		};
		//-----------------------------------------------------
		//! @brief clip region poses per key and normalize the resulting
		//! boundary counts so the animated `.oanim` topology remains fixed.
		std::vector<RegionPose> clipKeyRegions(Context & ctx,
			std::vector<RegionPose> const & keyRegions, bool hasMasks,
			std::vector<std::vector<P2> > const & masks, String const & layer)
		{
			if (!hasMasks)
			{
				return keyRegions;
			}
			std::vector<RegionPose> clipped;
			size_t pairs = std::min(keyRegions.size(), masks.size());
			for (size_t index = 0; index < pairs; ++index)
			{
				RegionPose pose;
				pose.outer = clipConvex(keyRegions[index].outer, masks[index]);
				for (std::vector<P2> const & hole : keyRegions[index].holes)
				{
					pose.holes.push_back(clipConvex(hole, masks[index]));
				}
				clipped.push_back(pose);
			}
			// A fully clipped key still needs valid, fixed topology. Represent
			// it by a microscopic triangle and make its paint transparent at
			// emission; as the mask reveals the shape, interpolation grows from
			// that boundary.
			std::vector<RegionPose> normalized;
			for (size_t index = 0; index < clipped.size(); ++index)
			{
				RegionPose pose = clipped[index];
				bool hiddenOuter = pose.outer.size() < 3;
				if (hiddenOuter)
				{
					P2 anchor = masks[index][0];
					double tiny = 1e-4;
					pose.outer.clear();
					pose.outer.push_back(anchor);
					pose.outer.push_back(P2(anchor.x + tiny, anchor.y));
					pose.outer.push_back(P2(anchor.x, anchor.y + tiny));
				}
				size_t expectedHoles = keyRegions[index].holes.size();
				std::vector<double> xs;
				std::vector<double> ys;
				for (P2 const & point : pose.outer)
				{
					xs.push_back(point.x);
					ys.push_back(point.y);
				}
				double anchorX = pySum(xs) / pose.outer.size();
				double anchorY = pySum(ys) / pose.outer.size();
				std::vector<std::vector<P2> > fixedHoles;
				for (size_t hole = 0; hole < expectedHoles; ++hole)
				{
					std::vector<P2> shape = hole < pose.holes.size()
						? pose.holes[hole] : std::vector<P2>();
					if (shape.size() < 3)
					{
						double tiny = hiddenOuter ? 1e-6 : 1e-5;
						shape.clear();
						shape.push_back(P2(anchorX, anchorY));
						shape.push_back(P2(anchorX, anchorY + tiny));
						shape.push_back(P2(anchorX + tiny, anchorY));
					}
					fixedHoles.push_back(shape);
				}
				pose.holes = fixedHoles;
				normalized.push_back(pose);
			}
			clipped = normalized;
			if (clipped.empty())
			{
				return keyRegions;
			}
			size_t outerCount = 0;
			for (RegionPose const & pose : clipped)
			{
				outerCount = std::max(outerCount, pose.outer.size());
			}
			size_t holeCount = clipped.front().holes.size();
			for (RegionPose const & pose : clipped)
			{
				if (pose.holes.size() != holeCount)
				{
					ctx.addError(formatText("mask changes hole topology on "
						"layer '%s'", layer.c_str()));
					return keyRegions;
				}
			}
			std::vector<size_t> holeSizes;
			for (size_t hole = 0; hole < holeCount; ++hole)
			{
				size_t largest = 0;
				for (RegionPose const & pose : clipped)
				{
					largest = std::max(largest, pose.holes[hole].size());
				}
				holeSizes.push_back(largest);
			}
			std::vector<RegionPose> out;
			for (RegionPose const & pose : clipped)
			{
				RegionPose resampled;
				resampled.outer = resampleClosed(pose.outer,
					static_cast<int>(outerCount));
				for (size_t hole = 0; hole < holeCount; ++hole)
				{
					resampled.holes.push_back(resampleClosed(pose.holes[hole],
						static_cast<int>(holeSizes[hole])));
				}
				out.push_back(resampled);
			}
			return out;
		}
		//-----------------------------------------------------
		//! one filled region: its outer contour index plus its hole indices
		struct HoleRegion
		{
			size_t				outer;
			std::vector<size_t>	holes;
		};
		//-----------------------------------------------------
		//! @brief containment-based hole assignment at the reference key.
		//! @remarks evenodd: odd containment depth = a hole of its immediate
		//! container; nonzero: additionally the winding must OPPOSE the
		//! container's, else the nested contour is its own filled region.
		std::vector<HoleRegion> assignHoles(
			std::vector<std::vector<P2> > const & contours, int fillRule)
		{
			size_t n = contours.size();
			std::vector<std::vector<size_t> > containers(n);
			for (size_t i = 0; i < n; ++i)
			{
				if (contours[i].empty())
				{
					continue;
				}
				P2 const & rep = contours[i][0];
				for (size_t j = 0; j < n; ++j)
				{
					if (i != j && pointInPolygon(rep, contours[j]))
					{
						containers[i].push_back(j);
					}
				}
			}
			std::vector<size_t> depth(n, 0);
			for (size_t i = 0; i < n; ++i)
			{
				depth[i] = containers[i].size();
			}
			std::vector<HoleRegion> regions;
			std::map<size_t, size_t> regionOf;
			std::vector<size_t> order(n);
			for (size_t i = 0; i < n; ++i)
			{
				order[i] = i;
			}
			std::stable_sort(order.begin(), order.end(),
				[&depth](size_t a, size_t b)
				{
					if (depth[a] != depth[b]) { return depth[a] < depth[b]; }
					return a < b;
				});
			for (size_t i : order)
			{
				if (depth[i] % 2 == 0)
				{
					regionOf[i] = regions.size();
					HoleRegion region;
					region.outer = i;
					regions.push_back(region);
					continue;
				}
				bool haveParent = false;
				size_t parent = 0;
				for (size_t j : containers[i])
				{
					if (depth[j] == depth[i] - 1 && !haveParent)
					{
						parent = j;
						haveParent = true;
					}
				}
				bool isHole = haveParent &&
					regionOf.find(parent) != regionOf.end();
				if (isHole && fillRule != 2)
				{
					// nonzero: the winding must oppose the container's
					if ((polygonArea(contours[i]) > 0) ==
						(polygonArea(contours[parent]) > 0))
					{
						isHole = false;
					}
				}
				if (isHole)
				{
					regions[regionOf[parent]].holes.push_back(i);
				}
				else
				{
					regionOf[i] = regions.size();
					HoleRegion region;
					region.outer = i;
					regions.push_back(region);
				}
			}
			return regions;
		}

		//=====================================================
		// block conversion
		//=====================================================
		//-----------------------------------------------------
		String strokeCapName(int code)
		{
			if (code == 2) { return "round"; }
			if (code == 3) { return "square"; }
			return "butt";
		}
		//-----------------------------------------------------
		String strokeJoinName(int code)
		{
			if (code == 2) { return "round"; }
			if (code == 3) { return "bevel"; }
			return "miter";
		}
		//-----------------------------------------------------
		//! @brief cook one stroke into `.oanim` STROKE regions: the flattened
		//! CENTRELINE plus width/cap/join/limit.
		//! @remarks The renderer sweeps that centreline into convex pieces at
		//! draw time, so nothing here expands an offset outline - an outline
		//! self-intersects wherever the path curves tighter than the half
		//! width, and a triangulator turns such a polygon into stray spikes and
		//! filaments.
		std::vector<ShapeOut> convertStrokeBlock(Context & ctx,
			Block const & block, double duration, double offset, double tol,
			double scale)
		{
			BlockTimes timeline = strokeSampleTimes(block, duration, offset);
			std::vector<double> const & times = timeline.times;
			std::vector<Affine> affines;
			for (double time : times)
			{
				affines.push_back(blockAffineAt(block, time - offset));
			}
			JsonValue const * style = block.fill;
			String cap = strokeCapName(intOr(style, "lc", 1));
			String join = strokeJoinName(intOr(style, "lj", 1));
			double miterLimit = std::max(numberOr(style, "ml", 4.0), 1.0);
			// the cook's world scale: a stroke width is a length, so it rides
			// the same factor its geometry does
			double unitScale = std::fabs(1.0 * scale);
			std::vector<ShapeOut> entries;

			for (Block::PathRef const & path : block.paths)
			{
				// Evaluate every emitted pose when choosing curve subdivision.
				// This is conservative and guarantees an identical centreline
				// vertex count.
				std::vector<BezPath> beziers;
				for (size_t index = 0; index < times.size(); ++index)
				{
					beziers.push_back(transformPath(blockPathWithModifiers(
						block, path.kind, path.item, times[index] - offset),
						affines[index]));
				}
				std::set<size_t> vertexCounts;
				for (BezPath const & bezier : beziers)
				{
					vertexCounts.insert(bezier.v.size());
				}
				if (beziers.empty() || vertexCounts.size() > 1)
				{
					ctx.addError(formatText("stroke path keyframes with "
						"differing vertex counts on layer '%s'",
						block.layer.c_str()));
					continue;
				}
				if (beziers[0].v.size() < 2)
				{
					continue;
				}
				std::vector<int> counts = edgeSegmentCounts(beziers, tol);
				struct StrokeState
				{
					std::vector<P2>					centerline;
					bool							closed;
					double							width;
					std::vector<std::vector<P2> >	dashes;
					bool							dashesValid;
				};
				std::vector<StrokeState> states;
				for (size_t index = 0; index < beziers.size(); ++index)
				{
					StrokeState state;
					state.centerline = flattenPath(beziers[index], counts);
					state.closed = beziers[index].closed;
					trimPolyline(state.centerline, state.closed, block.trim,
						times[index] - offset);
					Affine const & affine = affines[index];
					double determinant = std::fabs(affine.a * affine.d -
						affine.b * affine.c);
					double widthScale = determinant > EPS
						? std::sqrt(determinant) : 1.0;
					state.width = sampleProp(member(style, "w"), 1,
						std::vector<double>(1, 1.0),
						times[index] - offset)[0] * widthScale;
					if (!dashSegments(state.centerline, state.closed, style,
						times[index] - offset, widthScale, state.dashes,
						state.dashesValid))
					{
						ctx.addError(formatText("stroke dash pattern expands "
							"beyond 512 dash regions on layer '%s'",
							block.layer.c_str()));
						state.dashes.clear();
						state.dashesValid = true;
					}
					states.push_back(state);
				}
				if (!ctx.errors.empty())
				{
					continue;
				}
				// A negative/zero pattern is ignored as a whole by the format.
				// If an animation crosses that invalid state, keep this stroke
				// solid for the complete clip instead of changing its region
				// semantics midway.
				bool useDashes = truthy(member(style, "d"));
				for (StrokeState const & state : states)
				{
					if (!state.dashesValid)
					{
						useDashes = false;
					}
				}
				// one region per dash (an undashed stroke is one region); a
				// dash the pattern drops at some frames keeps its slot as a
				// degenerate, transparent centreline, so the topology stays
				// fixed
				struct Line
				{
					std::vector<P2>	points;
					bool			closed;
				};
				std::vector<std::vector<Line> > keyLines;
				std::vector<P2> anchors;
				for (StrokeState const & state : states)
				{
					anchors.push_back(state.centerline.empty()
						? P2(0.0, 0.0) : state.centerline.front());
					std::vector<Line> lines;
					if (useDashes)
					{
						for (std::vector<P2> const & segment : state.dashes)
						{
							Line line;
							line.points = segment;
							line.closed = false;
							lines.push_back(line);
						}
					}
					else
					{
						Line line;
						line.points = state.centerline;
						line.closed = state.closed;
						lines.push_back(line);
					}
					keyLines.push_back(lines);
				}
				size_t regionCount = 0;
				for (std::vector<Line> const & lines : keyLines)
				{
					regionCount = std::max(regionCount, lines.size());
				}
				std::vector<std::vector<P2> > masks;
				bool hasMasks = maskSeries(ctx, block, times, offset, tol,
					masks);
				for (size_t region = 0; region < regionCount; ++region)
				{
					std::vector<KeyOut> keys;
					bool haveTopology = false;
					size_t topologyCount = 0;
					bool topologyClosed = false;
					for (size_t index = 0; index < times.size(); ++index)
					{
						std::vector<Line> const & lines = keyLines[index];
						bool hidden = region >= lines.size();
						std::vector<P2> line;
						bool closed = false;
						if (hidden)
						{
							line.assign(8, anchors[index]);
						}
						else
						{
							line = lines[region].points;
							closed = lines[region].closed;
						}
						if (!haveTopology)
						{
							topologyCount = line.size();
							topologyClosed = closed;
							haveTopology = true;
						}
						else if (topologyCount != line.size() ||
							topologyClosed != closed)
						{
							ctx.addError(formatText("stroke centreline "
								"topology changes on layer '%s' - bake the "
								"trim/dash animation or keep its path count "
								"fixed", block.layer.c_str()));
							keys.clear();
							break;
						}
						Paint paint = block.kind == "gradient_stroke"
							? sampleGradientPaint(block, times[index] - offset,
								affines[index], scale)
							: sampleFillRgba(block, times[index] - offset);
						if (hidden)
						{
							paint = transparentPaint(paint);
						}
						KeyOut key;
						key.frame = times[index];
						key.ease = timeline.eases[index];
						key.paint = paint;
						key.stroke.present = true;
						key.stroke.width = states[index].width * unitScale;
						key.stroke.cap = cap;
						key.stroke.join = join;
						key.stroke.miter = miterLimit;
						key.stroke.closed = closed;
						for (P2 const & point : line)
						{
							key.outer.push_back(P2(point.x * scale,
								-point.y * scale));
						}
						if (hasMasks)
						{
							key.hasMask = true;
							for (P2 const & point : masks[index])
							{
								key.mask.push_back(P2(point.x * scale,
									-point.y * scale));
							}
						}
						keys.push_back(key);
					}
					if (!keys.empty() && keys.front().outer.size() >= 2)
					{
						ShapeOut shape;
						shape.keys = keys;
						entries.push_back(shape);
					}
				}
			}
			return entries;
		}
		//-----------------------------------------------------
		//! @brief one paint block -> a list of `.oanim` shape entries, each
		//! with fixed topology across keys. Vertices are placed to world
		//! (cooked) space.
		std::vector<ShapeOut> convertBlock(Context & ctx, Block const & block,
			double duration, double offset, double tol, double scale)
		{
			if (block.kind == "stroke" || block.kind == "gradient_stroke")
			{
				return convertStrokeBlock(ctx, block, duration, offset, tol,
					scale);
			}
			BlockTimes timeline = blockSampleTimes(block, duration, offset);
			std::vector<double> const & times = timeline.times;
			std::vector<Affine> affines;
			for (double time : times)
			{
				affines.push_back(blockAffineAt(block, time - offset));
			}

			// worst-case flattening resolution: sample every emitted pose,
			// which includes animated group transforms and bounds the required
			// curve subdivision after scaling/rotation
			std::vector<std::vector<int> > perPathCounts;
			for (Block::PathRef const & path : block.paths)
			{
				std::vector<BezPath> beziers;
				for (size_t index = 0; index < times.size(); ++index)
				{
					beziers.push_back(transformPath(blockPathWithModifiers(
						block, path.kind, path.item, times[index] - offset),
						affines[index]));
				}
				std::set<size_t> vertexCounts;
				for (BezPath const & bezier : beziers)
				{
					vertexCounts.insert(bezier.v.size());
				}
				if (vertexCounts.size() > 1)
				{
					ctx.addError(formatText("path keyframes with differing "
						"vertex counts on layer '%s' - every key must share one "
						"path structure", block.layer.c_str()));
					return std::vector<ShapeOut>();
				}
				// One- and two-point paths have no fill area but may share a
				// group with real fill contours (and often carry a visible
				// stroke). Keep walking; the region emission below naturally
				// drops degenerates.
				perPathCounts.push_back(edgeSegmentCounts(beziers, tol));
			}

			// flatten every path at every emitted time (fixed counts = fixed
			// vertices)
			std::vector<std::vector<std::vector<P2> > > contoursPerTime;
			for (size_t index = 0; index < times.size(); ++index)
			{
				std::vector<std::vector<P2> > contours;
				for (size_t which = 0; which < block.paths.size(); ++which)
				{
					BezPath path = transformPath(blockPathWithModifiers(block,
						block.paths[which].kind, block.paths[which].item,
						times[index] - offset), affines[index]);
					contours.push_back(flattenPath(path, perPathCounts[which]));
				}
				contoursPerTime.push_back(contours);
			}

			// hole assignment from the FIRST key's geometry, reused at every
			// key (the fixed-topology law: structure never changes mid-clip)
			int fillRule = static_cast<int>(staticValue(member(block.fill, "r"),
				1, std::vector<double>(1, 1.0))[0]);
			std::vector<HoleRegion> regions = assignHoles(contoursPerTime[0],
				fillRule);

			std::vector<ShapeOut> entries;
			std::vector<std::vector<P2> > masks;
			bool hasMasks = maskSeries(ctx, block, times, offset, tol, masks);
			for (HoleRegion const & region : regions)
			{
				if (contoursPerTime[0][region.outer].size() < 3)
				{
					continue;
				}
				std::vector<RegionPose> poses;
				for (size_t index = 0; index < times.size(); ++index)
				{
					RegionPose pose;
					pose.outer = contoursPerTime[index][region.outer];
					for (size_t hole : region.holes)
					{
						if (contoursPerTime[0][hole].size() >= 3)
						{
							pose.holes.push_back(contoursPerTime[index][hole]);
						}
					}
					poses.push_back(pose);
				}
				poses = clipKeyRegions(ctx, poses, hasMasks, masks,
					block.layer);
				std::vector<KeyOut> keys;
				for (size_t index = 0; index < times.size(); ++index)
				{
					Paint paint = block.kind == "gradient_fill"
						? sampleGradientPaint(block, times[index] - offset,
							affines[index], scale)
						: sampleFillRgba(block, times[index] - offset);
					if (hasMasks &&
						std::fabs(polygonArea(poses[index].outer)) < 1e-6)
					{
						paint = transparentPaint(paint);
					}
					KeyOut key;
					key.frame = times[index];
					key.ease = timeline.eases[index];
					key.paint = paint;
					for (P2 const & point : poses[index].outer)
					{
						key.outer.push_back(P2(point.x * scale,
							-point.y * scale));
					}
					for (std::vector<P2> const & hole : poses[index].holes)
					{
						std::vector<P2> placed;
						for (P2 const & point : hole)
						{
							placed.push_back(P2(point.x * scale,
								-point.y * scale));
						}
						key.holes.push_back(placed);
					}
					keys.push_back(key);
				}
				ShapeOut shape;
				shape.keys = keys;
				if (!block.texture.empty())
				{
					// an image block: the region is a textured cutout part
					// whose rect IS its contour's bounds
					shape.texture = block.texture;
				}
				entries.push_back(shape);
			}
			return entries;
		}

		//=====================================================
		// rig assembly
		//=====================================================
		//-----------------------------------------------------
		//! @brief the cooked transform channels of a flat layer. Values are
		//! converted to cooked space (y flip, scale /100, rotation negated to
		//! CCW, opacity /100).
		void layerChannels(Context const & ctx, FlatEntry const * entry,
			double duration, double scale, ChannelOut out[CH_COUNT])
		{
			JsonValue const * ks = entry->ks != nullptr && entry->ks->isObject()
				? entry->ks : nullptr;
			double offset = entry->offset;
			JsonValue const * position = ksProp(ctx, ks, "p");
			bool split = position != nullptr && position->isObject() &&
				truthy(member(position, "s"));
			out[CH_POS] = convertChannel(split ? nullptr : position, 2,
				duration, offset, VM_PLACE, scale,
				std::vector<double>(2, 0.0), split,
				split ? member(position, "x") : nullptr,
				split ? member(position, "y") : nullptr);
			out[CH_ANCHOR] = convertChannel(ksProp(ctx, ks, "a"), 2, duration,
				offset, VM_PLACE, scale, std::vector<double>(2, 0.0), false,
				nullptr, nullptr);
			out[CH_SCALE] = convertChannel(ksProp(ctx, ks, "s"), 2, duration,
				offset, VM_SCALE, scale, std::vector<double>(2, 100.0), false,
				nullptr, nullptr);
			out[CH_ROT] = convertChannel(ksProp(ctx, ks, "r"), 1, duration,
				offset, VM_ROTATION, scale, std::vector<double>(1, 0.0), false,
				nullptr, nullptr);
			out[CH_OPACITY] = convertChannel(ksProp(ctx, ks, "o"), 1, duration,
				offset, VM_OPACITY, scale, std::vector<double>(1, 100.0), false,
				nullptr, nullptr);
		}
		//-----------------------------------------------------
		std::vector<double> channelDefault(int channel)
		{
			switch (channel)
			{
			case CH_SCALE:		return std::vector<double>(2, 1.0);
			case CH_ROT:		return std::vector<double>(1, 0.0);
			case CH_OPACITY:	return std::vector<double>(1, 1.0);
			default:			return std::vector<double>(2, 0.0);
			}
		}
		//-----------------------------------------------------
		bool channelIsDefault(int channel, ChannelOut const & converted)
		{
			if (converted.animated)
			{
				return false;
			}
			if (converted.keys.empty())
			{
				return true;
			}
			std::vector<double> const & value = converted.keys.front().values;
			std::vector<double> fallback = channelDefault(channel);
			size_t count = std::min(value.size(), fallback.size());
			for (size_t index = 0; index < count; ++index)
			{
				if (std::fabs(value[index] - fallback[index]) > 1e-5)
				{
					return false;
				}
			}
			return true;
		}
		//-----------------------------------------------------
		//! @brief the emit-ready rig, honouring both grammar order constraints.
		//! @remarks The `.oanim` grammar requires every parent to PRECEDE its
		//! children while the file order IS the paint order. Source parenting
		//! only inherits transforms (opacity stays per-layer) and a parent may
		//! paint above its child, so the two orders can conflict. The cook
		//! resolves both structurally: every layer that is referenced as a
		//! parent contributes a TRANSFORM CARRIER (a null; no opacity except
		//! for inlined precomps, whose opacity legitimately multiplies down to
		//! their children), emitted first in hierarchy order; paint layers
		//! follow in paint order (the source lists top-first, so reversed),
		//! each parented to its carrier or its parent's carrier.
		std::vector<EmitLayer> buildRig(Context & ctx, double duration,
			double compW, double compH, double scale, double tol)
		{
			std::set<FlatEntry const *> parentRefs;
			for (FlatEntry const * entry : ctx.flat)
			{
				if (entry->parent != nullptr)
				{
					parentRefs.insert(entry->parent);
				}
			}
			std::vector<FlatEntry *> carriers;
			std::set<FlatEntry const *> carrierIds;
			// carriers: every referenced layer, plus its ancestors (closure)
			std::function<void(FlatEntry *)> needCarrier =
				[&](FlatEntry * entry)
			{
				if (carrierIds.find(entry) != carrierIds.end())
				{
					return;
				}
				if (entry->parent != nullptr)
				{
					needCarrier(entry->parent);
				}
				carrierIds.insert(entry);
				carriers.push_back(entry);
			};
			for (FlatEntry * entry : ctx.flat)
			{
				if (parentRefs.find(entry) != parentRefs.end())
				{
					needCarrier(entry);
				}
			}

			std::vector<EmitLayer> emit;
			std::map<FlatEntry const *, int> emitIndex;

			// the synthetic world root: centers the composition on the origin
			EmitLayer root;
			root.name = "comp";
			root.parent = -1;
			root.channels[CH_POS].present = true;
			root.channels[CH_POS].animated = false;
			{
				ChanKey key;
				key.frame = 0.0;
				key.values.push_back(-(compW * 0.5) * scale);
				key.values.push_back((compH * 0.5) * scale);
				root.channels[CH_POS].keys.push_back(key);
			}
			emit.push_back(root);

			for (FlatEntry * entry : carriers)
			{
				ChannelOut channels[CH_COUNT];
				layerChannels(ctx, entry, duration, scale, channels);
				EmitLayer layer;
				layer.name = entry->name;
				layer.channels[CH_POS] = channels[CH_POS];
				layer.channels[CH_ANCHOR] = channels[CH_ANCHOR];
				layer.channels[CH_SCALE] = channels[CH_SCALE];
				layer.channels[CH_ROT] = channels[CH_ROT];
				if (entry->inheritOpacity)
				{
					layer.channels[CH_OPACITY] = applyWindow(
						channels[CH_OPACITY], entry->windowStart,
						entry->windowEnd, duration);
				}
				int parent = 0;
				if (entry->parent != nullptr)
				{
					auto found = emitIndex.find(entry->parent);
					parent = found == emitIndex.end() ? 0 : found->second;
				}
				layer.parent = parent;
				emitIndex[entry] = static_cast<int>(emit.size());
				emit.push_back(layer);
			}

			for (size_t reverse = ctx.flat.size(); reverse > 0; --reverse)
			{
				FlatEntry * entry = ctx.flat[reverse - 1];	// bottom paint first
				if (entry->blocks.empty())
				{
					continue;
				}
				std::vector<ShapeOut> shapes;
				for (Block const & block : entry->blocks)
				{
					std::vector<ShapeOut> converted = convertBlock(ctx, block,
						duration, entry->offset, tol, scale);
					shapes.insert(shapes.end(), converted.begin(),
						converted.end());
				}
				// shape items list top-first, like layers
				std::reverse(shapes.begin(), shapes.end());
				ChannelOut channels[CH_COUNT];
				layerChannels(ctx, entry, duration, scale, channels);
				EmitLayer layer;
				bool split = carrierIds.find(entry) != carrierIds.end();
				if (split)
				{
					layer.channels[CH_OPACITY] = applyWindow(
						channels[CH_OPACITY], entry->windowStart,
						entry->windowEnd, duration);
					layer.parent = emitIndex[entry];
					layer.name = entry->name + "_paint";
				}
				else
				{
					layer.channels[CH_POS] = channels[CH_POS];
					layer.channels[CH_ANCHOR] = channels[CH_ANCHOR];
					layer.channels[CH_SCALE] = channels[CH_SCALE];
					layer.channels[CH_ROT] = channels[CH_ROT];
					layer.channels[CH_OPACITY] = applyWindow(
						channels[CH_OPACITY], entry->windowStart,
						entry->windowEnd, duration);
					int parent = 0;
					if (entry->parent != nullptr)
					{
						auto found = emitIndex.find(entry->parent);
						parent = found == emitIndex.end() ? 0 : found->second;
					}
					layer.parent = parent;
					layer.name = entry->name;
				}
				layer.shapes = shapes;
				emit.push_back(layer);
			}
			return emit;
		}

		//=====================================================
		// clips
		//=====================================================
		//-----------------------------------------------------
		//! python's `float()` of a token; false when it is not a number
		bool parseDouble(String const & text, double & out)
		{
			size_t begin = 0;
			size_t end = text.size();
			char const * spaces = " \t\n\r\f\v";
			while (begin < end && std::strchr(spaces, text[begin]) != nullptr &&
				text[begin] != '\0')
			{
				++begin;
			}
			while (end > begin &&
				std::strchr(spaces, text[end - 1]) != nullptr &&
				text[end - 1] != '\0')
			{
				--end;
			}
			if (begin >= end)
			{
				return false;
			}
			String trimmed = text.substr(begin, end - begin);
			char * stop = nullptr;
			double value = strtod(trimmed.c_str(), &stop);
			if (stop == nullptr || *stop != '\0')
			{
				return false;
			}
			out = value;
			return true;
		}
		//-----------------------------------------------------
		//! @brief named clips: an explicit override wins over markers; a marker
		//! comment's `#once` suffix makes a one-shot; a zero-duration marker
		//! extends to the next marker (or the end). No clips at all = the
		//! parser's implicit default.
		std::vector<ClipOut> buildClips(Context & ctx, double compIp,
			double duration, String const & clipsOverride)
		{
			std::vector<ClipOut> clips;
			if (!clipsOverride.empty())
			{
				size_t cursor = 0;
				while (cursor <= clipsOverride.size())
				{
					size_t comma = clipsOverride.find(',', cursor);
					String spec = clipsOverride.substr(cursor,
						comma == String::npos ? String::npos : comma - cursor);
					cursor = comma == String::npos ? clipsOverride.size() + 1
						: comma + 1;
					size_t begin = spec.find_first_not_of(" \t\n\r\f\v");
					size_t end = spec.find_last_not_of(" \t\n\r\f\v");
					String trimmed = begin == String::npos ? String()
						: spec.substr(begin, end - begin + 1);
					std::vector<String> parts;
					size_t part = 0;
					while (true)
					{
						size_t colon = trimmed.find(':', part);
						if (colon == String::npos)
						{
							parts.push_back(trimmed.substr(part));
							break;
						}
						parts.push_back(trimmed.substr(part, colon - part));
						part = colon + 1;
					}
					if (parts.size() < 3)
					{
						ctx.addError(formatText("bad --clips entry '%s' (want "
							"name:start:end[:loop|once])", spec.c_str()));
						continue;
					}
					ClipOut clip;
					clip.name = sanitizeName(parts[0], String());
					double clipStart = 0.0;
					double clipEnd = 0.0;
					if (!parseDouble(parts[1], clipStart) ||
						!parseDouble(parts[2], clipEnd))
					{
						ctx.addError(formatText("bad --clips frames in '%s'",
							spec.c_str()));
						continue;
					}
					clip.start = clipStart - compIp;
					clip.end = clipEnd - compIp;
					clip.loop = true;
					if (parts.size() >= 4)
					{
						if (parts[3] == "once")
						{
							clip.loop = false;
						}
						else if (parts[3] != "loop")
						{
							ctx.addError(formatText("bad --clips mode '%s' "
								"(loop|once)", parts[3].c_str()));
							continue;
						}
					}
					clips.push_back(clip);
				}
			}
			else
			{
				std::vector<JsonValue const *> markers;
				JsonValue const * markerList = member(&ctx.document, "markers");
				if (markerList != nullptr && markerList->isArray())
				{
					for (size_t index = 0; index < markerList->size(); ++index)
					{
						if (markerList->at(index).isObject())
						{
							markers.push_back(&markerList->at(index));
						}
					}
				}
				std::stable_sort(markers.begin(), markers.end(),
					[](JsonValue const * a, JsonValue const * b)
					{
						return numberOr(a, "tm", 0.0) < numberOr(b, "tm", 0.0);
					});
				for (size_t index = 0; index < markers.size(); ++index)
				{
					JsonValue const * marker = markers[index];
					JsonValue const * commentValue = member(marker, "cm");
					String comment = commentValue != nullptr
						? jsonStr(commentValue) : String();
					if (commentValue != nullptr && commentValue->isString())
					{
						comment = commentValue->asString();
					}
					size_t begin = comment.find_first_not_of(" \t\n\r\f\v");
					size_t end = comment.find_last_not_of(" \t\n\r\f\v");
					comment = begin == String::npos ? String()
						: comment.substr(begin, end - begin + 1);
					bool loop = true;
					String const suffix = "#once";
					if (comment.size() >= suffix.size() &&
						comment.compare(comment.size() - suffix.size(),
							suffix.size(), suffix) == 0)
					{
						loop = false;
						comment = comment.substr(0,
							comment.size() - suffix.size());
						size_t innerBegin =
							comment.find_first_not_of(" \t\n\r\f\v");
						size_t innerEnd =
							comment.find_last_not_of(" \t\n\r\f\v");
						comment = innerBegin == String::npos ? String()
							: comment.substr(innerBegin,
								innerEnd - innerBegin + 1);
					}
					ClipOut clip;
					clip.name = sanitizeName(comment,
						formatText("clip%d", static_cast<int>(index)));
					clip.start = numberOr(marker, "tm", 0.0) - compIp;
					double length = numberOr(marker, "dr", 0.0);
					if (length > EPS)
					{
						clip.end = clip.start + length;
					}
					else if (index + 1 < markers.size())
					{
						clip.end = numberOr(markers[index + 1], "tm", 0.0) -
							compIp;
					}
					else
					{
						clip.end = duration;
					}
					clip.loop = loop;
					clips.push_back(clip);
				}
			}

			std::set<String> seen;
			std::vector<ClipOut> out;
			for (ClipOut const & source : clips)
			{
				ClipOut clip = source;
				String base = clip.name;
				int suffix = 2;
				while (seen.find(clip.name) != seen.end())
				{
					clip.name = formatText("%s_%d", base.c_str(), suffix);
					++suffix;
				}
				seen.insert(clip.name);
				clip.start = std::max(0.0, clip.start);
				clip.end = std::min(duration, clip.end);
				if (clip.end <= clip.start + EPS)
				{
					ctx.addError(formatText("clip '%s' has an empty frame "
						"range (%g..%g) - give the marker a duration or fix "
						"--clips", base.c_str(), clip.start,
						clip.end));
					continue;
				}
				out.push_back(clip);
			}
			return out;
		}

		//=====================================================
		// emission
		//=====================================================
		//-----------------------------------------------------
		//! the shared `fill` / `linear` / `radial` paint vocabulary of one key
		void emitPaint(std::vector<String> & lines, char const * indent,
			Paint const & paint)
		{
			if (!paint.gradient)
			{
				lines.push_back(formatText("%sfill %.4f %.4f %.4f %.4f",
					indent, paint.r, paint.g, paint.b, paint.a));
				return;
			}
			lines.push_back(formatText("%s%s %s %s %s %s %d", indent,
				paint.radial ? "radial" : "linear",
				fmtVal(paint.start.x).c_str(), fmtVal(paint.start.y).c_str(),
				fmtVal(paint.end.x).c_str(), fmtVal(paint.end.y).c_str(),
				static_cast<int>(paint.stops.size())));
			if (paint.radial &&
				(std::fabs(paint.focal.x - paint.start.x) > EPS ||
					std::fabs(paint.focal.y - paint.start.y) > EPS))
			{
				lines.push_back(formatText("%sfocal %s %s", indent,
					fmtVal(paint.focal.x).c_str(),
					fmtVal(paint.focal.y).c_str()));
			}
			for (GradStop const & stop : paint.stops)
			{
				lines.push_back(formatText("%sstop %s %.4f %.4f %.4f %.4f",
					indent, fmtVal(stop.at).c_str(), stop.r, stop.g, stop.b,
					stop.a));
			}
		}
		//-----------------------------------------------------
		//! the `stroke W CAP JOIN LIMIT ENDS` spec of a stroke region
		void emitStroke(std::vector<String> & lines, char const * indent,
			StrokeSpec const & stroke)
		{
			lines.push_back(formatText("%sstroke %s %s %s %s %s", indent,
				fmtVal(stroke.width).c_str(), stroke.cap.c_str(),
				stroke.join.c_str(), fmtVal(stroke.miter).c_str(),
				stroke.closed ? "closed" : "open"));
		}
		//-----------------------------------------------------
		String joinLines(std::vector<String> const & lines)
		{
			size_t total = 0;
			for (String const & line : lines)
			{
				total += line.size() + 1;
			}
			String text;
			text.reserve(total);
			for (String const & line : lines)
			{
				text += line;
				text += '\n';
			}
			return text;
		}
		//-----------------------------------------------------
		String emitOanim(double fps, double duration,
			std::vector<ClipOut> const & clips,
			std::vector<EmitLayer> const & rig)
		{
			// the version only climbs when the document actually uses the newer
			// vocabulary, so untextured cooks stay byte-identical
			bool textured = false;
			for (EmitLayer const & layer : rig)
			{
				for (ShapeOut const & shape : layer.shapes)
				{
					if (!shape.texture.empty())
					{
						textured = true;
					}
				}
			}
			int version = textured ? 3 : 2;
			std::vector<String> lines;
			lines.push_back(formatText(OANIM_BANNER, version));
			lines.push_back(formatText("version %d", version));
			lines.push_back("fps " + fmtFrame(fps));
			lines.push_back("duration " + fmtFrame(duration));
			for (ClipOut const & clip : clips)
			{
				lines.push_back(formatText("clip %s %s %s %s",
					clip.name.c_str(), fmtFrame(clip.start).c_str(),
					fmtFrame(clip.end).c_str(), clip.loop ? "loop" : "once"));
			}
			static char const * const CHANNEL_NAMES[CH_COUNT] = { "pos",
				"anchor", "scale", "rot", "opacity" };
			for (EmitLayer const & layer : rig)
			{
				lines.push_back(formatText("layer %s parent %d",
					layer.name.c_str(), layer.parent));
				for (int channel = 0; channel < CH_COUNT; ++channel)
				{
					ChannelOut const & converted = layer.channels[channel];
					if (!converted.present ||
						channelIsDefault(channel, converted))
					{
						continue;
					}
					lines.push_back(formatText("  %s k %d",
						CHANNEL_NAMES[channel],
						static_cast<int>(converted.keys.size())));
					for (ChanKey const & key : converted.keys)
					{
						String values;
						for (size_t index = 0; index < key.values.size();
							++index)
						{
							if (index > 0)
							{
								values += ' ';
							}
							values += fmtVal(key.values[index]);
						}
						lines.push_back("    kf " + fmtFrame(key.frame) + " " +
							values + fmtEase(key.ease));
					}
				}
				for (ShapeOut const & shape : layer.shapes)
				{
					lines.push_back(formatText("  shape k %d",
						static_cast<int>(shape.keys.size())));
					for (KeyOut const & key : shape.keys)
					{
						lines.push_back("    kf " + fmtFrame(key.frame) +
							fmtEase(key.ease));
						emitPaint(lines, "      ", key.paint);
						if (!shape.texture.empty() && !key.outer.empty())
						{
							// the image rect IS the contour's bounds (the block
							// is the whole image pasted into its local rect)
							double minX = key.outer.front().x;
							double maxX = key.outer.front().x;
							double minY = key.outer.front().y;
							double maxY = key.outer.front().y;
							for (P2 const & point : key.outer)
							{
								minX = std::min(minX, point.x);
								maxX = std::max(maxX, point.x);
								minY = std::min(minY, point.y);
								maxY = std::max(maxY, point.y);
							}
							lines.push_back(formatText(
								"      texture %s %s %s %s %s",
								shape.texture.c_str(), fmtVal(minX).c_str(),
								fmtVal(minY).c_str(),
								fmtVal(maxX - minX).c_str(),
								fmtVal(maxY - minY).c_str()));
						}
						if (key.stroke.present)
						{
							emitStroke(lines, "      ", key.stroke);
						}
						lines.push_back(formatText("      contour %d",
							static_cast<int>(key.outer.size())));
						for (P2 const & point : key.outer)
						{
							lines.push_back("      v " + fmtVal(point.x) + " " +
								fmtVal(point.y));
						}
						for (std::vector<P2> const & hole : key.holes)
						{
							lines.push_back(formatText("      hole %d",
								static_cast<int>(hole.size())));
							for (P2 const & point : hole)
							{
								lines.push_back("      v " + fmtVal(point.x) +
									" " + fmtVal(point.y));
							}
						}
						if (key.hasMask && !key.mask.empty())
						{
							lines.push_back(formatText("      mask %d",
								static_cast<int>(key.mask.size())));
							for (P2 const & point : key.mask)
							{
								lines.push_back("      v " + fmtVal(point.x) +
									" " + fmtVal(point.y));
							}
						}
					}
				}
			}
			return joinLines(lines);
		}
		//-----------------------------------------------------
		bool rigIsStatic(std::vector<EmitLayer> const & rig)
		{
			for (EmitLayer const & layer : rig)
			{
				for (int channel = 0; channel < CH_COUNT; ++channel)
				{
					if (layer.channels[channel].present &&
						layer.channels[channel].animated)
					{
						return false;
					}
				}
				for (ShapeOut const & shape : layer.shapes)
				{
					if (!shape.keys.empty() && shape.keys.front().paint.gradient)
					{
						return false;	// `.oshape` has no gradient vocabulary
					}
					if (!shape.texture.empty())
					{
						// the static `.oshape` emitter bakes transforms INTO
						// the vertices, where a rotated axis-aligned texture
						// rect cannot follow - textured documents stay rigs
						return false;
					}
					if (shape.keys.size() > 1)
					{
						return false;
					}
				}
			}
			return true;
		}
		//-----------------------------------------------------
		//! @brief the static case: compose every layer's frame-0 transform
		//! chain and write a plain `.oshape` in world space.
		bool emitOshapeStatic(std::vector<EmitLayer> const & rig, String & out)
		{
			std::vector<Affine> affines;
			std::vector<double> opacities;
			std::vector<String> lines;
			lines.push_back(OSHAPE_BANNER);
			lines.push_back("version 2");
			int emitted = 0;
			for (EmitLayer const & layer : rig)
			{
				auto channelValue = [&layer](int channel)
				{
					ChannelOut const & converted = layer.channels[channel];
					if (!converted.present || converted.keys.empty())
					{
						return channelDefault(channel);
					}
					return converted.keys.front().values;
				};
				std::vector<double> pos = channelValue(CH_POS);
				std::vector<double> anchor = channelValue(CH_ANCHOR);
				std::vector<double> scaleValue = channelValue(CH_SCALE);
				double rot = channelValue(CH_ROT)[0] *
					(3.141592653589793 / 180.0);
				double opacity = channelValue(CH_OPACITY)[0];
				double cosR = std::cos(rot);
				double sinR = std::sin(rot);
				// +y-up CCW rotation composed with scale, anchored
				double la = cosR * scaleValue[0];
				double lb = -sinR * scaleValue[1];
				double lc = sinR * scaleValue[0];
				double ld = cosR * scaleValue[1];
				double tx = pos[0] - (la * anchor[0] + lb * anchor[1]);
				double ty = pos[1] - (lc * anchor[0] + ld * anchor[1]);
				Affine local(la, lb, lc, ld, tx, ty);
				int parent = layer.parent;
				Affine world = local;
				if (parent >= 0)
				{
					std::vector<Affine> chain;
					chain.push_back(affines[static_cast<size_t>(parent)]);
					chain.push_back(local);
					world = composeAffines(chain);
				}
				double worldOpacity = (parent >= 0
					? opacities[static_cast<size_t>(parent)] : 1.0) * opacity;
				affines.push_back(world);
				opacities.push_back(worldOpacity);

				double determinant = world.a * world.d - world.b * world.c;
				// a length under this world affine scales by its area factor
				double widthScale = std::fabs(determinant) > EPS
					? std::sqrt(std::fabs(determinant)) : 1.0;
				auto place = [&world](P2 const & point)
				{
					return P2(
						world.a * point.x + world.b * point.y + world.tx,
						world.c * point.x + world.d * point.y + world.ty);
				};
				for (ShapeOut const & shape : layer.shapes)
				{
					if (shape.keys.empty())
					{
						continue;
					}
					KeyOut const & key = shape.keys.front();
					lines.push_back(formatText("fill %.4f %.4f %.4f %.4f",
						key.paint.r, key.paint.g, key.paint.b,
						key.paint.a * worldOpacity));
					if (key.stroke.present)
					{
						StrokeSpec stroke = key.stroke;
						stroke.width *= widthScale;
						emitStroke(lines, "", stroke);
					}
					lines.push_back(formatText("contour %d",
						static_cast<int>(key.outer.size())));
					for (P2 const & point : key.outer)
					{
						P2 world2 = place(point);
						lines.push_back("v " + fmtVal(world2.x) + " " +
							fmtVal(world2.y));
					}
					for (std::vector<P2> const & hole : key.holes)
					{
						lines.push_back(formatText("hole %d",
							static_cast<int>(hole.size())));
						for (P2 const & point : hole)
						{
							P2 world2 = place(point);
							lines.push_back("v " + fmtVal(world2.x) + " " +
								fmtVal(world2.y));
						}
					}
					if (key.hasMask && !key.mask.empty())
					{
						lines.push_back(formatText("mask %d",
							static_cast<int>(key.mask.size())));
						for (P2 const & point : key.mask)
						{
							P2 world2 = place(point);
							lines.push_back("v " + fmtVal(world2.x) + " " +
								fmtVal(world2.y));
						}
					}
					++emitted;
				}
			}
			if (emitted == 0)
			{
				return false;
			}
			out = joinLines(lines);
			return true;
		}
	}

	//=============================================================
	// the public entry point
	//=============================================================
	//-------------------------------------------------------------
	bool VectorAnimCook::cook(String const & lottieJson,
		Options const & options, Result & out, String & errors)
	{
		Context ctx;
		if (!JsonValue::parse(lottieJson, ctx.document))
		{
			// deliberately position-less: the JSON reader is a bool API with
			// no offset readback, and threading one out through it for a
			// single diagnostic is not worth the churn. A source document is
			// written by an authoring tool, so a syntax error means a damaged
			// or truncated file rather than a typo to point at.
			errors = "not valid JSON";
			return false;
		}
		if (!ctx.document.isObject() ||
			!ctx.document.get("layers").isArray())
		{
			errors = "not a Lottie document (no layer list)";
			return false;
		}

		resolveLinkExpressions(ctx);

		double fps = numberOr(&ctx.document, "fr", 0.0);
		double compIp = numberOr(&ctx.document, "ip", 0.0);
		double compOp = numberOr(&ctx.document, "op", 0.0);
		double compW = numberOr(&ctx.document, "w", 0.0);
		double compH = numberOr(&ctx.document, "h", 0.0);
		double duration = compOp - compIp;
		if (fps <= 0.0)
		{
			ctx.addError("frame rate (fr) must be > 0");
		}
		if (duration <= 0.0)
		{
			ctx.addError("empty timeline (op must be greater than ip)");
		}
		if (compW <= 0.0 || compH <= 0.0)
		{
			ctx.addError("composition size (w/h) must be > 0");
		}
		auto joinErrors = [](std::vector<String> const & list)
		{
			String text;
			for (size_t index = 0; index < list.size(); ++index)
			{
				if (index > 0)
				{
					text += '\n';
				}
				text += list[index];
			}
			return text;
		};
		if (!ctx.errors.empty())
		{
			errors = joinErrors(ctx.errors);
			return false;
		}

		double span = std::max(compW, compH);
		double scale = options.extent / span;
		// flatten tolerance in composition units: 0.25% of the larger extent
		double tol = options.tolerance > 0.0 ? options.tolerance
			: span * 0.0025;

		flattenLayers(ctx);
		std::vector<ClipOut> clips = buildClips(ctx, compIp, duration,
			options.clips);
		std::vector<EmitLayer> rig;
		if (ctx.errors.empty())
		{
			rig = buildRig(ctx, duration, compW, compH, scale, tol);
		}
		if (!ctx.errors.empty())
		{
			errors = joinErrors(ctx.errors);
			return false;
		}
		bool anyShapes = false;
		for (EmitLayer const & layer : rig)
		{
			if (!layer.shapes.empty())
			{
				anyShapes = true;
				break;
			}
		}
		if (!anyShapes)
		{
			errors = "the document has no fillable shapes";
			return false;
		}

		if (rigIsStatic(rig))
		{
			String text;
			if (!emitOshapeStatic(rig, text))
			{
				errors = "the document has no fillable shapes";
				return false;
			}
			out.kind = KIND_OSHAPE;
			out.text = text;
			out.images = ctx.images;
			return true;
		}
		out.kind = KIND_OANIM;
		out.text = emitOanim(fps, duration, clips, rig);
		out.images = ctx.images;
		return true;
	}
}
