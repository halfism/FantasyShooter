# -*- coding: utf-8 -*-
"""检查导入后的 StaticMesh 包围盒，判断 OBJ 轴向是否被转换。"""
import os
import unreal

DST = "/Game/Art/Meshes"
OUT = r"G:/Epic/UnrealProjects/FantasyShooter58/Saved/art_bounds.txt"

lines = []
for stem in ["SM_AxisProbe", "SM_BevelBox", "SM_Prism12", "SM_IcoSphere", "SM_Cone8"]:
    path = DST + "/" + stem
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        lines.append("%s MISSING" % stem)
        continue
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    b = mesh.get_bounds()
    e, o = b.box_extent, b.origin
    # 顶点总数：判断三角化有没有把多顶点面拆爆
    lines.append("%-14s extent=(%7.2f,%7.2f,%7.2f) origin=(%7.2f,%7.2f,%7.2f)"
                 % (stem, e.x, e.y, e.z, o.x, o.y, o.z))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
print("\n".join(lines))
