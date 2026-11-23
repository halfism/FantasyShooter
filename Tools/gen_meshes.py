#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成低多边形部件资产（OBJ）。

设计约束（极重要）：
  UE 的 BasicShapes 是 {Cube, Cylinder, Sphere, Cone, Plane}，
  全工程 100+ 处 AddShape 都按它们的尺寸与枢轴摆放。
  所以这里生成的每个网格必须与对应 BasicShape 严格同尺寸、同枢轴，
  否则一次性替换会让所有位置与缩放整体错位。

  Cube     : 100 x 100 x 100，几何中心在原点
  Cylinder : 直径 100，高 100，轴向 +Z，几何中心在原点
  Sphere   : 直径 100，几何中心在原点
  Cone     : 底径 100，高 100，尖端朝 +Z，几何中心在原点（底面 z=-50）

手法：凸多面体 = 一组半空间的交。给一组平面 (法线, 距离)，
逐个平面求"该平面被其余所有平面裁剪出的多边形"，天然得到精确的面。
"""
import math
import os

OUT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "ArtSource", "Meshes")


# ---------------------------------------------------------------- 向量工具
def vadd(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def vsub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def vmul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def vdot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def vnorm(a):
    L = math.sqrt(vdot(a, a))
    return (a[0] / L, a[1] / L, a[2] / L) if L > 1e-12 else (0.0, 0.0, 1.0)


def plane(nx, ny, nz, d):
    """(任意法线, 距离) -> (单位法线, 单位距离)。
    距离必须跟着法线一起除以模长，否则 (1,1,0)@88 会被当成 dot=88，
    也就是 x+y=124 —— 平面跑到立体外面，倒角全被裁掉（这里踩过）。"""
    L = math.sqrt(nx * nx + ny * ny + nz * nz)
    return ((nx / L, ny / L, nz / L), d / L)


# ---------------------------------------------------------------- 核心：半空间裁剪
EPS = 1e-7


def clip_poly(poly, n, d):
    """保留 dot(n,p) <= d 的部分（Sutherland-Hodgman）"""
    if not poly:
        return poly
    out = []
    m = len(poly)
    for i in range(m):
        a = poly[i]
        b = poly[(i + 1) % m]
        da = vdot(n, a) - d
        db = vdot(n, b) - d
        if da <= EPS:
            out.append(a)
        if (da < -EPS and db > EPS) or (da > EPS and db < -EPS):
            t = da / (da - db)
            out.append(tuple(a[k] + (b[k] - a[k]) * t for k in range(3)))
    return out


def dedupe(poly):
    out = []
    for p in poly:
        if not out or math.sqrt(vdot(vsub(p, out[-1]), vsub(p, out[-1]))) > 1e-5:
            out.append(p)
    if len(out) > 2:
        d = math.sqrt(vdot(vsub(out[0], out[-1]), vsub(out[0], out[-1])))
        if d < 1e-5:
            out.pop()
    return out


def plane_basis(n):
    a = (0.0, 0.0, 1.0) if abs(n[2]) < 0.9 else (1.0, 0.0, 0.0)
    u = vnorm(vcross(a, n))
    v = vcross(n, u)
    return u, v


def newell(poly):
    nx = ny = nz = 0.0
    m = len(poly)
    for i in range(m):
        a = poly[i]
        b = poly[(i + 1) % m]
        nx += (a[1] - b[1]) * (a[2] + b[2])
        ny += (a[2] - b[2]) * (a[0] + b[0])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    return (nx, ny, nz)


def faces_from_planes(planes):
    """每个平面 -> 被其他所有平面裁出的凸多边形。返回 [(法线, [顶点...])]"""
    faces = []
    S = 8000.0
    for (n, d) in planes:
        u, v = plane_basis(n)
        p0 = vmul(n, d)
        poly = [vadd(vadd(p0, vmul(u, S)), vmul(v, S)),
                vadd(vadd(p0, vmul(u, -S)), vmul(v, S)),
                vadd(vadd(p0, vmul(u, -S)), vmul(v, -S)),
                vadd(vadd(p0, vmul(u, S)), vmul(v, -S))]
        for (n2, d2) in planes:
            if n2 is n:
                continue
            poly = clip_poly(poly, n2, d2)
            if len(poly) < 3:
                break
        poly = dedupe(poly)
        if len(poly) < 3:
            continue
        nl = newell(poly)
        if vdot(nl, n) < 0:
            poly.reverse()
        faces.append((n, poly))
    return faces


# ---------------------------------------------------------------- 形状定义
def shape_bevel_box(h=50.0, cham=8.0):
    """倒角立方体：6 正方形面 + 12 棱斜面 + 8 角三角 = 26 面。
    推导（踩过两次坑，别再改）：
      棱平面 |x|+|y| <= 2h-c
      角平面 |x|+|y|+|z| <= 3h-2c
    两者必须差 c，取 3h-c 会让角平面跑到顶点外侧、根本不切（8 个角面全丢）；
    取 3h-2c 时角面三角边长 c*sqrt2、面内正方形边长 2(h-c)，是标准 chamfered cube。
    """
    P = []
    for s in (-1, 1):
        P.append(plane(s, 0, 0, h))
        P.append(plane(0, s, 0, h))
        P.append(plane(0, 0, s, h))
    de = 2.0 * h - cham
    for sx in (-1, 1):
        for sy in (-1, 1):
            P.append(plane(sx, sy, 0, de))
            P.append(plane(sx, 0, sy, de))
            P.append(plane(0, sx, sy, de))
    dc = 3.0 * h - 2.0 * cham
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                P.append(plane(sx, sy, sz, dc))
    return faces_from_planes(P)


def shape_prism(sides=24, radius=50.0, half_h=50.0):
    """24 边棱柱（替换 Cylinder）。
    12 边在 100uu 物体上还像低模，但玩家脚下光环半径只有 81uu、
    弦长折算到屏幕约 14px，折线一眼就看得见，明显"廉价"。
    24 边的外接/内切差 0.9%，屏上读作圆，同时保留棱面。"""
    P = [plane(0, 0, 1, half_h), plane(0, 0, -1, half_h)]
    apo = radius * math.cos(math.pi / sides)   # 内切圆（apothem）
    for i in range(sides):
        a = 2.0 * math.pi * i / sides
        P.append(plane(math.cos(a), math.sin(a), 0, apo))
    return faces_from_planes(P)


def shape_cone(sides=8, radius=50.0, half_h=50.0):
    """8 边锥（替换 Cone）：底 z=-50，尖 z=+50。"""
    P = [plane(0, 0, -1, half_h)]
    # 侧面：过底面点 (cR, sR, -h) 与顶点 (0,0,h)
    # c*x + s*y + k*z = D，代入两点 => k = R/(2h)，D = R/2
    k = radius / (2.0 * half_h)
    for i in range(sides):
        a = 2.0 * math.pi * i / sides
        P.append(plane(math.cos(a), math.sin(a), k, radius * 0.5))
    return faces_from_planes(P)


def shape_icosphere(radius=50.0, subdiv=1):
    """icosphere（替换 Sphere）：80 面平法线，低模球。"""
    t = (1.0 + math.sqrt(5.0)) / 2.0
    verts = [
        (-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0),
        (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
        (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1),
    ]
    verts = [vnorm(v) for v in verts]
    tris = [
        (0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
        (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
        (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
        (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1),
    ]
    for _ in range(subdiv):
        cache = {}
        new_tris = []

        def mid(i, j):
            key = (min(i, j), max(i, j))
            if key in cache:
                return cache[key]
            m = vnorm(vmul(vadd(verts[i], verts[j]), 0.5))
            verts.append(m)
            cache[key] = len(verts) - 1
            return cache[key]

        for (a, b, c) in tris:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            new_tris += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        tris = new_tris
    faces = []
    for (a, b, c) in tris:
        pa = vmul(verts[a], radius)
        pb = vmul(verts[b], radius)
        pc = vmul(verts[c], radius)
        n = vnorm(vcross(vsub(pb, pa), vsub(pc, pa)))
        faces.append((n, [pa, pb, pc]))
    return faces


def shape_rock(radius=50.0, subdiv=1):
    """不规则岩块（新增场景资产）。icosphere 顶点做确定性扰动，四边不对称——
    场景里散落的石头再也不是"等比缩放的方块"。"""
    t = (1.0 + math.sqrt(5.0)) / 2.0
    verts = [
        (-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0),
        (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
        (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1),
    ]
    verts = [vnorm(v) for v in verts]
    tris = [
        (0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
        (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
        (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
        (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1),
    ]
    for _ in range(subdiv):
        cache = {}
        new_tris = []

        def mid(i, j):
            key = (min(i, j), max(i, j))
            if key in cache:
                return cache[key]
            verts.append(vnorm(vmul(vadd(verts[i], verts[j]), 0.5)))
            cache[key] = len(verts) - 1
            return cache[key]

        for (a, b, c) in tris:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            new_tris += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        tris = new_tris

    # 确定性扰动：同一份源码每次生成完全一致，不引入随机资产
    out = []
    for v in verts:
        f = (1.0 + 0.26 * math.sin(3.1 * v[0] + 1.7) * math.cos(2.7 * v[1] + 0.4)
                 + 0.12 * math.sin(5.3 * v[2] + 2.2))
        p = (v[0] * f * radius, v[1] * f * radius * 0.86, v[2] * f * radius * 0.74)
        out.append(p)
    faces = []
    for (a, b, c) in tris:
        pa, pb, pc = out[a], out[b], out[c]
        n = vnorm(vcross(vsub(pb, pa), vsub(pc, pa)))
        faces.append((n, [pa, pb, pc]))
    return faces


def shape_shard(sides=6, radius=50.0, hm=12.0, top=50.0, bottom=-50.0):
    """六棱尖晶（新增场景资产）：中段一小截柱身，上下各收成尖。
    替换原来等比缩放的 Cone，场景里的"锥体石头"才有晶体感。
    hm 是柱身半高；给 0 会退化成纯双锥（侧面直接消失）。"""
    apo = radius * math.cos(math.pi / sides)
    P = []
    for i in range(sides):
        a = 2.0 * math.pi * i / sides
        c, s = math.cos(a), math.sin(a)
        P.append(plane(c, s, 0, apo))
        kt = apo / (top - hm)                  # 上锥：过 (apo*c, apo*s, +hm) 与 (0,0,top)
        P.append(plane(c, s, kt, apo * top / (top - hm)))
        kb = apo / (bottom + hm)               # 下锥：过 (apo*c, apo*s, -hm) 与 (0,0,bottom)
        P.append(plane(c, s, kb, apo * bottom / (bottom + hm)))
    return faces_from_planes(P)


def shape_axis_probe():
    """轴向探针：三轴尺寸全不同（60 / 20 / 100 偏置），导入后看 bounds 就知道轴向有没有翻。"""
    return faces_from_planes([
        plane(1, 0, 0, 60), plane(-1, 0, 0, 60),
        plane(0, 1, 0, 20), plane(0, -1, 0, 20),
        plane(0, 0, 1, 150), plane(0, 0, -1, 50),
    ])


# ---------------------------------------------------------------- OBJ 输出
def write_obj(path, faces, name):
    lines = ["# FantasyShooter lowpoly part: %s" % name,
             "# generated by Tools/gen_meshes.py"]
    vi = 1
    body = []
    for (n, poly) in faces:
        u, v = plane_basis(n)
        # 面内 2D 坐标，按包围盒归一到 0..1（给贴图留 UV）
        p2 = [(vdot(p, u), vdot(p, v)) for p in poly]
        minx = min(q[0] for q in p2); maxx = max(q[0] for q in p2)
        miny = min(q[1] for q in p2); maxy = max(q[1] for q in p2)
        sx = (maxx - minx) or 1.0
        sy = (maxy - miny) or 1.0
        idx = []
        for k, p in enumerate(poly):
            lines.append("v %.4f %.4f %.4f" % p)
            lines.append("vt %.4f %.4f" % ((p2[k][0] - minx) / sx, (p2[k][1] - miny) / sy))
            lines.append("vn %.4f %.4f %.4f" % n)
            idx.append(vi)
            vi += 1
        # 扇形三角化，绕序已由 newell 校正过
        for k in range(1, len(idx) - 1):
            body.append("f %d/%d/%d %d/%d/%d %d/%d/%d" % (
                idx[0], idx[0], idx[0],
                idx[k], idx[k], idx[k],
                idx[k + 1], idx[k + 1], idx[k + 1]))
    lines += body
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


def bounds(faces):
    xs = [p[0] for (_, poly) in faces for p in poly]
    ys = [p[1] for (_, poly) in faces for p in poly]
    zs = [p[2] for (_, poly) in faces for p in poly]
    return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    jobs = [
        ("SM_BevelBox", shape_bevel_box()),
        ("SM_Prism24", shape_prism()),
        ("SM_IcoSphere", shape_icosphere()),
        ("SM_Cone8", shape_cone()),
        ("SM_Rock", shape_rock()),
        ("SM_Shard", shape_shard()),
        ("SM_AxisProbe", shape_axis_probe()),
    ]
    for (name, faces) in jobs:
        path = os.path.join(OUT_DIR, name + ".obj")
        write_obj(path, faces, name)
        x0, x1, y0, y1, z0, z1 = bounds(faces)
        tris = sum(len(p) - 2 for (_, p) in faces)
        print("%-14s faces=%3d tris=%4d  x[%7.2f,%7.2f] y[%7.2f,%7.2f] z[%7.2f,%7.2f]"
              % (name, len(faces), tris, x0, x1, y0, y1, z0, z1))


if __name__ == "__main__":
    main()
