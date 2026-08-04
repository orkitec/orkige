#!/usr/bin/env python3
"""Diagnosis for the cross-flavor parity gates: a picture, and a shape.

The gates score a pair of frames with NUMBERS - a mean absolute error, an
outlier fraction, per-region colour deltas. A number says two frames differ.
It cannot say WHERE the disagreement sits, or what shape it has, and those are
the facts that separate a bug from the rasterizers' noise: ten thousand
scattered one-level pixels and one badly rendered object can score the same
mean. Two instruments, shared by every driver that compares a frame pair:

  * THE DIFF IMAGE. `delta_map` reduces a pair to one worst-channel delta per
    pixel; `write_diff_png` paints that map over a dimmed grayscale of the
    reference frame, so the picture shows where in the SCENE the disagreement
    lives. The heat ramp is ABSOLUTE (see HEAT_RAMP), never normalised to the
    frame's own maximum - two diff images from different runs are directly
    comparable, and a nearly-clean frame stays nearly black instead of being
    amplified into an alarm.
  * THE SPATIAL SUMMARY. `spatial_summary` clusters the pixels above a
    threshold into 8-connected regions and reports the LARGEST one with its
    bounding box. "One object is wrong" and "everything is a shade off" are
    different facts, and usually only the first is a bug.

The spatial summary is REPORTED, NEVER GATED. Its healthy value on the CI
hosts (a software GL rasterizer against a software Vulkan one) has never been
measured, and a threshold invented without a measurement is a new way to block
merges rather than a new way to catch bugs. Printing it on every run - green
ones included - is what makes a corridor measurable later; a gate can be added
then, from numbers, in one line.

Pure stdlib. The PNG write is Util/orkige_png.py's encoder: the repository's
one stdlib PNG writer, so a second one cannot drift from it.
"""

import os
import sys
from collections import namedtuple

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "..", "Util"))
from orkige_png import Image, encode_png  # noqa: E402

#: a pixel counts as "differing" above this per-channel delta. The same value
#: the facade pixel gate uses for its outlier fraction, so every driver's
#: region numbers are in one unit and comparable across the gates.
DEFAULT_THRESHOLD = 48

#: absolute heat ramp, (delta, rgb) stops interpolated in between. Deliberately
#: not normalised per frame - see the module docstring.
HEAT_RAMP = (
    (0, (0, 0, 0)),          # identical
    (16, (0, 0, 255)),       # a shade off
    (48, (0, 255, 255)),     # the outlier threshold
    (128, (255, 255, 0)),    # plainly the wrong colour
    (255, (255, 0, 0)),      # inverted / missing content
)

#: how much of the reference frame shows through as context (0..1)
BASE_GAIN = 0.28

DeltaMap = namedtuple("DeltaMap", "width height channels deltas channel_sum")
Spatial = namedtuple("Spatial", "over regions largest box")


def delta_map(image_a, image_b):
    """Reduce a frame pair to one worst-channel delta per pixel.

    Each image is the drivers' decode tuple (width, height, channels, data).
    Returns a DeltaMap carrying the per-pixel map AND the summed per-channel
    difference, so a caller computes a mean off the same single pass instead
    of walking the pixels twice. Raises ValueError on a dimension mismatch -
    two differently sized frames have no per-pixel correspondence at all.
    """
    width, height, channels_a, data_a = image_a
    width_b, height_b, channels_b, data_b = image_b
    if (width, height) != (width_b, height_b):
        raise ValueError(f"dimension mismatch: {width}x{height} vs "
                         f"{width_b}x{height_b}")
    compare = min(channels_a, channels_b, 3)   # RGB only; alpha is format noise
    count = width * height
    deltas = bytearray(count)
    channel_sum = 0
    base_a = base_b = 0
    for index in range(count):
        worst = 0
        for channel in range(compare):
            diff = data_a[base_a + channel] - data_b[base_b + channel]
            if diff < 0:
                diff = -diff
            channel_sum += diff
            if diff > worst:
                worst = diff
        deltas[index] = worst
        base_a += channels_a
        base_b += channels_b
    return DeltaMap(width, height, compare, deltas, channel_sum)


