"""
Halo trunk-emblem display for the BMW G30 5 Series: housing and back plates.
Run:  python3 halo_housing.py            (needs numpy, scikit-image:  pip install numpy scikit-image)
Writes Halo_88_HOUSING, Halo_88_BACK_PLATE and Halo_88_BACK_PLATE_SPREADER as .stl and .3mf next to this file.
Change any value in P below and run it again to make your own version.

- 88 mm round housing over the 84 mm G30 badge holder. Centred 3" (76.2 mm) x 1.5 mm lens, flush with the rim.
- The display is shifted up by D (about 2.65 mm) under the lens so its cable tab and ribbon loop fit inside 88 mm.
  It loads from the BACK: the board drops into a wider board pocket and the glass stops against a lip at the front.
  The housing top covers the cable tab and ribbon loop (no separate tab cap).
- Back plate drops into the base and rests on the board's brass studs, flush with the base: 14 mm centre hole for
  the wires (lid drilled 16 mm), two 6 mm solid pegs that locate in the factory badge holes, and a shallow dome on the
  paint side that fills the 0.86 mm dip in the badge seat. SPREADER version: 46 mm window + 0.4 mm recess for a
  50 mm aluminium disc, thermal pads from the hot parts on the board down to the disc.
- 45 deg bevel on the outside bottom edge; the tape ring (68-83.6 mm) sits inside the badge holder.
Coordinates: origin = badge centre, +Y = up on the car, z = 0 = base (tape side).
"""
import sys, json, os
import numpy as np
from skimage.measure import marching_cubes

P = dict(R=44.0, disp_d=73.0, disp_clear=0.6, chin_hw=16.76 + 1.0, chin_bottom=76.65 - 36.5, tab_clear=0.5,
         ribbon_hw=13.5, ribbon_out=2.6, ribbon_clear=0.3, ribbon_floor=1.0, wall_min=0.8,
         module_depth=7.54 + 0.05, plate_t=0.6, plate_clear=0.2, plate_ledge=0.5, lead_in=0.4, plate_max_r=41.2, board_grow=1.5, board_top_below_glass=3.2, bevel_r0=42.0,
         lens_d=76.2, lens_clear=0.15, lens_t=1.5, lens_bond=0.0,
         edge_r=1.5, dome=1.0, step_r=42.2, step_h=0.5,
         peg_x=28.0, peg_d=4.0, peg_len=6.0, hole_d=14.0,
         dip=0.86, sp_window_r=23.0, sp_recess_r=25.3, sp_recess_d=0.4, tape_t=0.64, dome_margin=0.3, tape_in_r=34.0, tape_out_r=41.8, holder_r=42.0, voxel=0.2)
R = P["R"]
R_CAV = P["disp_d"] / 2 + P["disp_clear"]
HW = P["chin_hw"] + P["tab_clear"]; BOT = P["chin_bottom"] + P["tab_clear"]
RHW = P["ribbon_hw"] + P["ribbon_clear"]; RBOT = BOT + P["ribbon_out"] + P["ribbon_clear"]
Rw = R - P["wall_min"]
D = max(BOT - np.sqrt(Rw**2 - HW**2), RBOT - np.sqrt(Rw**2 - RHW**2), 0.0)
D = float(np.ceil(D * 20) / 20)
PT = P["plate_t"]
H = PT + P["module_depth"]                      # glass face
R_LP = P["lens_d"] / 2 + P["lens_clear"]
TOP = H + P["lens_bond"] + P["lens_t"]

def grid(zmin, zmax):
    v = P["voxel"]; m = R + 2.0
    xs = np.arange(-m, m + v, v, dtype=np.float32)
    zs = np.arange(zmin - 1.5, zmax + 1.5 + v, v, dtype=np.float32)
    X, Y, Z = np.meshgrid(xs, xs, zs, indexing="ij")
    return X, Y, Z, (xs[0], xs[0], zs[0])

def disc_tab(X, Y, grow=0.0):
    y = Y - D
    disc = np.sqrt(X**2 + y**2) - (R_CAV + grow)
    tab = np.maximum.reduce([np.abs(X) - (HW + grow), -(BOT + grow) - y, y + 30])
    return np.minimum(disc, tab)

