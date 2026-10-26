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

# Minecraft's light curve, in display (sRGB) space like the game, then back to linear for Unreal.
# Light.x is sky light and Light.y block light, each 0..1 (level / 15). A level's brightness is
# l / (4 - 3l): each level down is noticeably darker. Sky light scales with the time of day (SkyFactor);
# block light is a warm white. The Brightness option (Gamma, 0 Moody .. 1 Bright) lifts the curve, and
# a small floor keeps unlit places from going fully black. Shade is the vertex colour: tint, face shade
# and ambient occlusion, also in display space.
LIGHTMAP_HLSL = """
float2 l = saturate(Light.xy);
float2 b = l / (4.0 - 3.0 * l);
float3 c = saturate(b.x * SkyFactor + b.y * float3(1.0, 0.92, 0.8));
float3 bright = 1.0 - pow(1.0 - c, 4.0);
c = lerp(c, bright, Gamma);
c = c * 0.96 + 0.04;
float3 display = saturate(Shade.rgb * c);
return Tex * pow(display, 2.2);
"""

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
    # Nothing refers to the materials as assets (the game loads them by path), so they can be made afresh.
    if eal.does_asset_exist(f"{FOLDER}/{name}"):
        eal.delete_asset(f"{FOLDER}/{name}")
    material = asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
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
    uv2 = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -350, 400)
    uv2.set_editor_property("coordinate_index", 2)
    sky_factor = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -350, 500)
    sky_factor.set_editor_property("parameter_name", "SkyFactor")
    sky_factor.set_editor_property("default_value", 1.0)
    gamma = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -350, 600)
    gamma.set_editor_property("parameter_name", "Gamma")
    gamma.set_editor_property("default_value", 0.5)

    colour = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -100, 0)
    colour.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    colour.set_editor_property("description", "McLightmap")
    inputs = []
    for input_name in ["Tex", "Shade", "Light", "SkyFactor", "Gamma"]:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        inputs.append(custom_input)
    colour.set_editor_property("inputs", inputs)
    colour.set_editor_property("code", LIGHTMAP_HLSL)
    mel.connect_material_expressions(texture, "RGB", colour, "Tex")
    mel.connect_material_expressions(vertex_color, "", colour, "Shade")
    mel.connect_material_expressions(uv2, "", colour, "Light")
    mel.connect_material_expressions(sky_factor, "", colour, "SkyFactor")
    mel.connect_material_expressions(gamma, "", colour, "Gamma")
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