def spatial_summary(dmap, threshold=DEFAULT_THRESHOLD):
    """Cluster the over-threshold pixels into 8-connected regions.

    8-connected rather than 4-: an antialiased silhouette differs along a
    diagonal chain, and splitting one wrong object into a dozen diagonal
    fragments would report the opposite of the fact wanted here. Returns a
    Spatial with the total over-threshold count, the region count, the LARGEST
    region's pixel count and its bounding box (x0, y0, x1, y1) inclusive.
    """
    width, height, deltas = dmap.width, dmap.height, dmap.deltas
    visited = bytearray(width * height)
    over = regions = largest = 0
    box = None
    for start in range(width * height):
        if visited[start] or deltas[start] <= threshold:
            continue
        regions += 1
        visited[start] = 1
        stack = [start]
        size = 0
        x0 = x1 = start % width
        y0 = y1 = start // width
        while stack:
            index = stack.pop()
            size += 1
            x = index % width
            y = index // width
            if x < x0:
                x0 = x
            elif x > x1:
                x1 = x
            if y < y0:
                y0 = y
            elif y > y1:
                y1 = y
            for neighbour_y in range(max(0, y - 1), min(height, y + 2)):
                row = neighbour_y * width
                for neighbour_x in range(max(0, x - 1), min(width, x + 2)):
                    neighbour = row + neighbour_x
                    if not visited[neighbour] and deltas[neighbour] > threshold:
                        visited[neighbour] = 1
                        stack.append(neighbour)
        over += size
        if size > largest:
            largest = size
            box = (x0, y0, x1, y1)
    return Spatial(over, regions, largest, box)


def describe(spatial, total_pixels, threshold=DEFAULT_THRESHOLD):
    """One line naming the SHAPE of the disagreement (reported, not gated)."""
    if spatial.largest == 0:
        return f"no pixel over {threshold}"
    x0, y0, x1, y1 = spatial.box
    return (f"largest region {spatial.largest}px "
            f"({100.0 * spatial.largest / float(total_pixels):.3f}%) at "
            f"({x0},{y0})-({x1},{y1}); {spatial.regions} region(s), "
            f"{spatial.over}px over {threshold}")


def heat_colour(delta):
    """The absolute HEAT_RAMP colour for one delta (0..255)."""
    previous_stop, previous_rgb = HEAT_RAMP[0]
    for stop, rgb in HEAT_RAMP[1:]:
        if delta <= stop:
            span = stop - previous_stop
            weight = (delta - previous_stop) / float(span) if span else 0.0
            return tuple(int(round(previous_rgb[i] +
                                   (rgb[i] - previous_rgb[i]) * weight))
                         for i in range(3))
        previous_stop, previous_rgb = stop, rgb
    return HEAT_RAMP[-1][1]


def write_diff_png(path, dmap, reference=None):
    """Write the delta map as a heat image over a dimmed reference frame.

    `reference` is one of the compared decode tuples (the classic frame, by
    convention) and supplies the scene context: without it a heat map floats
    on black and a reader cannot tell which object is wrong. Composited as a
    per-channel max, so even a delta of 1 stays visible against lit content.
    """
    width, height = dmap.width, dmap.height
    image = Image(width, height)
    pixels = image.pixels
    ramp = [heat_colour(delta) for delta in range(256)]
    ref_data = ref_channels = None
    if reference is not None and (reference[0], reference[1]) == (width, height):
        ref_channels = reference[2]
        ref_data = reference[3]
    for index in range(width * height):
        red, green, blue = ramp[dmap.deltas[index]]
        if ref_data is not None:
            base = index * ref_channels
            if ref_channels >= 3:
                luma = (77 * ref_data[base] + 151 * ref_data[base + 1] +
                        28 * ref_data[base + 2]) >> 8
            else:
                luma = ref_data[base]
            grey = int(luma * BASE_GAIN)
            if grey > red:
                red = grey
            if grey > green:
                green = grey
            if grey > blue:
                blue = grey
        out = index * 4
        pixels[out] = red
        pixels[out + 1] = green
        pixels[out + 2] = blue
        pixels[out + 3] = 255
    encode_png(image, path)
    return path


