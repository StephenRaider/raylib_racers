#!/usr/bin/env python3
"""Traces a track centreline from a circuit map image (a thick line on a plain background).

    python3 tools/tracetrack.py MAP.png OUT.json --length 5700 [--start X,Y] [--cut-labels] [--flip] [--rotate DEG]

--start is the start line's pixel position in the image. --cut-labels removes corner-number
circles that touch the track (F1-style maps where they bend the trace).

Writes {"points": [[x, y], ...]} in metres, clockwise, y up, one point every ~10 m, and
"pixels": where each point sits in the image (for naming corners).
The track is the coloured racing line of F1-style maps (else the largest blob of pixels
that differ from the background colour); the
infield is the largest background region it encloses. The centreline is where the
distance to the infield equals the distance to the outside.
"""
import json
import math
import sys

import numpy as np
from PIL import Image
from scipy import ndimage
from skimage import measure


def trace(path, length, flip=False, rotate=0.0, thresh=None, start=None, cut_labels=False):
    im = Image.open(path)
    scale = 1.0
    if max(im.size) > 2000:  # big maps: work at ~2000 px, plenty for a centreline
        scale = 2000 / max(im.size)
        im = im.resize((round(im.size[0] * scale), round(im.size[1] * scale)), Image.LANCZOS)
    if start is not None:
        start = (start[0] * scale, start[1] * scale)
    img = np.asarray(im.convert("RGB")).astype(float)
    hsv = np.asarray(im.convert("RGB").convert("HSV")).astype(float)
    hue = hsv[:, :, 0] * 360 / 255
    vivid = (hsv[:, :, 1] > 120) & (hsv[:, :, 2] > 120)
    # F1-style maps draw the racing line in sector colours (red, yellow, blue) on a dark
    # outline; green (DRS) and magenta (speed trap) are labels. Trace that line if present.
    line = vivid & ((hue < 20) | (hue > 340) | ((hue > 35) & (hue < 70)) | ((hue > 170) & (hue < 230)))
    if True:
        if im.mode == "RGBA":
            ink = np.asarray(im)[:, :, 3] > 128
        else:
            border = np.concatenate([img[0], img[-1], img[:, 0], img[:, -1]])
            bg = np.median(border, axis=0)
            ink = np.linalg.norm(img - bg, axis=2) > (thresh or 120)  # skips faint watermarks
        # drop thin lines (label leaders, DRS markers) so only the thick track outline is left
        r = max(2, int(min(ink.shape) / 130))
        yy, xx = np.mgrid[-r:r + 1, -r:r + 1]
        ink = ndimage.binary_opening(ink, structure=(xx * xx + yy * yy) <= r * r)
        if cut_labels and line.sum() > 2000:
            # corner-number circles (dark discs with white digits) stick to the outline and
            # bend the centreline: cut them out, then put back a band around the racing line
            digits = ink & (img.min(axis=2) > 200)
            ink &= ~ndimage.binary_dilation(digits, iterations=3 * r)
            ink |= ndimage.binary_dilation(line, iterations=2 * r)
        if start is not None:  # a chequered start line can cut the outline: paint over it
            gy, gx = np.ogrid[:ink.shape[0], :ink.shape[1]]
            ink |= (gx - start[0]) ** 2 + (gy - start[1]) ** 2 <= (3 * r) ** 2
    lab, n = ndimage.label(ink)
    sizes = ndimage.sum(ink, lab, range(1, n + 1))
    track = lab == (1 + int(np.argmax(sizes)))
    holes, m = ndimage.label(~track)
    edge = set(np.unique(np.concatenate([holes[0], holes[-1], holes[:, 0], holes[:, -1]])))
    counts = np.bincount(holes.ravel())
    inner = [(int(counts[k]), k) for k in range(1, m + 1) if k not in edge]
    infield = holes == max(inner)[1]
    outside = np.isin(holes, list(edge))
    f = ndimage.distance_transform_edt(~outside) - ndimage.distance_transform_edt(~infield)
    cs = measure.find_contours(f, 0.0)
    c = max(cs, key=len)  # (row, col)
    if line.sum() > 2000:
        # snap the rough centreline onto the coloured racing line where there is one nearby
        dist, (iy, ix) = ndimage.distance_transform_edt(~line, return_indices=True)
        half = 1.7 * np.median(ndimage.distance_transform_edt(track)[track & line])
        r, q = np.clip(c[:, 0].round().astype(int), 0, line.shape[0] - 1), np.clip(c[:, 1].round().astype(int), 0, line.shape[1] - 1)
        near = dist[r, q] < half
        c = c.copy()
        c[near, 0], c[near, 1] = iy[r[near], q[near]], ix[r[near], q[near]]
    # cut small loops (label circles stuck to the outline make the contour curl)
    keep, i = [], 0
    while i < len(c):
        keep.append(c[i])
        ahead = c[i + 4:i + 120]
        if len(ahead):
            d = np.hypot(ahead[:, 0] - c[i, 0], ahead[:, 1] - c[i, 1])
            j = np.nonzero(d < 3)[0]
            if len(j):
                i += 4 + j[-1] + 1
                continue
        i += 1
    c = np.array(keep)
    if start is not None:  # begin at the image point nearest the start line
        k = int(np.argmin((c[:, 1] - start[0]) ** 2 + (c[:, 0] - start[1]) ** 2))
        c = np.vstack([c[k:], c[:k]])
    px = np.stack([c[:, 1], c[:, 0]], axis=1) / scale  # image pixels, for labelling corners
    pts = np.stack([c[:, 1], -c[:, 0]], axis=1)  # x right, y up
    if flip:
        pts[:, 0] *= -1
    if rotate:
        a = math.radians(rotate)
        R = np.array([[math.cos(a), -math.sin(a)], [math.sin(a), math.cos(a)]])
        pts = pts @ R.T
    # clockwise
    area = 0.5 * np.sum(pts[:, 0] * np.roll(pts[:, 1], -1) - np.roll(pts[:, 0], -1) * pts[:, 1])
    if area > 0:
        pts = np.vstack([pts[:1], pts[1:][::-1]])
        px = np.vstack([px[:1], px[1:][::-1]])
    # smooth out pixel steps, then scale and resample every ~10 m
    for _ in range(12):
        pts = (np.roll(pts, 1, 0) + 2 * pts + np.roll(pts, -1, 0)) / 4
    seg = np.linalg.norm(np.diff(np.vstack([pts, pts[:1]]), axis=0), axis=1)
    pts *= length / seg.sum()
    s = np.concatenate([[0], np.cumsum(seg * length / seg.sum())])
    loop = np.vstack([pts, pts[:1]])
    n = int(length / 10)
    t = np.linspace(0, length, n, endpoint=False)
    out = np.stack([np.interp(t, s, loop[:, 0]), np.interp(t, s, loop[:, 1])], axis=1)
    out -= out[0]
    pix = np.stack([np.interp(t, s, np.append(px[:, 0], px[0, 0])), np.interp(t, s, np.append(px[:, 1], px[0, 1]))], axis=1)
    return out, pix


def main():
    a = sys.argv[1:]
    length = float(a[a.index("--length") + 1])
    rot = float(a[a.index("--rotate") + 1]) if "--rotate" in a else 0.0
    st = [float(v) for v in a[a.index("--start") + 1].split(",")] if "--start" in a else None
    pts, pix = trace(a[0], length, "--flip" in a, rot, start=st, cut_labels="--cut-labels" in a)
    json.dump({"points": [[round(x, 1), round(y, 1)] for x, y in pts],
               "pixels": [[round(x), round(y)] for x, y in pix]}, open(a[1], "w"))
    print(f"{len(pts)} points")


if __name__ == "__main__":
    main()
