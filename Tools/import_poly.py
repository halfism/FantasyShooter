# -*- coding: utf-8 -*-
"""把 ArtSource/Converted/poly/*.obj 导入 UE，并存到一个共享材质上。

与 import_props.py（kenney 那批）是同一套约定：
  - 资产进 /Game/Art/Props/poly/<name>
  - 材质只有一个 M_Art_poly（Unlit + VectorParameter("Tint") -> Emissive），
    C++ 侧 FSUtil::PropColor() 按名字分类上色，同色共享 MID
  - 顺手删掉源目录已经移除的孤儿资产

跑法：
  UnrealEditor-Cmd.exe <abs uproject> -run=pythonscript -script="<abs this>" \
     -nullrhi -nosplash -nop4 -stdout
"""
import os
import unreal

ROOT = r"G:/Epic/UnrealProjects/FantasyShooter58"
SRC = os.path.join(ROOT, "ArtSource", "Converted", "poly")
MESH_DST = "/Game/Art/Props/poly"
MAT_DST = "/Game/Art/Materials"
MAT_NAME = "M_Art_poly"
RPT = os.path.join(ROOT, "Saved", "import_poly.txt")

_lines = []


def log(msg):
    _lines.append(str(msg))
    print("[FSPOLYIMP] %s" % msg, flush=True)


def flush():
    with open(RPT, "w", encoding="utf-8") as f:
        f.write("\n".join(_lines))


def build_material():
    path = MAT_DST + "/" + MAT_NAME
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(MAT_NAME, MAT_DST, unreal.Material, unreal.MaterialFactoryNew())
    if not mat:
        log("ERROR material create failed")
        return None

    mel = unreal.MaterialEditingLibrary
    try:
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    except Exception as e:
        log("warn shading_model: %s" % e)

    param = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -360, 0)
    param.set_editor_property("parameter_name", "Tint")
    param.set_editor_property("default_value", unreal.LinearColor(0.42, 0.42, 0.46, 1.0))
    mel.connect_material_property(param, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    return unreal.EditorAssetLibrary.load_asset(path)


def main():
    if not os.path.isdir(SRC):
        log("source missing %s" % SRC)
        flush()
        return

    stems = sorted(f[:-4] for f in os.listdir(SRC) if f.lower().endswith(".obj"))
    log("found %d obj" % len(stems))

    mat = build_material()
    if mat is None:
        flush()
        return

    tasks = []
    for stem in stems:
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", os.path.join(SRC, stem + ".obj"))
        t.set_editor_property("destination_path", MESH_DST)
        t.set_editor_property("destination_name", stem)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        tasks.append(t)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    log("import tasks done: %d" % len(tasks))

    # 清孤儿：源目录里没有的一律删掉（改名/删模型后不会留垃圾）
    want = set(stems)
    if unreal.EditorAssetLibrary.does_directory_exist(MESH_DST):
        for a in unreal.EditorAssetLibrary.list_assets(MESH_DST, recursive=False):
            short = a.split("/")[-1].split(".")[0]
            if short not in want:
                unreal.EditorAssetLibrary.delete_asset(a)
                log("removed stray %s" % short)

    ok, bad = 0, 0
    for stem in stems:
        path = MESH_DST + "/" + stem
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            log("MISSING %s" % stem)
            bad += 1
            continue
        mesh = unreal.EditorAssetLibrary.load_asset(path)
        try:
            old = mesh.get_editor_property("static_materials")
            new = []
            if old:
                for s in old:
                    sm = unreal.StaticMaterial()
                    sm.set_editor_property("material_interface", mat)
                    sm.set_editor_property("material_slot_name",
                                           s.get_editor_property("material_slot_name"))
                    new.append(sm)
            else:
                sm = unreal.StaticMaterial()
                sm.set_editor_property("material_interface", mat)
                sm.set_editor_property("material_slot_name", "ColorMap")
                new.append(sm)
            mesh.set_editor_property("static_materials", new)
        except Exception as e:
            log("warn material %s: %s" % (stem, e))
        unreal.EditorAssetLibrary.save_asset(path)

        b = mesh.get_bounds()
        e, o = b.box_extent, b.origin
        log("%-30s ext=%.0f,%.0f,%.0f pivotZ=%.0f tris=%d" % (
            stem, e.x, e.y, e.z, o.z, mesh.get_num_triangles(0)))
        ok += 1

    log("SUMMARY ok=%d bad=%d" % (ok, bad))
    flush()


main()
