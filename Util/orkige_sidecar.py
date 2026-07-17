#!/usr/bin/env python3
"""Sidecar (.orkmeta) stamping for GENERATED textures (stdlib only).

Generated glyph/UI/sprite atlases and baked normal maps are pixel-exact
artwork: GPU block compression smears glyph edges and distorts normals, so
the asset generators stamp format="none" into the texture's sidecar at
generation time - the export cook then ships the PNG untouched. The stamp:

  * PRESERVES an existing sidecar's id (references ride the id) and mints a
    fresh 128-bit one for a brand-new file (the same shape the editor's
    AssetDatabase would mint on import),
  * NEVER overrides a format the user already chose - a sidecar whose
    <texture> block carries a format attribute is left untouched, so the
    field stays flippable per atlas (a decorative atlas may opt in to
    compression through the editor),
  * keeps every other attribute of an existing <texture> block verbatim.
"""

import os
import re
import secrets
import xml.etree.ElementTree as ET

META_EXTENSION = ".orkmeta"

_FRESH_TEMPLATE = (
    '<orkmeta id="%s">\n'
    '    <texture filter="bilinear" wrap="clamp" maxSize="0" '
    'premultiply="false" generateMips="false" format="%s" '
    'quality="normal"/>\n'
    '</orkmeta>\n')


def stamp_texture_sidecar(texture_path, fmt="none"):
    """ensure texture_path's sidecar carries a <texture> block with the given
    format, following the rules above; returns the sidecar path"""
    meta_path = texture_path + META_EXTENSION
    asset_id = None
    if os.path.isfile(meta_path):
        try:
            root = ET.parse(meta_path).getroot()
        except ET.ParseError:
            root = None
        if root is not None and root.tag == "orkmeta":
            candidate = root.get("id", "")
            if re.fullmatch(r"[0-9a-fA-F]{32}", candidate):
                asset_id = candidate
            texture = root.find("texture")
            if texture is not None:
                if texture.get("format") is not None:
                    return meta_path  # a chosen format is the user's intent
                # an existing settings block: add the stamp, keep the rest
                texture.set("format", fmt)
                if texture.get("quality") is None:
                    texture.set("quality", "normal")
                ET.ElementTree(root).write(meta_path)
                return meta_path
    if not asset_id:
        asset_id = secrets.token_hex(16)
    with open(meta_path, "w", newline="\n") as handle:
        handle.write(_FRESH_TEMPLATE % (asset_id, fmt))
    return meta_path
