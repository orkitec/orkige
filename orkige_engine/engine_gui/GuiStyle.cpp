/********************************************************************
	created:	Wednesday 2026/07/29 at 09:00
	filename: 	GuiStyle.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	the pure .oui styling vocabulary (@see GuiStyle.h).
*********************************************************************/

#include "engine_gui/GuiStyle.h"

#include <core_util/StringUtil.h>

#include <cmath>
#include <sstream>

namespace Orkige
{
	namespace GuiStyle
	{
		namespace
		{
			//! the section types that are NOT widgets (no style resolution)
			bool isNonWidgetType(String const & lowered)
			{
				return lowered == "layout" || lowered == "style" ||
					lowered == "modal" || lowered == "togglegroup" ||
					lowered == "tabbar";
			}
			//! read one finite float from a stream, refusing anything trailing
			bool readFinite(std::istringstream & stream, float & out)
			{
				if(!(stream >> out))
				{
					return false;
				}
				return std::isfinite(out);
			}
		}
		//---------------------------------------------------------
		char const * styleSectionType()
		{
			return "style";
		}
		//---------------------------------------------------------
		char const * styleKey()
		{
			return "style";
		}
		//---------------------------------------------------------
		GuiLayoutSection const * findStyle(GuiLayoutDoc const & doc,
			String const & name)
		{
			if(name.empty())
			{
				return NULL;
			}
			for(GuiLayoutSection const & section : doc.sections)
			{
				if(StringUtil::to_lower_copy(section.type) == styleSectionType() &&
					section.id == name)
				{
					return &section;
				}
			}
			return NULL;
		}
		//---------------------------------------------------------
		std::vector<String> styleNames(GuiLayoutDoc const & doc)
		{
			std::vector<String> names;
			for(GuiLayoutSection const & section : doc.sections)
			{
				if(StringUtil::to_lower_copy(section.type) == styleSectionType() &&
					!section.id.empty())
				{
					names.push_back(section.id);
				}
			}
			return names;
		}
		//---------------------------------------------------------
		GuiLayoutSection resolveSection(GuiLayoutDoc const & doc,
			GuiLayoutSection const & widget, String * unknownStyleOut)
		{
			if(unknownStyleOut != NULL)
			{
				unknownStyleOut->clear();
			}
			String const * reference = widget.find(styleKey());
			if(reference == NULL || reference->empty())
			{
				return widget;	// unstyled: a byte-identical copy
			}
			GuiLayoutSection const * style = findStyle(doc, *reference);
			GuiLayoutSection out;
			out.type = widget.type;
			out.id = widget.id;
			if(style == NULL)
			{
				// an unknown style is one warn and the widget's own keys alone -
				// never a refusal to build the widget
				if(unknownStyleOut != NULL)
				{
					*unknownStyleOut = *reference;
				}
			}
			else
			{
				// the bundle SEEDS the widget (declaration order); a `style` key
				// inside a style section is ignored - styles do not nest
				for(GuiLayoutEntry const & entry : style->entries)
				{
					if(entry.key == styleKey())
					{
						continue;
					}
					out.set(entry.key, entry.value);
				}
			}
			// the widget's OWN keys override, in the widget's declaration order;
			// the `style` reference itself is not a widget property
			for(GuiLayoutEntry const & entry : widget.entries)
			{
				if(entry.key == styleKey())
				{
					continue;
				}
				out.set(entry.key, entry.value);
			}
			return out;
		}
		//---------------------------------------------------------
		GuiLayoutDoc resolveDocument(GuiLayoutDoc const & doc,
			std::vector<String> * unknownStyles)
		{
			GuiLayoutDoc out;
			out.sections.reserve(doc.sections.size());
			for(GuiLayoutSection const & section : doc.sections)
			{
				const String lowered = StringUtil::to_lower_copy(section.type);
				if(isNonWidgetType(lowered) || section.id.empty())
				{
					out.sections.push_back(section);
					continue;
				}
				String unknown;
				out.sections.push_back(resolveSection(doc, section, &unknown));
				if(!unknown.empty() && unknownStyles != NULL)
				{
					unknownStyles->push_back(section.id + ":" + unknown);
				}
			}
			return out;
		}
		//---------------------------------------------------------
		bool parseTextColour(String const & value, float rgba[4], String & error)
		{
			error.clear();
			std::istringstream stream(value);
			float parsed[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			for(int each = 0; each < 3; ++each)
			{
				if(!readFinite(stream, parsed[each]))
				{
					error = "expected 'r g b [a]' with 0..1 components, got '" +
						value + "'";
					return false;
				}
			}
			// the alpha is optional; anything AFTER it is a malformed value
			if(stream >> parsed[3])
			{
				if(!std::isfinite(parsed[3]))
				{
					error = "the alpha component of '" + value +
						"' is not a finite number";
					return false;
				}
			}
			else
			{
				parsed[3] = 1.0f;
			}
			String trailing;
			if(stream >> trailing)
			{
				error = "'" + value + "' carries more than four components";
				return false;
			}
			for(int each = 0; each < 4; ++each)
			{
				rgba[each] = parsed[each];
			}
			return true;
		}
		//---------------------------------------------------------
		bool parseTextScale(String const & value, float & out, String & error)
		{
			error.clear();
			std::istringstream stream(value);
			float parsed = 0.0f;
			if(!readFinite(stream, parsed))
			{
				error = "expected a positive scale factor, got '" + value + "'";
				return false;
			}
			String trailing;
			if(stream >> trailing)
			{
				error = "'" + value + "' is not a single scale factor";
				return false;
			}
			if(parsed <= 0.0f)
			{
				error = "a text scale must be greater than zero, got '" + value +
					"'";
				return false;
			}
			out = parsed;
			return true;
		}
		//---------------------------------------------------------
		bool isFontIndexLiteral(String const & value, uint & indexOut)
		{
			if(value.empty())
			{
				return false;
			}
			for(char const c : value)
			{
				if(c < '0' || c > '9')
				{
					return false;
				}
			}
			unsigned long parsed = 0;
			std::istringstream stream(value);
			if(!(stream >> parsed))
			{
				return false;
			}
			indexOut = uint(parsed);
			return true;
		}
	}
}
