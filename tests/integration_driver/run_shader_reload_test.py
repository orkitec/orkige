#!/usr/bin/env python3
"""Shader-file hot-reload, end to end and per flavor.

An edit to a shader SOURCE FILE must reach the RUNNING game's next frame with
no restart - the second half of look-dev without compiles. The proof is a
picture, taken three times from one boot:

  1. copy the flavor's shader media tree into a scratch directory and point
     the run at it (the one media override each flavor resolves through
     engine_util/ShaderMediaDir.h), so the repository's own media and the
     installed engine media are never written to;
  2. write the EDITED variant of one shader file beside the original - the
     edit is an unmistakable, compiling change to the lit output;
  3. run the player once. Its selfcheck captures the frame, copies the edited
     file over the original, calls RenderSystem::reloadShaderFiles (what the
     reload_shaders debug message runs on a live session), captures again,
     restores the file, reloads once more and captures a third time;
  4. compare: the frame MOVED on the edit, and RETURNED on the restore. The
     restore leg is what makes it a reload rather than a one-way degradation.

Which file is edited is per flavor - the mechanism differs, so the fixture
does too:

  next     the Hlms pixel-shader template piece, at both of its output lines.
  classic  the engine shader library the generated surface shaders pull in as
           an `#include`, at the lighting function's accumulated result.

Both edits zero the green and blue channels of the lit result rather than
scaling its brightness: a magnitude change can be swallowed by a surface that
was already saturating, while dropping two channels moves a pixel of ANY
intensity - white or blown-out included - by a fixed, large amount.

Pure stdlib (zlib PNG decode). Runs per flavor.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import zlib

SKIP_EXIT_CODE = 77

#: the edit must move the frame by at least this much mean absolute luminance
#: (0-255). Comfortably above 8-bit readback noise, comfortably below what the
#: fixture edits actually produce.
#: A pixel counts as MOVED when its luminance changed by this much (0-255).
#: The verdict is the SHARE of moved pixels rather than a frame mean, because
#: how much of a frame a flavor's shader files paint is a property of the
#: fixture, not of the reload: one flavor may repaint nearly everything and the
#: other only the surfaces its shader library reaches. A share is comparable
#: across both, and it separates a real shading change (a large move over a
#: modest area) from a fixture that merely animates (a tiny move everywhere).
#: (measured on the committed fixture: the edit moves 65-79% of the frame on
#: the two flavors, an animating water surface leaves 0.6-1.2% behind)
STRONG_PIXEL_DELTA = 8.0
#: the edit must move at least this share of the frame
MIN_MOVED_FRACTION = 0.25
#: after the restore, essentially nothing may still be moved - the band is not
#: zero because the fixture scene animates between two captures
MAX_RESIDUAL_FRACTION = 0.05

#: per flavor: the shader source file (relative to the media root) and the
#: substitutions that make the edited variant. Every substitution must apply,
#: or the fixture has drifted from the shipped shader and the run fails by name
#: rather than quietly testing nothing.
FIXTURES = {
    "next": {
        "file": "Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any",
        "edits": [
            ("= sqrt( finalColour );",
             "= sqrt( finalColour ) * midf3_c( 1.0, 0.0, 0.0 );"),
            ("= finalColour;", "= finalColour * midf3_c( 1.0, 0.0, 0.0 );"),
        ],
    },
    "classic": {
        "file": "OrkigeLib_MetalRough.glsl",
        "edits": [
            ("vOutColour += pixel.baseColor * ambient.rgb * pixel.ambientOcclusion;",
             "vOutColour += pixel.baseColor * ambient.rgb * pixel.ambientOcclusion;\n"
             "    vOutColour *= vec3(1.0, 0.0, 0.0);"),
            ("colour.rgb = sqrt(max(colour.rgb, vec3_splat(0.0)));",
             "colour.rgb = sqrt(max(colour.rgb, vec3_splat(0.0))) * "
             "vec3(1.0, 0.0, 0.0);"),
        ],
    },
}

#: the environment variable each flavor's shader media root is overridden with
MEDIA_ENV = {
    "next": "ORKIGE_NEXT_HLMS_MEDIA_DIR",
    "classic": "ORKIGE_CLASSIC_RTSS_MEDIA_DIR",
}


def decode_png(path):
    """Minimal PNG decoder: 8-bit RGB/RGBA/gray, non-interlaced -> (w,h,ch,bytes)."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    width = height = colour_type = None
    idat = bytearray()
    while pos < len(data):
        length, chunk_type = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            (width, height, bit_depth, colour_type,
             _c, _f, interlace) = struct.unpack(">IIBBBBB", chunk)
            if bit_depth != 8 or colour_type not in (0, 2, 6) or interlace != 0:
                raise ValueError(f"{path}: unsupported PNG")
        elif chunk_type == b"IDAT":
            idat.extend(chunk)
        elif chunk_type == b"IEND":
            break
    channels = {0: 1, 2: 3, 6: 4}[colour_type]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(width * height * channels)
    previous = bytearray(stride)
    src = 0
    for row in range(height):
        ftype = raw[src]
        src += 1
        line = bytearray(raw[src:src + stride])
        src += stride
        if ftype == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = previous[i]
                up_left = previous[i - channels] if i >= channels else 0
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                predictor = left if (pa <= pb and pa <= pc) else (up if pb <= pc else up_left)
                line[i] = (line[i] + predictor) & 0xFF
        elif ftype != 0:
            raise ValueError(f"{path}: unknown PNG filter {ftype}")
        out[row * stride:(row + 1) * stride] = line
        previous = line
    return width, height, channels, out


