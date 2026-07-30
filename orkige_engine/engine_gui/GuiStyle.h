/********************************************************************
	created:	Wednesday 2026/07/29 at 09:00
	filename: 	GuiStyle.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __GuiStyle_h__29_7_2026__09_00_00__
#define __GuiStyle_h__29_7_2026__09_00_00__

//! @file GuiStyle.h
//! @brief the PURE `.oui` styling vocabulary: the named-style merge and the
//! value parsers for the text-style keys (`font`, `textColor`, `textScale`).
//! Pure text and plain floats - no renderer, no atlas, no scripting - so the
//! whole precedence contract and every malformed-input verdict is unit-tested
//! headlessly (GuiStyleTests) and compiles in the ORKIGE_NOSCRIPT build.
//!
//! NAMED STYLES. A `[Style NAME]` section carries a bundle of ordinary widget
//! keys (any subset: sprite, nineSlice, font, textColor, textScale, color,
//! size, ...). A widget references it with `style = NAME`. Application order is
//! STYLE FIRST, then the widget's own explicit keys override - the same
//! declaration-order precedence the scene-side atmosphere preset uses: the
//! bundle SEEDS the widget and every key the widget spells out itself wins.
//! Styles do not nest (a `style` key inside a `[Style]` section is ignored);
//! an unknown style name is one warn and the widget's own keys alone.
//!
//! FONT REFERENCE. `font` accepts EITHER a decimal `[Font.N]` index (`font =
//! 24`) or the NAME an atlas font declares with its own `name` key (`font =
//! heading`). Only the index/name DISCRIMINATION lives here (it is pure); the
//! name lookup itself belongs to UiAtlas, which owns the fonts.

#include "engine_module/EnginePrerequisites.h"
#include "engine_gui/GuiLayout.h"

#include <vector>

namespace Orkige
{
	namespace GuiStyle
	{
		//! @brief the `.oui` section type token of a named style, lower-cased
		//! (GuiFactory lower-cases every type before dispatch)
		ORKIGE_ENGINE_DLL char const * styleSectionType();
		//! @brief the widget key that references a named style
		ORKIGE_ENGINE_DLL char const * styleKey();

		//! @brief the `[Style NAME]` section named @p name, or NULL. Matching is
		//! case-insensitive on the TYPE token (like every other section) and
		//! case-SENSITIVE on the name (like every other id in the grammar).
		ORKIGE_ENGINE_DLL GuiLayoutSection const * findStyle(
			GuiLayoutDoc const & doc, String const & name);

		//! @brief the names of every `[Style NAME]` section, in declaration order
		//! (the editor's style dropdown reads this off the document it edits)
		ORKIGE_ENGINE_DLL std::vector<String> styleNames(GuiLayoutDoc const & doc);

		//! @brief resolve @p widget against the document's named styles: the
		//! referenced style's entries first (declaration order), then @p widget's
		//! OWN entries, each overwriting the style's value for the same key.
		//! @remarks The `style` key itself is dropped from the result (it is a
		//! reference, not a widget property), and a `style` key INSIDE the style
		//! section is ignored (styles do not nest). A widget with no `style` key
		//! resolves to a byte-identical copy of itself, so an unstyled screen
		//! behaves exactly as before.
		//! @param unknownStyleOut set to the referenced name when the document
		//! declares no such style (the caller warns once); cleared otherwise.
		//! @return the merged section (same type/id as @p widget).
		ORKIGE_ENGINE_DLL GuiLayoutSection resolveSection(
			GuiLayoutDoc const & doc, GuiLayoutSection const & widget,
			String * unknownStyleOut = NULL);

		//! @brief resolve EVERY widget section of @p doc against its style,
		//! leaving the non-widget sections ([Layout], [Style ...], [Modal ...],
		//! [ToggleGroup ...], [TabBar ...]) untouched. The one call GuiFactory
		//! makes before it builds a screen. @p unknownStyles collects each
		//! "<widgetId>:<styleName>" whose style is missing (one warn per entry).
		ORKIGE_ENGINE_DLL GuiLayoutDoc resolveDocument(GuiLayoutDoc const & doc,
			std::vector<String> * unknownStyles = NULL);

		//! @brief parse a `textColor = r g b a` value into @p rgba (0..1).
		//! Three components are accepted (alpha defaults to 1); fewer, more, or a
		//! non-numeric component is REFUSED with @p error set and @p rgba
		//! untouched, so the caller keeps the widget's current colour and warns.
		//! Components are NOT clamped (a value outside 0..1 is the author's call
		//! and the renderer's business), but they must be finite numbers.
		ORKIGE_ENGINE_DLL bool parseTextColour(String const & value,
			float rgba[4], String & error);

		//! @brief parse a `textScale = f` value: a finite factor > 0 (1 = the
		//! font's baked size). Anything else is REFUSED with @p error set.
		ORKIGE_ENGINE_DLL bool parseTextScale(String const & value, float & out,
			String & error);

		//! @brief is @p value a decimal `[Font.N]` index rather than a font NAME?
		//! (a run of digits, nothing else). True fills @p indexOut.
		ORKIGE_ENGINE_DLL bool isFontIndexLiteral(String const & value,
			uint & indexOut);
	}
}

#endif //__GuiStyle_h__29_7_2026__09_00_00__
