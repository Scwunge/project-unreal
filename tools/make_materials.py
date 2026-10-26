"""Create the block materials under /Game/Mc/Materials. Run headless from the project folder:

    "E:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" McClient.uproject
        -run=pythonscript -script="<abs path>/tools/make_materials.py" -unattended -nopause -NullRHI

Material assets can only be made inside the editor, so this script is how they are made and remade: the
.uasset files are build output of this script, committed so the game runs without running it.

Every block material is unlit. Minecraft's look comes from its own lighting (sky and block light, AO and
per-face shading), which the mesher bakes into the vertex colour. The texture comes from one texture
array: UV0 is the position inside the 16x16 texture and UV1.x is the layer.
"""

import os
import struct
import zlib

import unreal

FOLDER = "/Game/Mc/Materials"
PLACEHOLDER_PNG = os.path.join(unreal.Paths.project_saved_dir(), "McPlaceholder.png")

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def write_checker_png(path: str) -> None:
    """A 16x16 magenta and black checker, written by hand so no image library is needed."""
    rows = []
    for y in range(16):
        row = bytearray([0])  # filter type: none
        for x in range(16):
            on = (x // 8 + y // 8) % 2 == 0
            row += bytes([255, 0, 255, 255] if on else [0, 0, 0, 255])
        rows.append(bytes(row))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", 16, 16, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows)))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def load_or_create(name: str, asset_class, factory):
    # Existing assets are reused, not deleted: they reference each other, and deleting a referenced
    # asset fails when run unattended.
    path = f"{FOLDER}/{name}"
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)
    return asset_tools.create_asset(name, FOLDER, asset_class, factory)


def make_placeholder_array():
    """The texture array the materials hold by default; the game swaps in the real one at runtime."""
    write_checker_png(PLACEHOLDER_PNG)
    task = unreal.AssetImportTask()
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("filename", PLACEHOLDER_PNG)
    task.set_editor_property("destination_path", FOLDER)
    task.set_editor_property("destination_name", "T_McPlaceholder")
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    asset_tools.import_asset_tasks([task])
    texture = unreal.load_asset(f"{FOLDER}/T_McPlaceholder")
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    eal.save_loaded_asset(texture)

    # The factory's InitialTextures isn't visible to Python; setting SourceTextures on the asset rebuilds
    # its source the same way the editor's details panel does.
    array = load_or_create("TA_McPlaceholder", unreal.Texture2DArray, unreal.Texture2DArrayFactory())
    array.set_editor_property("source_textures", [texture])
    array.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    eal.save_loaded_asset(array)
    return array


def make_block_material(name: str, blend_mode, default_array):
    material = load_or_create(name, unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(material)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", blend_mode)
    if blend_mode == unreal.BlendMode.BLEND_MASKED:
        # Minecraft's cutout layer drops texels below 10% alpha.
        material.set_editor_property("opacity_mask_clip_value", 0.1)
    material.set_editor_property("used_with_static_lighting", False)

    uv0 = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    uv0.set_editor_property("coordinate_index", 0)
    uv1 = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -900, 150)
    uv1.set_editor_property("coordinate_index", 1)
    layer = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, -700, 150)
    layer.set_editor_property("r", True)
    layer.set_editor_property("g", False)
    layer.set_editor_property("b", False)
    layer.set_editor_property("a", False)
    mel.connect_material_expressions(uv1, "", layer, "")
    uvw = mel.create_material_expression(material, unreal.MaterialExpressionAppendVector, -550, 50)
    mel.connect_material_expressions(uv0, "", uvw, "A")
    mel.connect_material_expressions(layer, "", uvw, "B")

    texture = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2DArray, -350, 0)
    texture.set_editor_property("parameter_name", "Blocks")
    texture.set_editor_property("texture", default_array)
    texture.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    mel.connect_material_expressions(uvw, "", texture, "UVs")

    vertex_color = mel.create_material_expression(material, unreal.MaterialExpressionVertexColor, -350, 250)
    colour = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -100, 0)
    mel.connect_material_expressions(texture, "RGB", colour, "A")
    mel.connect_material_expressions(vertex_color, "", colour, "B")
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    if blend_mode == unreal.BlendMode.BLEND_MASKED:
        mel.connect_material_property(texture, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    elif blend_mode == unreal.BlendMode.BLEND_TRANSLUCENT:
        mel.connect_material_property(texture, "A", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(material)
    eal.save_loaded_asset(material)
    unreal.log(f"McMaterials: made {name}")


def main() -> None:
    array = make_placeholder_array()
    make_block_material("M_McBlockOpaque", unreal.BlendMode.BLEND_OPAQUE, array)
    make_block_material("M_McBlockCutout", unreal.BlendMode.BLEND_MASKED, array)
    make_block_material("M_McBlockTranslucent", unreal.BlendMode.BLEND_TRANSLUCENT, array)


main()