class Image:
    def __init__(self, path):
        self.w, self.h, self.ch, self.px = decode_png(path)

    def luminance_rows(self, step=4):
        """a coarse per-pixel luminance grid - enough resolution to see a
        shading change, cheap enough for a stdlib decoder"""
        rows = []
        for y in range(0, self.h, step):
            row = []
            for x in range(0, self.w, step):
                base = (y * self.w + x) * self.ch
                row.append((self.px[base] + self.px[base + 1] +
                            self.px[base + 2]) / 3.0)
            rows.append(row)
        return rows


def frame_compare(image_a, image_b):
    """(share of strongly moved pixels, mean absolute delta) between two
    same-size captures; the share is the verdict, the mean is for the report"""
    if (image_a.w, image_a.h) != (image_b.w, image_b.h):
        raise RuntimeError(
            f"the captures differ in size ({image_a.w}x{image_a.h} vs "
            f"{image_b.w}x{image_b.h}) - the window was resized mid-run")
    a = image_a.luminance_rows()
    b = image_b.luminance_rows()
    total = 0.0
    moved = 0
    count = 0
    for row_a, row_b in zip(a, b):
        for pixel_a, pixel_b in zip(row_a, row_b):
            delta = abs(pixel_a - pixel_b)
            total += delta
            if delta >= STRONG_PIXEL_DELTA:
                moved += 1
            count += 1
    count = max(count, 1)
    return moved / count, total / count


def stage_media(source_root, scratch_root):
    """copy the flavor's shader media tree so the run may edit it"""
    if os.path.isdir(scratch_root):
        shutil.rmtree(scratch_root)
    shutil.copytree(source_root, scratch_root)


def write_edited_variant(target_path, edit_path, edits):
    """produce the edited shader beside the original; every substitution must
    apply, or the fixture no longer matches the shipped shader"""
    with open(target_path, "r", encoding="utf-8") as handle:
        text = handle.read()
    for needle, replacement in edits:
        if needle not in text:
            raise RuntimeError(
                f"{target_path}: the fixture expected to find "
                f"{needle!r} and did not - the shipped shader changed shape, "
                f"so this probe would prove nothing")
        text = text.replace(needle, replacement)
    with open(edit_path, "w", encoding="utf-8") as handle:
        handle.write(text)


def check(failures, ok, message):
    print(("ok:   " if ok else "FAIL: ") + message)
    if not ok:
        failures.append(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--player", required=True, help="the orkige_player app")
    parser.add_argument("--flavor", required=True, choices=sorted(FIXTURES),
                        help="which render flavor this tree built")
    parser.add_argument("--media", required=True,
                        help="the flavor's shader media root to copy")
    parser.add_argument("--project", required=True,
                        help="the fixture project the player boots")
    parser.add_argument("--scene", required=True,
                        help="the fixture scene, project-relative")
    parser.add_argument("--out", required=True, help="scratch dir")
    parser.add_argument("--timeout", type=int, default=240)
    args = parser.parse_args()

    if not os.path.exists(args.player):
        print(f"SKIP: player not built: {args.player}")
        return SKIP_EXIT_CODE
    if not os.path.isdir(args.media):
        print(f"SKIP: no shader media tree at {args.media}")
        return SKIP_EXIT_CODE

    fixture = FIXTURES[args.flavor]
    media_root = os.path.join(args.out, "media")
    shots = os.path.join(args.out, "shots")
    os.makedirs(shots, exist_ok=True)
    stage_media(args.media, media_root)

    target = os.path.join(media_root, fixture["file"])
    if not os.path.exists(target):
        print(f"FAIL: the staged media carries no {fixture['file']}")
        return 1
    edited = os.path.join(args.out, "edited_shader.txt")
    try:
        write_edited_variant(target, edited, fixture["edits"])
    except RuntimeError as error:
        print(f"FAIL: {error}")
        return 1

    env = dict(os.environ,
               ORKIGE_AUTOMATED_RUN="1",
               ORKIGE_SHADER_RELOAD_SELFCHECK=shots,
               ORKIGE_SHADER_RELOAD_TARGET=target,
               ORKIGE_SHADER_RELOAD_EDIT=edited)
    env[MEDIA_ENV[args.flavor]] = media_root
    command = [args.player, args.scene, "--project", args.project]
    try:
        result = subprocess.run(command, env=env, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        print(f"FAIL: the player did not finish within {args.timeout}s")
        return 1
    if result.returncode == SKIP_EXIT_CODE:
        print("SKIP: the player has no display session")
        return SKIP_EXIT_CODE
    if result.returncode != 0:
        print(f"FAIL: the player exited {result.returncode}")
        return 1

    frames = {}
    for name in ("before", "after", "restored"):
        path = os.path.join(shots, f"shader_{name}.png")
        if not os.path.exists(path):
            print(f"FAIL: the run captured no {name} frame at {path}")
            return 1
        frames[name] = Image(path)

    failures = []
    edit_moved, edit_mean = frame_compare(frames["before"], frames["after"])
    residual, residual_mean = frame_compare(frames["before"],
                                            frames["restored"])
    check(failures, edit_moved > MIN_MOVED_FRACTION,
          f"the shader edit reached the running frame ({edit_moved * 100:.1f}% "
          f"of the frame moved > {MIN_MOVED_FRACTION * 100:.1f}%, "
          f"mean delta {edit_mean:.2f})")
    check(failures, residual < MAX_RESIDUAL_FRACTION,
          f"restoring the shader file brings the frame back "
          f"({residual * 100:.2f}% still moved < "
          f"{MAX_RESIDUAL_FRACTION * 100:.2f}%, mean delta "
          f"{residual_mean:.2f})")
    if failures:
        print(f"{len(failures)} shader reload check(s) failed")
        return 1
    print(f"shader reload ({args.flavor}): the edit and the restore both "
          f"reached the next frame with no restart")
    return 0


if __name__ == "__main__":
    sys.exit(main())