def ribbon(X, Y, grow=0.0):
    y = Y - D
    return np.maximum.reduce([np.abs(X) - (RHW + grow), -(RBOT + grow) - y, y + 30])

def plate_outline(X, Y, grow=0.0):
    y = Y - D
    a = np.minimum(np.sqrt(X**2 + y**2) - (R_CAV + P["board_grow"] + grow), disc_tab(X, Y, P["plate_ledge"] + grow))
    return np.maximum(a, np.sqrt(X**2 + Y**2) - (P["plate_max_r"] + grow))

def zslab(Z, z0, z1):
    return np.maximum(z0 - Z, Z - z1)

def housing():
    X, Y, Z, org = grid(0, TOP)
    r = np.sqrt(X**2 + Y**2)
    er = P["edge_r"]
    a, b = (r - R) + er, Z - (TOP - er)
    body = np.sqrt(np.maximum(a, 0)**2 + np.maximum(b, 0)**2) + np.minimum(np.maximum(a, b), 0) - er
    body = np.maximum(body, -Z)
    s = np.clip((r - R_LP) / (R - R_LP), 0, 1)                         # front slopes down toward the paint
    f = np.maximum(body, Z - (TOP - P["dome"] * s * s))
    # 45 deg bevel on the outside bottom edge (prints without a hanging step, clears the 84 mm holder edge)
    f = np.maximum(f, -(Z - (r - P["bevel_r0"])))
    f = np.maximum(f, -np.maximum.reduce([disc_tab(X, Y), zslab(Z, -2, H)]))                  # display + tab
    f = np.maximum(f, -np.maximum.reduce([ribbon(X, Y), zslab(Z, P["ribbon_floor"], H)]))    # ribbon loop
    li = P["lead_in"]                                                                         # 45 deg lead-in at the back entry
    f = np.maximum(f, -np.maximum(disc_tab(X, Y, li - np.clip(Z - PT, 0, li)), zslab(Z, -2, PT + li)))
    f = np.maximum(f, -np.maximum(r - R_LP, zslab(Z, H, TOP + 2)))                           # lens pocket
    ch = (TOP + 0.3) - Z - (r - R_LP)                                                         # 0.3 chamfer at lens edge
    f = np.maximum(f, -np.maximum(-ch, -(Z - (TOP - 0.6))))
    # board pocket: the PCB and its edge connectors are wider than the glass; lets the board go in so the glass reaches the lip
    bp = np.maximum(np.sqrt(X**2 + (Y - D)**2) - (R_CAV + P["board_grow"]), r - (P["plate_max_r"] + P["plate_clear"]))
    zb = H - P["board_top_below_glass"]
    f = np.maximum(f, -np.maximum(bp, zslab(Z, -2, zb)))
    f = np.maximum(f, -np.maximum(bp + (Z - zb), zslab(Z, zb - 0.01, zb + P["board_grow"])))     # 45 deg roof so it prints
    f = np.maximum(f, -np.maximum(plate_outline(X, Y, P["plate_clear"]), zslab(Z, -2, PT)))  # plate pocket
    return f, org

def plate(spreader=False):
    """Print orientation: board side on the bed (z=0), paint side up, dome + pegs above."""
    v = P["voxel"]; m = P["plate_max_r"] + 1.5
    xs = np.arange(-m, m + v, v, dtype=np.float32)
    zs = np.arange(-1.0, PT + P["peg_len"] + 1.5, v, dtype=np.float32)
    X, Y, Z = np.meshgrid(xs, xs, zs, indexing="ij")
    r = np.sqrt(X**2 + Y**2)
    po = plate_outline(X, Y)
    f = np.maximum(po, zslab(Z, 0, PT))
    rd = P["tape_in_r"] - 0.5
    dh = P["tape_t"] + P["dip"] * (1 - (r / P["holder_r"])**2) - P["dome_margin"]
    dome = np.maximum.reduce([r - rd, Z - (PT + dh), -Z, po])
    if spreader:                                       # no dome over the disc; disc carries a soft pad to the paint instead
        dome = np.maximum(dome, -(r - (P["sp_recess_r"] + 1.0)))
    f = np.minimum(f, dome)
    for px in (-P["peg_x"], P["peg_x"]):
        rp = np.sqrt((X - px)**2 + Y**2); tip = PT + P["peg_len"]
        f = np.minimum(f, np.maximum.reduce([rp - (P["peg_d"] / 2 - np.clip(Z - (tip - 0.5), 0, 0.5)), Z - tip, -Z]))
    if spreader:
        f = np.maximum(f, -np.maximum.reduce([r - P["sp_window_r"], -Z - 1, Z - PT - 1]))                 # window: pads reach the disc
        f = np.maximum(f, -np.maximum.reduce([r - P["sp_recess_r"], (PT - P["sp_recess_d"]) - Z, Z - 10]))  # disc recess, paint side
    f = np.maximum(f, -(r - P["hole_d"] / 2))
    return f, (xs[0], xs[0], zs[0])