def diff_path(compared_path, diff_dir=None):
    """Where a pair's diff image lands: beside the compared frame by default.

    Beside it is what makes the artifact travel - the parity job uploads the
    directory tree it compared, so a diff written into it rides out with the
    failure and needs no second upload path.
    """
    stem = os.path.splitext(os.path.basename(compared_path))[0]
    directory = diff_dir or os.path.dirname(os.path.abspath(compared_path))
    return os.path.join(directory, stem + ".diff.png")


def try_write_diff(path, dmap, reference=None):
    """Write the diff image; report but never RAISE if the write fails.

    A diagnosis must not invent a failure mode of its own: an unwritable
    output directory changes what the reader gets, never what the gate said.
    """
    try:
        directory = os.path.dirname(os.path.abspath(path))
        if directory:
            os.makedirs(directory, exist_ok=True)
        return write_diff_png(path, dmap, reference)
    except OSError as error:
        print(f"(no diff image written to {path}: {error})")
        return None


def drop_stale_diff(path):
    """Remove a diff image left by an earlier failing run of the same pair.

    A run that now agrees must not leave a picture of a disagreement behind:
    the file would outlive the fix and read as a live failure.
    """
    try:
        os.remove(path)
    except OSError:
        pass


# --- selftest ---------------------------------------------------------------
#
# Exercised through the two parity drivers' own --selftest runs (they are the
# registered ctest entries); these helpers keep the pure assertions in one
# place so both call the same checks.

def make_map(width, height, deltas):
    """A DeltaMap straight from a delta list - fixtures without an image."""
    return DeltaMap(width, height, 3, bytearray(deltas), sum(deltas))


def selftest_pure():
    """Assert the pure parts: clustering, the ramp, the diff path."""
    # one 3x3 block, one isolated pixel: the block is the largest region
    deltas = [0] * 64
    for y in range(1, 4):
        for x in range(1, 4):
            deltas[y * 8 + x] = 200
    deltas[7 * 8 + 7] = 200
    spatial = spatial_summary(make_map(8, 8, deltas))
    assert spatial.largest == 9, spatial
    assert spatial.regions == 2, spatial
    assert spatial.over == 10, spatial
    assert spatial.box == (1, 1, 3, 3), spatial

    # 8-connectivity: a diagonal chain is ONE region, not three
    diagonal = [0] * 64
    for i in range(3):
        diagonal[(i + 2) * 8 + (i + 2)] = 200
    assert spatial_summary(make_map(8, 8, diagonal)).regions == 1

    # a pixel exactly AT the threshold is not over it
    assert spatial_summary(make_map(2, 1, [DEFAULT_THRESHOLD, 0])).largest == 0
    assert spatial_summary(
        make_map(2, 1, [DEFAULT_THRESHOLD + 1, 0])).largest == 1

    # a clean frame says so, and names no box
    clean = spatial_summary(make_map(4, 4, [0] * 16))
    assert clean.largest == 0 and clean.box is None
    assert "no pixel over" in describe(clean, 16)
    assert "largest region 9px" in describe(spatial, 64)

    # the ramp is absolute and monotone in heat: dark at 0, red at the top
    assert heat_colour(0) == (0, 0, 0)
    assert heat_colour(255) == (255, 0, 0)
    assert heat_colour(48) == (0, 255, 255)
    assert heat_colour(300) == (255, 0, 0)      # clamped, never wrapped

    # the diff lands beside the frame it explains, unless told otherwise
    assert diff_path(os.path.join("a", "b", "next.png")) == \
        os.path.join(os.path.abspath(os.path.join("a", "b")), "next.diff.png")
    assert diff_path("next.png", "elsewhere") == \
        os.path.join("elsewhere", "next.diff.png")


def selftest_roundtrip(decoder, scratch):
    """Write a diff image and read it back with the caller's own decoder.

    The writer and the drivers' decoders are separate implementations; this is
    the one place they meet, so the round trip is worth asserting.
    """
    deltas = [0] * 64
    deltas[3 * 8 + 3] = 255
    path = os.path.join(scratch, "roundtrip.diff.png")
    write_diff_png(path, make_map(8, 8, deltas))
    width, height, channels, data = decoder(path)
    assert (width, height) == (8, 8), (width, height)
    hot = (3 * 8 + 3) * channels
    assert data[hot] == 255 and data[hot + 1] == 0, data[hot:hot + 3]
    cold = 0
    assert data[cold] == data[cold + 1] == data[cold + 2] == 0
    os.remove(path)
