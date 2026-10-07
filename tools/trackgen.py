#!/usr/bin/env python3
"""Builds tracks/*.trk from corner lists.

Each layout is a closed polygon of corners in race order (clockwise, y up):
    (x, y, radius, width)   # comment naming the real corner it is inspired by
Every corner is rounded with an arc of that radius, joined by straights, then the
whole lap is scaled to the target length. `start` is a point on the main straight.
Run it from the repo root:

    python3 tools/trackgen.py              # writes tracks/<key>.trk
    python3 tools/trackgen.py --plot DIR   # also draws a 2D map of each track
"""
import math
import sys

# Inspired by the real circuits (their signature corners and straights), not copies.
TRACKS = {
    "monza": dict(
    name="Autodromo Monzetta", length=5700, width=14, pitspeed=22,
    about="Temple of speed: long straights broken by chicanes, Curva Grande, the Lesmos, Ascari and the Parabolica.",
    start=(0,0), runoff=9, verts=[
        (0,1100,15,15),(40,1112,15,13),            # Rettifilo
        (150,1800,420,14),                         # Curva Grande
        (820,1440,20,13),(870,1450,20,13),         # Roggia
        (1180,1430,55,13),                         # Lesmo 1
        (1190,1200,50,13),                         # Lesmo 2
        (1000,560,800,13),                         # Serraglio
        (920,170,60,13),(925,40,50,13),(840,-40,60,13),  # Ascari
        (500,-860,110,15),(0,-1060,250,15),        # Parabolica
    ], names=['Rettifilo', '', 'Curva Grande', 'Roggia', '', 'Lesmo 1', 'Lesmo 2', 'Serraglio', 'Ascari', '', '', 'Parabolica', '']),
    "spa": dict(
    name="Ardennes Ring", length=6900, width=13, pitspeed=22,
    about="Long and fast: La Source hairpin, the Eau Rouge-Raidillon kink, Kemmel straight, Pouhon and Blanchimont.",
    start=(0,-300), runoff=9, verts=[
        (0,0,20,15),(80,0,20,15),                  # La Source
        (90,-420,70,13),(170,-480,80,13),(260,-490,200,13),  # Eau Rouge, Raidillon
        (1250,-1250,35,13),(1280,-1320,35,13),     # Les Combes
        (1380,-1500,60,12),                        # Malmedy
        (1220,-1790,22,13),(1170,-1750,22,13),     # Rivage
        (980,-1640,80,12),(780,-1920,110,13),      # Pouhon
        (560,-1960,50,12),(490,-2020,60,12),       # Fagnes
        (260,-2030,70,13),                         # Stavelot
        (-80,-1500,300,12),(-100,-900,500,12),     # Blanchimont
        (-60,-620,18,13),(0,-605,18,13),           # Bus Stop
    ], names=['La Source', '', 'Eau Rouge', 'Raidillon', '', 'Les Combes', '', 'Malmedy', 'Rivage', '', 'Pouhon', '', 'Fagnes', '', 'Stavelot', 'Blanchimont', '', 'Bus Stop', '']),
    "silverstone": dict(
    name="Silverfield", length=5800, width=15, pitspeed=22,
    about="Fast and flowing: Abbey, the Loop, Luffield, Copse and the Maggotts-Becketts-Chapel esses into Hangar straight.",
    start=(0,60), runoff=10, verts=[
        (0,420,160,15),(180,620,250,15),           # Abbey, Farm
        (330,700,45,15),(440,640,25,15),           # Village, Loop
        (410,820,60,15),                           # Aintree
        (430,1300,50,15),(250,1360,45,16),(260,1500,60,16),  # Brooklands, Luffield
        (1100,1470,110,16),                        # Copse
        (1180,1330,150,15),(1300,1260,60,15),(1310,1140,60,15),(1430,1030,60,15),  # Maggotts-Becketts-Chapel
        (820,220,90,16),                           # Stowe
        (470,120,30,15),(330,60,45,15),            # Vale, Club
        (0,-400,70,15),
    ], names=['Abbey', 'Farm', 'Village', 'Loop', 'Aintree', 'Brooklands, Luffield', '', '', 'Copse', 'Maggotts-Becketts-Chapel', '', '', '', 'Stowe', 'Vale', 'Club', '']),
    "hungaroring": dict(
    name="Magyar Park", length=4380, width=12, pitspeed=22,
    about="Tight and twisty, hard to pass on: a long run to the turn 1 hairpin, then corner after corner.",
    start=(0,200), runoff=8, verts=[
        (0,640,30,14),(70,640,30,14),              # T1
        (150,380,45,12),(240,350,55,12),           # T2, T3
        (330,-40,80,12),                           # T4
        (650,-60,40,12),                           # T5
        (690,-250,20,11),(650,-300,20,11),         # T6-T7
        (610,-420,50,11),(520,-480,40,11),         # T8, T9
        (500,-570,60,12),(420,-680,90,12),         # T10, T11
        (200,-720,40,12),(110,-600,35,12),         # T12, T13
        (0,-420,60,13),                            # T14
    ], names=['T1', '', 'T2', 'T3', 'T4', 'T5', 'T6-T7', '', 'T8', 'T9', 'T10', 'T11', 'T12', 'T13', 'T14']),
    "brands": dict(
    name="Brands Lane", length=3900, width=11, pitspeed=20,
    about="Old-school and narrow: Paddock Hill Bend, the Druids hairpin, a blast through the woods to Hawthorn and back via Clearways.",
    start=(130,0), runoff=7, verts=[
        (420,0,100,12),                            # Paddock Hill
        (570,-70,22,11),(540,-140,22,11),          # Druids
        (450,-140,50,11),                          # Graham Hill
        (80,-180,70,11),                           # Surtees
        (150,-500,300,11),                         # Pilgrim's Drop
        (260,-900,80,11),                          # Hawthorn
        (100,-1050,70,10),                         # Westfield
        (-20,-960,100,10),(-60,-900,60,10),        # Dingle Dell, Sheene
        (-80,-560,40,10),                          # Stirlings
        (-200,-450,50,11),                         # Clearways
        (-140,0,120,12),                           # Clark
    ], names=['Paddock Hill', 'Druids', '', 'Graham Hill', 'Surtees', "Pilgrim's Drop", 'Hawthorn', 'Westfield', 'Dingle Dell', 'Sheene', 'Stirlings', 'Clearways', 'Clark']),
    "zandvoort": dict(
    name="Dunes of Zandhoek", length=4260, width=11, pitspeed=20,
    about="Narrow and compact among the dunes: the Tarzan hairpin, Hugenholtz, Scheivlak and the long banked final right.",
    start=(150,0), runoff=7, verts=[
        (480,0,30,13),(480,-70,30,13),             # Tarzan
        (320,-90,90,11),                           # Gerlach
        (200,-170,25,12),(240,-240,25,12),         # Hugenholtz
        (450,-340,60,11),(600,-380,80,11),         # Hunserug, Slotemaker
        (760,-560,90,11),                          # Scheivlak
        (700,-800,40,11),                          # Mastersbocht
        (560,-820,50,11),(480,-780,40,11),         # T9-T10
        (250,-760,60,11),                          # T11
        (120,-700,25,11),(80,-660,25,11),          # Hans Ernst
        (-120,-560,90,12),(-140,0,150,12),         # Arie Luyendyk
    ], names=['Tarzan', '', 'Gerlach', 'Hugenholtz', '', 'Hunserug', 'Slotemaker', 'Scheivlak', 'Mastersbocht', 'T9-T10', '', 'T11', 'Hans Ernst', '', 'Arie Luyendyk', '']),
    "sepang": dict(
    name="Kuala Speedway", length=5500, width=17, pitspeed=22,
    about="Wide and fast: the turn 1 hairpin complex, sweeping esses, and two long straights joined by a hairpin.",
    start=(200,0), runoff=10, verts=[
        (950,-120,28,18),(960,-190,28,18),         # T1
        (880,-260,45,17),                          # T2
        (900,-620,140,16),                         # T3
        (600,-700,35,16),                          # T4
        (420,-640,120,16),(250,-720,120,16),       # T5-T6
        (-150,-700,90,16),(-260,-400,90,16),       # T7-T8
        (-280,300,25,16),                          # T9
        (100,420,90,16),(350,520,70,16),(600,480,120,16),  # T10-T13
        (1000,330,35,17),(1000,260,35,17),         # T14
        (-40,60,25,18),(-40,0,25,18),              # T15
    ], names=['T1', '', 'T2', 'T3', 'T4', 'T5-T6', '', 'T7-T8', '', 'T9', 'T10-T13', '', '', 'T14', '', 'T15', '']),
}


