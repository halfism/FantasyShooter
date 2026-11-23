# -*- coding: utf-8 -*-
"""把 ArtSource/Meshes/*.obj 导入为 /Game/Art/Meshes 下的 StaticMesh。
无界面跑：
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<this>" -nullrhi -nosplash -nop4 -stdout
"""
import os
import unreal

SRC = r"G:/Epic/UnrealProjects/FantasyShooter58/ArtSource/Meshes"
DST = "/Game/Art/Meshes"

names = []
tasks = []
for f in sorted(os.listdir(SRC)):
    if not f.lower().endswith(".obj"):
        continue
    stem = os.path.splitext(f)[0]
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, f))
    t.set_editor_property("destination_path", DST)
    t.set_editor_property("destination_name", stem)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    tasks.append(t)
    names.append(stem)

unreal.log("[FSART] importing %d meshes -> %s" % (len(tasks), DST))
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

# 源目录里已删掉的资产（比如重新命名过的部件）同步清掉，避免 Content 里留孤儿
stems = set(names)
for a in unreal.EditorAssetLibrary.list_assets(DST, recursive=False):
    short = a.split("/")[-1].split(".")[0]
    if short not in stems:
        unreal.EditorAssetLibrary.delete_asset(a)
        unreal.log("[FSART] removed stale %s" % short)

ok = 0
for stem in names:
    path = DST + "/" + stem
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.log_error("[FSART] MISSING %s" % stem)
        continue
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    b = mesh.get_bounds()
    e = b.box_extent
    o = b.origin
    unreal.log("[FSART] %-14s extent=(%.2f, %.2f, %.2f) origin=(%.2f, %.2f, %.2f) tris=%d"
               % (stem, e.x, e.y, e.z, o.x, o.y, o.z,
                  mesh.get_num_triangles(0) if hasattr(mesh, "get_num_triangles") else -1))
    ok += 1

unreal.log("[FSART] DONE imported=%d ok=%d" % (len(names), ok))