# ------------------------------------------------------------------ mesh output
def vol_sign_ok(tri, verts, faces):
    t = verts[faces]
    return np.einsum("ij,ij->i", t[:, 0], np.cross(t[:, 1], t[:, 2])).sum() > 0

def write_3mf(verts, faces, path):
    import zipfile, io
    vs = "\n".join(f'<vertex x="{a:.3f}" y="{b:.3f}" z="{c:.3f}"/>' for a, b, c in verts)
    ts = "\n".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in faces)
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             '<resources><object id="1" type="model"><mesh><vertices>' + vs + '</vertices><triangles>' + ts +
             '</triangles></mesh></object></resources><build><item objectid="1"/></build></model>')
    ct = ('<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
          '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
          '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("[Content_Types].xml", ct); z.writestr("_rels/.rels", rels); z.writestr("3D/3dmodel.model", model)

def to_stl(f, org, path, mirror_z_for_print=False, spacing=None):
    v = P["voxel"]
    sp = np.array(spacing if spacing else (v, v, v), dtype=float)
    f = np.pad(f, 1, constant_values=10.0)
    verts, faces, _, _ = marching_cubes(f, 0.0, spacing=tuple(sp))
    verts += np.array(org) - sp
    if mirror_z_for_print:              # flip so the decorative front prints face-down
        verts[:, 2] = -verts[:, 2]; verts[:, 1] = -verts[:, 1]
    verts[:, 2] -= verts[:, 2].min()
    tri = verts[faces]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    # skimage winding -> make normals point outward (check via signed volume)
    vol = np.einsum("ij,ij->i", tri[:, 0], np.cross(tri[:, 1], tri[:, 2])).sum() / 6
    if vol < 0:
        tri = tri[:, ::-1]; n = -n; vol = -vol
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    data = np.zeros(len(tri), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
    data["n"] = n; data["v"] = tri
    write_3mf(verts if vol_sign_ok(tri,verts,faces) else verts, faces if vol_sign_ok(tri,verts,faces) else faces[:, ::-1], path.replace(".stl", ".3mf"))
    with open(path, "wb") as fh:
        fh.write(b"Halo G30 emblem display".ljust(80, b" "))
        fh.write(np.uint32(len(tri)).tobytes()); fh.write(data.tobytes())
    # watertight check: every edge shared by exactly 2 faces
    e = np.sort(np.concatenate([faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]]), axis=1)
    _, cnt = np.unique(e, axis=0, return_counts=True)
    ext = verts.max(0) - verts.min(0)
    return dict(file=path, triangles=len(tri), volume_cm3=round(vol / 1000, 2),
                watertight=bool((cnt == 2).all()), extent_mm=[round(x, 2) for x in ext])


if __name__ == "__main__":
    out = os.path.dirname(os.path.abspath(__file__))
    f, o = housing()
    v = P["voxel"]
    r1 = to_stl(f, o, f"{out}/Halo_88_HOUSING.stl"); del f
    f, o = plate()
    r2 = to_stl(f, o, f"{out}/Halo_88_BACK_PLATE.stl")
    f, o = plate(spreader=True)
    r3 = to_stl(f, o, f"{out}/Halo_88_BACK_PLATE_SPREADER.stl")
    info = dict(housing=r1, plate=r2, spreader=r3, display_shift_up=D, glass_face=H, top=TOP,
                rim_width=round(R - R_LP, 2), glass_edge_under_lip_top=round(R_CAV - 0.3 + D - R_LP, 2),
                dome_peak=round(P["tape_t"] + P["dip"] - P["dome_margin"], 2))
    print(json.dumps(info, indent=1))