def path(verts, start, step=2.0):
    """Points (x, y, width, in_corner) round the lap, beginning nearest `start`."""
    n = len(verts)
    info = []
    for i, (x, y, r, w) in enumerate(verts):
        px, py = verts[i - 1][:2]
        nx, ny = verts[(i + 1) % n][:2]
        a1 = math.atan2(y - py, x - px)
        a2 = math.atan2(ny - y, nx - x)
        d = (a2 - a1 + math.pi) % (2 * math.pi) - math.pi
        info.append((a1, d, r * math.tan(abs(d) / 2)))
    pts = []
    for i, (x, y, r, w) in enumerate(verts):
        a1, d, t = info[i]
        px, py, _, pw = verts[i - 1]
        tp = info[i - 1][2]
        L = math.hypot(x - px, y - py)
        if tp + t > L + 1e-6:
            raise ValueError(f"corners {i - 1} and {i} are too close for their radii ({tp:.0f} + {t:.0f} > {L:.0f} m)")
        sx, sy = px + math.cos(a1) * tp, py + math.sin(a1) * tp
        ex, ey = x - math.cos(a1) * t, y - math.sin(a1) * t
        m = max(1, int((L - tp - t) / step))
        for k in range(m):
            f = k / m
            pts.append((sx + (ex - sx) * f, sy + (ey - sy) * f, pw + (w - pw) * f, False))
        sgn = 1 if d > 0 else -1
        cx = ex + math.cos(a1 + sgn * math.pi / 2) * r
        cy = ey + math.sin(a1 + sgn * math.pi / 2) * r
        a0 = a1 - sgn * math.pi / 2
        m = max(2, int(r * abs(d) / step))
        for k in range(m):
            a = a0 + d * k / m
            pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r, w, True))
    turn = sum(i[1] for i in info)
    assert abs(abs(math.degrees(turn)) - 360) < 1, "the corners do not add up to one lap"
    j = min(range(len(pts)), key=lambda k: (pts[k][0] - start[0]) ** 2 + (pts[k][1] - start[1]) ** 2)
    return pts[j:] + pts[:j]


