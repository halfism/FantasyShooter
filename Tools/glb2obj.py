# -*- coding: utf-8 -*-
"""把 poly.pizza 的 .glb 转成工程能直接导入的 OBJ。

为什么不走 UE 的 glTF 导入器：
  - Interchange 导 glb 会连带建材质/贴图资产，而本工程在 -nullrhi commandlet
    下贴图这条路是死的（见 MEMORY：GPU 侧永远 32x32、采样全黑）。
  - 我们只要几何。自己解 glb 反而更可控：能精确控制 Z-up、厘米、枢轴位置。

转换规则与 kenney 那条链路**完全一致**，保证两批资产在世界里行为相同：
  - Y-up 米制 -> Z-up 厘米： (x, y, z) -> (x*100, -z*100, y*100)
  - 保留 mtllib/usemtl（Interchange 靠它建材质槽），去掉 g/o 行
  额外一步（kenney 的 OBJ 天然满足，glb 不满足）：
  - 枢轴归到底面中心，否则 AddProp 按"枢轴在底面"落地会把模型埋进地里。

用法:
    python Tools/glb2obj.py
去读 ArtSource/Downloaded/poly/manifest.json，产出 ArtSource/Converted/poly/*.obj
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'ArtSource', 'Downloaded', 'poly')
DST = os.path.join(ROOT, 'ArtSource', 'Converted', 'poly')
LOG = os.path.join(ROOT, 'Saved', 'glb2obj.log')
SEL = os.path.join(ROOT, 'Tools', 'poly_select.txt')
SCALE = 100.0          # 米 -> 厘米

CT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT2': 4, 'MAT3': 9, 'MAT4': 16}


def log(msg):
    line = '[FSGLB] %s' % msg
    print(line, flush=True)
    with open(LOG, 'a', encoding='utf-8') as f:
        f.write(line + '\n')


# ------------------------------------------------------------------ 矩阵
def m_ident():
    return [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def m_mul(a, b):
    r = [0.0] * 16
    for i in range(4):
        ai = i * 4
        for j in range(4):
            r[ai + j] = (a[ai] * b[j] + a[ai + 1] * b[4 + j]
                         + a[ai + 2] * b[8 + j] + a[ai + 3] * b[12 + j])
    return r


def m_from_trs(t, q, s):
    x, y, z, w = q
    xx, yy, zz = x * x, y * y, z * z
    xy, xz, yz = x * y, x * z, y * z
    wx, wy, wz = w * x, w * y, w * z
    return [
        (1 - 2 * (yy + zz)) * s[0], 2 * (xy - wz) * s[0], 2 * (xz + wy) * s[0], t[0],
        2 * (xy + wz) * s[1], (1 - 2 * (xx + zz)) * s[1], 2 * (yz - wx) * s[1], t[1],
        2 * (xz - wy) * s[2], 2 * (yz + wx) * s[2], (1 - 2 * (xx + yy)) * s[2], t[2],
        0.0, 0.0, 0.0, 1.0,
    ]


def m_node(n):
    if 'matrix' in n:                      # glTF 的 matrix 是列主序，转成行主序
        c = n['matrix']
        return [c[0], c[4], c[8], c[12],
                c[1], c[5], c[9], c[13],
                c[2], c[6], c[10], c[14],
                c[3], c[7], c[11], c[15]]
    return m_from_trs(n.get('translation', [0, 0, 0]),
                      n.get('rotation', [0, 0, 0, 1]),
                      n.get('scale', [1, 1, 1]))


def m_apply(m, p):
    x, y, z = p
    return (m[0] * x + m[1] * y + m[2] * z + m[3],
            m[4] * x + m[5] * y + m[6] * z + m[7],
            m[8] * x + m[9] * y + m[10] * z + m[11])


def m_apply_dir(m, p):
    x, y, z = p
    return (m[0] * x + m[1] * y + m[2] * z,
            m[4] * x + m[5] * y + m[6] * z,
            m[8] * x + m[9] * y + m[10] * z)


def recalc_normals(verts, tris):
    """按面法线累积算顶点法线。

    只在 glTF 缺 NORMAL（或某些 primitive 缺、导致长度对不上）时用。
    Unlit 材质不参与光照，法线本身不影响画面，但 OBJ 里 `f v/vt/vn` 三者
    必须齐全，否则又是一种 Interchange 解析警告。
    """
    acc = [[0.0, 0.0, 0.0] for _ in verts]
    for t in tris:
        a, b, c = t
        p0, p1, p2 = verts[a], verts[b], verts[c]
        ux, uy, uz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
        vx, vy, vz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        for i in (a, b, c):
            acc[i][0] += nx
            acc[i][1] += ny
            acc[i][2] += nz
    out = []
    for v in acc:
        ln = (v[0] ** 2 + v[1] ** 2 + v[2] ** 2) ** 0.5
        out.append((v[0] / ln, v[1] / ln, v[2] / ln) if ln > 1e-9 else (0.0, 0.0, 1.0))
    return out


# ------------------------------------------------------------------ glb 解析
def parse_glb(path):
    d = open(path, 'rb').read()
    if d[:4] != b'glTF':
        raise ValueError('not a glb')
    off, js, bin_ = 12, None, b''
    while off + 8 <= len(d):
        clen, ctype = struct.unpack_from('<I4s', d, off)
        body = d[off + 8:off + 8 + clen]
        if ctype == b'JSON':
            js = json.loads(body)
        elif ctype.startswith(b'BIN'):
            bin_ = body
        off += 8 + clen + ((4 - clen % 4) % 4 if clen % 4 else 0)
    if js is None:
        raise ValueError('no JSON chunk')
    return js, bin_


def read_acc(j, bin_, idx):
    acc = j['accessors'][idx]
    n = NC.get(acc['type'], 1)
    fmt, sz = CT[acc['componentType']]
    count = acc['count']
    if 'bufferView' not in acc:
        return [tuple([0.0] * n)] * count
    bv = j['bufferViews'][acc['bufferView']]
    base = bv.get('byteOffset', 0) + acc.get('byteOffset', 0)
    stride = bv.get('byteStride') or (n * sz)
    out = []
    for i in range(count):
        v = struct.unpack_from('<' + fmt * n, bin_, base + i * stride)
        out.append(v if n > 1 else v[0])
    return out


def tri_indices(j, bin_, prim, vcount):
    """返回三角形索引列表；顺带处理 strip/fan。"""
    if 'indices' in prim:
        idx = read_acc(j, bin_, prim['indices'])
    else:
        idx = list(range(vcount))
    mode = prim.get('mode', 4)
    if mode == 4:
        return [idx[i:i + 3] for i in range(0, len(idx) - 2, 3)]
    if mode == 5:      # TRIANGLE_STRIP
        out = []
        for i in range(len(idx) - 2):
            a, b, c = idx[i], idx[i + 1], idx[i + 2]
            out.append([a, b, c] if i % 2 == 0 else [b, a, c])
        return out
    if mode == 6:      # TRIANGLE_FAN
        return [[idx[0], idx[i], idx[i + 1]] for i in range(1, len(idx) - 1)]
    return []


def convert(path, out_path, mtl_name='art.mtl'):
    j, bin_ = parse_glb(path)
    if j.get('extensionsRequired'):
        raise ValueError('uses required extensions %s' % j['extensionsRequired'])

    nodes = j.get('nodes', [])
    # 根节点：scene 指定的，否则取"没被任何 node 当 child 的"
    scenes = j.get('scenes') or []
    if scenes:
        roots = scenes[j.get('scene', 0)].get('nodes', [])
    else:
        kids = set()
        for n in nodes:
            kids.update(n.get('children', []))
        roots = [i for i in range(len(nodes)) if i not in kids]

    verts, norms, tris = [], [], []      # 全局累积

    def walk(ni, parent):
        n = nodes[ni]
        world = m_mul(parent, m_node(n))
        if 'mesh' in n:
            mesh = j['meshes'][n['mesh']]
            for prim in mesh.get('primitives', []):
                attrs = prim.get('attributes', {})
                if 'POSITION' not in attrs:
                    continue
                pos = read_acc(j, bin_, attrs['POSITION'])
                nor = read_acc(j, bin_, attrs['NORMAL']) if 'NORMAL' in attrs else None
                base = len(verts)
                for p in pos:
                    verts.append(m_apply(world, p))
                # 法线：Unlit 材质不参与光照，做线性变换 + 归一化足够；
                # 不做逆转置是为了少一份代码，代价在非均匀缩放下看不出来。
                if nor:
                    for v in nor:
                        d = m_apply_dir(world, v)
                        ln = (d[0] ** 2 + d[1] ** 2 + d[2] ** 2) ** 0.5 or 1.0
                        norms.append((d[0] / ln, d[1] / ln, d[2] / ln))
                for t in tri_indices(j, bin_, prim, len(pos)):
                    tris.append([base + t[0], base + t[1], base + t[2]])
        for c in n.get('children', []):
            walk(c, world)

    for r in roots:
        walk(r, m_ident())

    if not tris:
        raise ValueError('no triangles')

    # ---- 枢轴归底面中心 + Y-up -> Z-up + 米 -> 厘米
    # 注意"底面"是 **min(y)** 而不是 min(z)：glTF 规范是 Y-up，高度沿 Y。
    # 一开始写成 min(z) 时，因为多数模型的原点在中段，转出来 Z 会是 ±87 对称 ——
    # 放进 UE 就是一半埋进地里（AddProp 假定枢轴在底面）。
    # 同时水平中心要取 x 与 z 的中心，别把 x 的值拿去当 z 用。
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    cx = (min(xs) + max(xs)) * 0.5      # 原 x 中心
    cz = (min(zs) + max(zs)) * 0.5      # 原 z 中心（Y-up 下的水平第二轴）
    y0 = min(ys)                        # 原 y 底面 = 高度方向的底
    out_v = []
    for (x, y, z) in verts:
        x, y, z = x - cx, y - y0, z - cz
        out_v.append((x * SCALE, -z * SCALE, y * SCALE))

    out_n = [((d[0]), (-d[2]), (d[1])) for d in norms] if norms else []
    if len(out_n) != len(out_v):
        out_n = recalc_normals(out_v, tris)

    # UV：统一给一份 XY 平面投影。
    # **必须给 vt** —— InterchangeOBJTranslator 在 OBJ 没有 UV 时会
    # ensure(UVs.IsValidIndex(VertexData.UVIndex))，导入虽然还能完成，但每次
    # 都刷一整屏错误栈，冒烟测试里会被当成故障。glTF 自带的 TEXCOORD_0 对本工程
    # 没用（Unlit 纯色，不采样贴图），所以不去解析它，直接投影一份。
    mnx = min(v[0] for v in out_v)
    mxx = max(v[0] for v in out_v)
    mny = min(v[1] for v in out_v)
    mxy = max(v[1] for v in out_v)
    dx = (mxx - mnx) or 1.0
    dy = (mxy - mny) or 1.0
    out_uv = [((v[0] - mnx) / dx, (v[1] - mny) / dy) for v in out_v]

    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('mtllib %s\n' % mtl_name)
        f.write('usemtl colormap\n')
        # 精度：厘米制下 %.2f = 0.1mm，对 1480 臂长的相机绰绰有余。
        # 用 %.6f（微米）会让这批 OBJ 从 6.8MB 涨到十来 MB，纯属浪费 ——
        # glTF 原始顶点本来就是展开的（每面独立顶点），行数已经很可观。
        for v in out_v:
            f.write('v %.2f %.2f %.2f\n' % v)
        for d in out_n:
            ln = (d[0] ** 2 + d[1] ** 2 + d[2] ** 2) ** 0.5 or 1.0
            f.write('vn %.4f %.4f %.4f\n' % (d[0] / ln, d[1] / ln, d[2] / ln))
        for uv in out_uv:
            f.write('vt %.4f %.4f\n' % uv)
        for t in tris:
            a, b, c = t[0] + 1, t[1] + 1, t[2] + 1
            f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (a, a, a, b, b, b, c, c, c))

    hx = (max(v[0] for v in out_v) - min(v[0] for v in out_v))
    hy = (max(v[1] for v in out_v) - min(v[1] for v in out_v))
    hz = (max(v[2] for v in out_v) - min(v[2] for v in out_v))
    return len(out_v), len(tris), (hx, hy, hz)


def load_select():
    """读 Tools/poly_select.txt 白名单。文件不存在 = 全转（用于首次生成索引表）。

    白名单存在时只转名单内的模型，并删掉目录里多余的 OBJ —— 因为
    ArtSource/Converted/poly/*.obj 是要进版本库的，173 个全收会白占十几 MB，
    真正用到的只有几十个。
    """
    if not os.path.isfile(SEL):
        return None
    out = set()
    with open(SEL, encoding='utf-8') as f:
        for line in f:
            line = line.split('#')[0].strip()
            if line:
                out.add(line)
    return out


def main():
    os.makedirs(DST, exist_ok=True)
    man = os.path.join(SRC, 'manifest.json')
    if not os.path.isfile(man):
        log('manifest missing: %s' % man)
        print('GLB2OBJ_EXIT=1')
        return
    rows = json.load(open(man, encoding='utf-8'))

    sel = load_select()
    all_rows = rows
    if sel is not None:
        rows = [r for r in rows if r.get('name') in sel]
        missing = sel - set(r.get('name') for r in rows)
        if missing:
            log('select names not found: %s' % ', '.join(sorted(missing)))
        log('select mode: %d / %d' % (len(rows), len(all_rows)))

    ok, fail = 0, 0
    idx = []
    for r in rows:
        name = r.get('name')
        src = os.path.join(SRC, name + '.glb')
        if not name or not os.path.isfile(src):
            fail += 1
            continue
        dst = os.path.join(DST, name + '.obj')
        try:
            nv, nt, (hx, hy, hz) = convert(src, dst)
            ok += 1
            idx.append((name, nv, nt, hx, hy, hz, r.get('license', ''), r.get('title', '')))
            log('%-28s v=%-6d f=%-6d size=%.0fx%.0fx%.0f %s' % (name, nv, nt, hx, hy, hz, r.get('license')))
        except Exception as e:
            fail += 1
            log('FAIL %-24s %s' % (name, e))

    # 索引表：按面数排序，方便一眼看出哪些是"单件道具"、哪些是"整片组合体"
    idx.sort(key=lambda x: x[2])
    with open(os.path.join(ROOT, 'Saved', 'poly_index.txt'), 'w', encoding='utf-8') as f:
        f.write('%-30s %6s %6s %-20s %-10s %s\n' % ('name', 'verts', 'tris', 'size(cm)', 'license', 'title'))
        for (n, nv, nt, hx, hy, hz, lic, ti) in idx:
            f.write('%-30s %6d %6d %-20s %-10s %s\n' % (
                n, nv, nt, '%.0fx%.0fx%.0f' % (hx, hy, hz), lic, ti))

    # 白名单模式下清掉目录里多余的 OBJ（首次全转会留下 173 个，最后只留选中的）
    if sel is not None:
        keep = set(r.get('name') for r in rows)
        for fn in os.listdir(DST):
            if fn.lower().endswith('.obj') and fn[:-4] not in keep:
                os.remove(os.path.join(DST, fn))
                log('removed stale obj %s' % fn)

    # 写一个共享 mtl，与 kenney 那批保持同一套约定
    with open(os.path.join(DST, 'art.mtl'), 'w', encoding='utf-8') as f:
        f.write('newmtl colormap\nKa 0 0 0\nKd 0.5 0.5 0.5\nKs 0 0 0\nd 1\nillum 0\n\n')

    # 署名：poly.pizza 上混着 CC0 与 CC-BY，后者要求保留作者与来源。
    # 生成一份可直接提交的 CREDITS.md，比把授权散落在 manifest 里靠谱。
    with open(os.path.join(DST, 'CREDITS.md'), 'w', encoding='utf-8') as f:
        f.write('# 第三方模型署名（poly.pizza）\n\n')
        f.write('本目录的 `*.obj` 由 `Tools/glb2obj.py` 从 poly.pizza 的 `.glb` 转出，'
                '只保留几何（Y-up 米制 → Z-up 厘米）。\n')
        f.write('原始 `.glb` 不入库（体积大），可用 `Tools/fetch_poly.py` 重新下载。\n\n')
        f.write('| 名称 | 原始标题 | 授权 | 作者 | 来源 |\n')
        f.write('| --- | --- | --- | --- | --- |\n')
        for r in sorted(rows, key=lambda x: (x.get('name') or '')):
            if not r.get('name'):
                continue
            f.write('| `%s` | %s | %s | %s | https://poly.pizza/m/%s |\n' % (
                r['name'], r.get('title', ''), r.get('license', ''), r.get('author', ''), r['id']))
        f.write('\nCC0 的条目无需署名，列在此处只为可追溯；CC-BY 的条目必须保留上表。\n')

    log('SUMMARY ok=%d fail=%d' % (ok, fail))
    print('GLB2OBJ_EXIT=0')


if __name__ == '__main__':
    main()