def arc_s(pts):
    s = [0.0]
    for a, b in zip(pts, pts[1:]):
        s.append(s[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = s[-1] + math.hypot(pts[0][0] - pts[-1][0], pts[0][1] - pts[-1][1])
    return s, total


def overlaps(pts, margin):
    """(s1, s2, gap) where two far-apart parts of the lap come closer than their widths plus margin."""
    s, total = arc_s(pts)
    P, S, bad = pts[::3], s[::3], []
    for i in range(len(P)):
        for j in range(i + 1, len(P)):
            ds = min(S[j] - S[i], total - (S[j] - S[i]))
            lim = (P[i][2] + P[j][2]) / 2 + margin
            if ds > lim * 3 and math.hypot(P[i][0] - P[j][0], P[i][1] - P[j][1]) < lim:
                bad.append((round(S[i]), round(S[j])))
    return bad


def build(key, t):
    pts = path(t["verts"], t["start"])
    k = t["length"] / arc_s(pts)[1]
    verts = [(x * k, y * k, r * k, w) for x, y, r, w in t["verts"]]
    pts = path(verts, (t["start"][0] * k, t["start"][1] * k))
    bad = overlaps(pts, 2 * t["runoff"] + 4)
    assert not bad, f"{key}: track runs into itself at {bad[:3]}"
    return pts


def write(key, t, pts):
    s, total = arc_s(pts)
    # the straight the start line sits on: from the last corner exit to the first corner entry
    ahead = next(s[i] for i in range(len(pts)) if pts[i][3])
    behind = total - next(s[i] for i in range(len(pts) - 1, 0, -1) if pts[i][3])
    # pit lane on the right (the inside: the tracks run clockwise), around the start line
    entry = -min(behind - 20, 300)
    lane_start = entry + 60
    lane_end = min(ahead - 80, 300)
    exit_s = lane_end + 60
    lines = [
        f"# Raylib Racers: {t['name']}, {total / 1000:.2f} km. {t['about']}",
        "# Generated by tools/trackgen.py (edit the layout there, not here).",
        f"name {t['name']}",
        f"width {t['width']}",
        f"runoff {t['runoff']}",
        f"pit right {entry % total:.0f} {lane_start % total:.0f} {lane_end:.0f} {exit_s:.0f}",
        f"pitspeed {t['pitspeed']}",
    ]
    acc = 24.0
    for a, b in zip([pts[-1]] + pts, pts):
        acc += math.hypot(b[0] - a[0], b[1] - a[1])
        if acc >= (12 if b[3] else 24):  # denser control points in corners
            lines.append(f"p {b[0]:.1f} {b[1]:.1f} {b[2]:g}")
            acc = 0.0
    with open(f"tracks/{key}.trk", "w") as f:
        f.write("\n".join(lines) + "\n")
    return total, (entry % total, lane_start % total, lane_end, exit_s)


def plot(key, t, pts, pit, outdir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.collections import LineCollection
    s, total = arc_s(pts)
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    fig, ax = plt.subplots(figsize=(7, 6), dpi=110)
    fig.patch.set_facecolor("#14161c")
    ax.set_facecolor("#14161c")
    loop = pts + pts[:1]
    segs = [[a[:2], b[:2]] for a, b in zip(loop, loop[1:])]
    # line width proportional to the real track width so narrow and wide parts show
    ax.add_collection(LineCollection(segs, colors="#c8ccd6", linewidths=[(a[2] - 6) * 0.7 for a in loop[1:]],
                                     capstyle="round"))
    def at(v):
        v %= total
        return pts[min(range(len(s)), key=lambda i: abs(s[i] - v))]
    ent, ls, le, ex = pit
    lane = [p for p, sv in zip(pts, s) if sv <= le or sv >= ls]
    lane = sorted(lane, key=lambda p: (s[pts.index(p)] - ls) % total)
    side = [(p[0] + 0, p[1] + 0) for p in lane]
    ax.plot([p[0] for p in side], [p[1] for p in side], color="#ff8030", linewidth=1.0, alpha=0.9, label="pit lane")
    ax.plot(xs[0], ys[0], "s", color="#ff4040", markersize=7)
    ax.annotate("", xy=at(120)[:2], xytext=pts[0][:2], arrowprops=dict(arrowstyle="->", color="#ff4040", lw=1.6))
    # corner names next to their apex, pushed away from the lap centre
    cx, cy = sum(xs) / len(xs), sum(ys) / len(ys)
    span = max(max(xs) - min(xs), max(ys) - min(ys))
    k = t["length"] / t["_rawlen"]
    for (x, y, r, w), name in zip(t["verts"], t["names"]):
        if not name:
            continue
        x, y = x * k, y * k
        dx, dy = x - cx, y - cy
        d = math.hypot(dx, dy) or 1
        ax.text(x + dx / d * span * 0.05, y + dy / d * span * 0.05, name, color="#8fc8ff", fontsize=7,
                ha="center", va="center")
    ax.set_aspect("equal")
    ax.margins(0.08)
    ax.axis("off")
    ax.set_title(f"{t['name']}   {total / 1000:.2f} km, {min(p[2] for p in pts):g}-{max(p[2] for p in pts):g} m wide",
                 color="white", fontsize=11, pad=16)
    x0, y0 = min(xs), min(ys) - span * 0.06
    ax.plot([x0, x0 + 500], [y0, y0], color="white", linewidth=2)
    ax.text(x0 + 250, y0 - span * 0.035, "500 m", color="white", ha="center", fontsize=7)
    fig.savefig(f"{outdir}/{key}.png", bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)


def main():
    outdir = sys.argv[sys.argv.index("--plot") + 1] if "--plot" in sys.argv else None
    keys = [a for a in sys.argv[1:] if a in TRACKS] or list(TRACKS)
    for key in keys:
        t = TRACKS[key]
        t["_rawlen"] = arc_s(path(t["verts"], t["start"]))[1]
        pts = build(key, t)
        total, pit = write(key, t, pts)
        print(f"{key:12s} {total:6.0f} m  pit {pit[0]:.0f}-{pit[3]:.0f}, lane {(pit[2] - pit[1]) % total:.0f} m")
        if outdir:
            plot(key, t, pts, pit, outdir)


if __name__ == "__main__":
    main()
