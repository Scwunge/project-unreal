"""Create the block materials under /Game/Mc/Materials. Run headless from the project folder:

    "E:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" McClient.uproject
        -run=pythonscript -script="<abs path>/tools/make_materials.py" -unattended -nopause -NullRHI

Material assets can only be made inside the editor, so this script is how they are made and remade: the
.uasset files are build output of this script, committed so the game runs without running it.

The vanilla-look block materials are unlit. Minecraft's look comes from its own lighting (sky and block light,
AO and per-face shading), which the mesher bakes into the vertex colour. The texture comes from one texture
array: UV0 is the position inside the 16x16 texture and UV1.x is the layer.

The enhanced mode (M10, "Unreal Mode") swaps them for lit twins (the same name with "Lit" on the end): Unreal's sun,
shadows and global illumination light them, and only what Unreal can't know comes from the chunk's light: the sky's
ambient, which the sky light level lets in (so caves stay dark), and the glow of torches and other block light.

UV1.y says what kind of surface a vertex is on (McSurface in McSectionMesher.h), which only the lit twins act on:
water waves (moving the surface, bending its normal, glossy, see-through face on and mirror-like at a slant), lava
glows, leaves and plants sway in the wind, blocks that give light glow, and particles. Every other surface is
bump-mapped from its own texture (each texel's brightness against its neighbours') and goes wet and glossy where rain
falls on it. Kinds 9 and 10 are particles of one colour (the vertex colour), and 11 is a bit of a block's texture, flat-shaded, which the vanilla look draws too.

HD Textures (a second switch on top of Unreal Mode) has its own twins with "Hd" on the end of the name (M_McBlockOpaqueHd
and so on, and McRenderMode::Pick picks them): the same surface kinds, but albedo, normal map and specular map come from
three filtered arrays (BlocksHd, BlocksHdN, BlocksHdS), the opaque one adds parallax occlusion mapping, and they use
metalness, ambient occlusion and the map's emission. MC_ONLY_HD=1 makes only these.
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
if (round(Kind) >= 9.0 && round(Kind) < 10.5) Tex = 1.0;
float2 l = saturate(Light.xy);
float2 b = l / (4.0 - 3.0 * l);
float3 c = saturate(b.x * SkyFactor + b.y * float3(1.0, 0.92, 0.8));
float3 bright = 1.0 - pow(1.0 - c, 4.0);
c = lerp(c, bright, Gamma);
c = c * 0.96 + 0.04;
float3 display = saturate(Shade.rgb * c);
return Tex * pow(display, 2.2);
"""

# The surface kinds (UV1.y); see McSurface in McSectionMesher.h.
KIND_HLSL = "float k = round(Kind);\n"

# Moves the surface: leaves and plants in the wind (plants by how high up their texture the vertex is, so their roots
# stay put), water in slow swells that keep below the block's top. In Unreal units, in world space.
LIT_WPO_HLSL = KIND_HLSL + """
float3 o = 0;
float2 p = Pos.xy * 0.01;
if (k == 3.0 || k == 4.0)
{
    float w = (k == 4.0) ? saturate(1.0 - UV.y) : 1.0;
    float gust = 0.55 + 0.45 * sin(Time * 0.35 + p.x * 0.05 + p.y * 0.04);
    float s = sin(Time * 1.9 + p.x * 0.7 + p.y * 0.45 + Pos.z * 0.013);
    float c = cos(Time * 1.4 + p.x * 0.38 - p.y * 0.62);
    // In rain the wind picks up: stronger gusts, and the sway quickens.
    float storm = 1.0 + 1.6 * Wet;
    s = sin(Time * 1.9 * (1.0 + 0.6 * Wet) + p.x * 0.7 + p.y * 0.45 + Pos.z * 0.013);
    float a = (k == 3.0 ? 2.0 : 5.5) * gust * w * storm;
    o = float3(s * a, c * a * 0.7, k == 3.0 ? s * c * 0.8 : 0.0);
}
else if (k == 1.0)
{
    float h = sin(Time * 1.3 + p.x * 0.9 + p.y * 0.6) * 0.5 + sin(Time * 1.9 - p.x * 0.5 + p.y * 1.1) * 0.5;
    o = float3(0.0, 0.0, -3.0 + 2.0 * h * (1.0 + 0.8 * Wet));
}
return o;
"""

# The normal, in world space. Water: the slope of a few sines travelling different ways. Blocks: bumps from the texture,
# the brightness difference to the next texel across and down, put on the face with a frame made from the screen-space
# derivatives of position and UV (so it needs no tangents).
LIT_NORMAL_HLSL = KIND_HLSL + """
float3 N = normalize(VNormal);
if (k == 1.0)
{
    float2 p = Pos.xy * 0.01;
    float2 g = 0;
    g += float2(0.9, 0.6) * cos(Time * 1.3 + p.x * 0.9 + p.y * 0.6) * 0.5;
    g += float2(-0.5, 1.1) * cos(Time * 1.9 - p.x * 0.5 + p.y * 1.1) * 0.5;
    g += float2(2.3, -1.7) * cos(Time * 2.7 + p.x * 2.3 - p.y * 1.7) * 0.18;
    g += float2(-3.1, -2.9) * cos(Time * 3.3 - p.x * 3.1 - p.y * 2.9) * 0.12;
    g += float2(5.3, 4.1) * cos(Time * 4.1 + p.x * 5.3 + p.y * 4.1) * 0.06;
    g += float2(-7.7, 6.1) * cos(Time * 5.3 - p.x * 7.7 + p.y * 6.1) * 0.03;
    return normalize(N + saturate(N.z) * float3(-g * 0.11, 0.0));
}
if (k >= 9.0)
{
    return N;
}
float3 luma = float3(0.3, 0.59, 0.11);
float2 dh = float2(dot(TexX, luma) - dot(Tex, luma), dot(TexY, luma) - dot(Tex, luma));
float3 dpdx = ddx(Cam);
float3 dpdy = ddy(Cam);
float2 duvdx = ddx(UV);
float2 duvdy = ddy(UV);
float3 dp2perp = cross(dpdy, N);
float3 dp1perp = cross(N, dpdx);
float3 T = dp2perp * duvdx.x + dp1perp * duvdy.x;
float3 B = dp2perp * duvdx.y + dp1perp * duvdy.y;
float m = max(dot(T, T), dot(B, B));
if (m < 1e-12)
{
    return N;
}
float inv = rsqrt(m);
float s = Strength * (k == 2.0 ? 0.5 : 1.0);
return normalize(N - s * (dh.x * T + dh.y * B) * inv);
"""

# The HD twins (HD Textures, on top of Unreal Mode) read three arrays made by McHd: albedo ("BlocksHd"), a normal map
# ("BlocksHdN": normal x and y in red and green, occlusion in blue, height in alpha) and a specular map ("BlocksHdS":
# smoothness in red, F0 in green with 230 and up meaning metal, emission in alpha), the LabPBR layout. They are
# filtered with mips and anisotropy, and opaque blocks get parallax occlusion mapping from the height.
# The normal: water and particles as above; blocks from the normal map, put on the face with the same frame.
LIT_NORMAL_HD_HLSL = LIT_NORMAL_HLSL[:LIT_NORMAL_HLSL.index("float3 luma")] + """
float2 xy = (NMap.rg * 2.0 - 1.0) * Strength * (k == 2.0 ? 0.5 : 1.0);
float z = sqrt(max(1.0 - dot(xy, xy), 0.05));
float3 dpdx = ddx(Cam);
float3 dpdy = ddy(Cam);
float2 duvdx = ddx(UV);
float2 duvdy = ddy(UV);
float3 dp2perp = cross(dpdy, N);
float3 dp1perp = cross(N, dpdx);
float3 T = dp2perp * duvdx.x + dp1perp * duvdy.x;
float3 B = dp2perp * duvdx.y + dp1perp * duvdy.y;
if (max(dot(T, T), dot(B, B)) < 1e-12)
{
    return N;
}
T *= rsqrt(max(dot(T, T), 1e-12));
B *= rsqrt(max(dot(B, B), 1e-12));
return normalize(xy.x * T - xy.y * B + z * N);
"""

# Where the view ray, going into a block's surface, comes out in the picture: the whole shift in UV over the full depth
# (Scale, in units of the picture), in the frame of the face.
POM_SHIFT_HLSL = KIND_HLSL + """
float3 N = normalize(VNormal);
float3 V = normalize(View);
float3 dpdx = ddx(Cam);
float3 dpdy = ddy(Cam);
float2 duvdx = ddx(UV);
float2 duvdy = ddy(UV);
float3 dp2perp = cross(dpdy, N);
float3 dp1perp = cross(N, dpdx);
float3 T = dp2perp * duvdx.x + dp1perp * duvdy.x;
float3 B = dp2perp * duvdx.y + dp1perp * duvdy.y;
if (max(dot(T, T), dot(B, B)) < 1e-12 || k == 2.0 || k >= 9.0)
{
    return float3(0.0, 0.0, 0.0);
}
T *= rsqrt(max(dot(T, T), 1e-12));
B *= rsqrt(max(dot(B, B), 1e-12));
float2 off = -float2(dot(V, T), dot(V, B)) / max(dot(V, N), 0.2) * Scale;
return float3(off, 0.0);
"""
# One step of the ray march: S is (u, v, depth reached 0..1, 1 once the surface is hit). A layer above the surface (the
# height H stands for 1 - depth) lets the ray go a step deeper.
POM_STEP_HLSL = """
float4 s = S;
if (s.w < 0.5)
{
    if (s.z < 1.0 - H)
    {
        s.xy += Shift.xy * STEPSIZE;
        s.z += STEPSIZE;
    }
    else
    {
        s.w = 1.0;
    }
}
return s;
"""
POM_FINAL_HLSL = """
float2 uv = S.xy;
if (S.w > 0.5)
{
    uv -= 0.5 * Shift.xy * STEPSIZE;
}
return float3(uv, Layer);
"""

# How wet the surface is: open to the sky (sky light near full) and facing up, as rain falls.
WET_HLSL = "float wet = Wet * saturate((Light.x - 0.9) * 10.0) * saturate(VNormal.z);\n"

LIT_BASE_KINDS_HLSL = KIND_HLSL + WET_HLSL + """
float3 a = (k >= 9.0 && k < 10.5 ? 1.0 : Tex) * pow(saturate(Shade.rgb), 2.2);
if (k == 1.0)
{
    a *= 0.5;
    // Deeper water takes the colour out of what lies in it and goes a dark teal; at the shore (the scene's depth a
    // hand's breadth behind the surface) a ragged line of foam, drifting with the waves.
    float deep = saturate(1.0 - exp(-max(Depth, 0.0) / 250.0));
    a = lerp(a, a * float3(0.45, 0.8, 0.9) * 0.6, deep);
    float foam = saturate(1.0 - max(Depth, 0.0) / 40.0);
    foam *= 0.55 + 0.45 * sin(Time * 1.7 + Pos.x * 0.21 + Pos.y * 0.17 + sin(Pos.y * 0.31) * 2.0);
    a = lerp(a, float3(0.8, 0.85, 0.85), saturate(foam * foam * 1.4));
}
else if (k == 2.0)
{
    a *= 0.3;
}
return a * lerp(1.0, 0.62, wet);
"""

LIT_ROUGHNESS_HLSL = KIND_HLSL + WET_HLSL + """
float r = Base - 0.2 * dot(Tex, float3(0.3, 0.59, 0.11));
if (k == 1.0) r = 0.02;
else if (k == 2.0) r = 0.5;
else if (Glass > 0.5 && k == 0.0) r = 0.06;
else if (k >= 9.0) r = 0.9;
return lerp(r, 0.1, wet);
"""

LIT_SPECULAR_HLSL = KIND_HLSL + WET_HLSL + """
float s = 0.3;
if (k == 1.0 || (Glass > 0.5 && k == 0.0)) s = 0.5;
return lerp(s, 0.6, wet);
"""

# What the lit twins add that Unreal's lights don't. Lava burns, flickering slowly; blocks that give light and glowing
# particles glow, so they bloom.
LIT_EMISSIVE_KINDS_HLSL = KIND_HLSL + """
float2 l = saturate(Light.xy);
float2 b = l / (4.0 - 3.0 * l);
float3 sky = pow(b.x * SkyFactor, 2.2) * float3(0.55, 0.65, 0.85) * Ambient;
// Torch light breathes a little, each place out of step with the next.
float flick = 1.0 + 0.07 * sin(Time * 9.0 + Pos.x * 0.004 + Pos.y * 0.003) * sin(Time * 5.3 + Pos.y * 0.004 - Pos.x * 0.002);
float3 glow = pow(b.y, 2.2) * float3(1.0, 0.85, 0.65) * BlockGlow * lerp(1.0, flick, saturate(b.y * 1.5 - b.x * 0.5));
float3 e = Albedo * (sky + glow);
if (k == 2.0)
{
    float2 p = Pos.xy * 0.01;
    // The bright parts burn and the dark crust stays dark.
    float burn = saturate(dot(Albedo, float3(0.3, 0.59, 0.11)) * 1.3);
    e += Albedo * Lava * (0.04 + 0.96 * burn * burn * burn) * (0.9 + 0.1 * sin(Time * 1.7 + p.x * 1.3 + p.y * 0.9));
}
else if (k == 5.0)
{
    e += Albedo * Glow;
}
else if (k == 10.0)
{
    e += Albedo * Glow * 3.0;
}
return e;
"""

# Water is see-through looking down into it and turns to a mirror at a slant (Fresnel).
LIT_OPACITY_HLSL = KIND_HLSL + """
if (k == 1.0)
{
    float f = pow(1.0 - saturate(abs(dot(normalize(VNormal), normalize(View)))), 5.0);
    float o = saturate(lerp(0.55, 1.0, f) * max(A, 0.6));
    // Murkier with depth, solid where the foam is.
    float foam = saturate(1.0 - max(Depth, 0.0) / 40.0);
    o = max(o, 0.4 + 0.55 * saturate(1.0 - exp(-max(Depth, 0.0) / 300.0)));
    return max(o, foam * 0.9);
}
return A;
"""

# The sky's materials. The dome and the stars' squares take their colour from the vertex colour (display space, raised
# to 2.2 like the blocks'). The additive one draws the sun, the moon and the stars on top of the dome: Solid swaps the
# texture for white (the stars), Tint scales the result (the stars fading in).
SKY_ADDITIVE_HLSL = """
return lerp(Tex, float3(1.0, 1.0, 1.0), Solid) * pow(saturate(Shade.rgb), 2.2) * Tint;
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


def add_custom(material, x: int, y: int, description: str, code: str, sources: dict, scalar: bool = False,
               four: bool = False):
    # A Custom node with one input per entry of sources (input name: (expression, output name)); float3 out, or one
    # float with scalar, or float4 with four.
    node = mel.create_material_expression(material, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1 if scalar
                             else unreal.CustomMaterialOutputType.CMOT_FLOAT4 if four
                             else unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    node.set_editor_property("description", description)
    inputs = []
    for input_name in sources:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        inputs.append(custom_input)
    node.set_editor_property("inputs", inputs)
    node.set_editor_property("code", code)
    for input_name, (expression, output) in sources.items():
        mel.connect_material_expressions(expression, output, node, input_name)
    return node


def add_scalar(material, x: int, y: int, name: str, value: float):
    node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def add_mask(material, x: int, y: int, source, channel: str):
    # One channel ("r", "g", "b" or "a") of source.
    node = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, x, y)
    for name in "rgba":
        node.set_editor_property(name, name == channel)
    mel.connect_material_expressions(source, "", node, "")
    return node


def add_array_sample(material, x: int, y: int, uvw, default_array):
    node = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2DArray, x, y)
    node.set_editor_property("parameter_name", "Blocks")
    node.set_editor_property("texture", default_array)
    node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    mel.connect_material_expressions(uvw, "", node, "UVs")
    return node


def add_offset(material, x: int, y: int, source, offset):
    # source + a constant (u, v, layer) offset.
    constant = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, x - 150, y)
    constant.set_editor_property("constant", unreal.LinearColor(offset[0], offset[1], offset[2], 0.0))
    node = mel.create_material_expression(material, unreal.MaterialExpressionAdd, x, y)
    mel.connect_material_expressions(source, "", node, "A")
    mel.connect_material_expressions(constant, "", node, "B")
    return node


POM_STEPS = 14
POM_DEPTH = 0.12


def add_hd_sample(material, x: int, y: int, parameter: str, uvw, default_array, linear: bool):
    node = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2DArray, x, y)
    node.set_editor_property("parameter_name", parameter)
    node.set_editor_property("texture", default_array)
    node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if linear
                             else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    mel.connect_material_expressions(uvw, "", node, "UVs")
    return node


def make_pom(material, x: int, y: int, uv0, layer, common, uvw_extra, linear_array):
    """Parallax occlusion mapping from the normal array's alpha: the layered ray march as a chain of nodes (a Custom node
    can't sample a texture array). Returns the node whose output is the (u, v, layer) to read the maps at."""
    shift = add_custom(material, x, y + 200, "McPomShift", POM_SHIFT_HLSL,
                       {"Kind": common["Kind"], "VNormal": common["VNormal"], "View": uvw_extra["View"], "Cam": uvw_extra["Cam"],
                        "UV": (uv0, ""), "Scale": (add_scalar(material, x - 200, y + 300, "ParallaxDepth", POM_DEPTH), "")})
    start = add_custom(material, x, y, "McPomStart", "return float4(UV.xy, 0.0, 0.0);", {"UV": (uv0, "")}, four=True)
    state = start
    for step in range(POM_STEPS):
        step_x = x + 250 * (step + 1)
        at = add_custom(material, step_x, y, "McPomAt", "return float3(S.xy, L);", {"S": (state, ""), "L": (layer, "")})
        height = add_hd_sample(material, step_x, y + 150, "BlocksHdN", at, linear_array, True)
        state = add_custom(material, step_x, y + 350, "McPomStep", POM_STEP_HLSL.replace("STEPSIZE", f"{1.0 / POM_STEPS}"),
                           {"S": (state, ""), "H": (height, "A"), "Shift": (shift, "")}, four=True)
    return add_custom(material, x + 250 * (POM_STEPS + 1), y, "McPomFinal", POM_FINAL_HLSL.replace("STEPSIZE", f"{1.0 / POM_STEPS}"),
                      {"S": (state, ""), "Shift": (shift, ""), "Layer": (layer, "")})


def make_block_material(name: str, blend_mode, default_array, vertex_alpha: bool = False, two_sided: bool = False,
                        lit: bool = False, hd_linear_array=None):
    # Nothing refers to the materials as assets (the game loads them by path), so they can be made afresh.
    # vertex_alpha: the vertex colour's alpha scales the opacity too (rain fading with distance, clouds).
    # two_sided: both sides are drawn (rain's sheets); see-through boxes like the clouds need only the near ones, or
    # their far faces show through.
    if eal.does_asset_exist(f"{FOLDER}/{name}"):
        eal.delete_asset(f"{FOLDER}/{name}")
    material = asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model",
                                  unreal.MaterialShadingModel.MSM_DEFAULT_LIT if lit else unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", blend_mode)
    if lit and blend_mode == unreal.BlendMode.BLEND_TRANSLUCENT:
        # Water and glass lit per pixel like the rest (forward shaded, which Lumen's front-layer reflections need).
        material.set_editor_property("translucency_lighting_mode",
                                      unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    if lit:
        # The normal the shaders make is in world space.
        material.set_editor_property("tangent_space_normal", False)
    if two_sided:
        material.set_editor_property("two_sided", True)
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
    kind = add_mask(material, -700, 300, uv1, "g")
    uv2 = mel.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -350, 400)
    uv2.set_editor_property("coordinate_index", 2)
    sky_factor = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -350, 500)
    sky_factor.set_editor_property("parameter_name", "SkyFactor")
    sky_factor.set_editor_property("default_value", 1.0)
    gamma = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -350, 600)
    gamma.set_editor_property("parameter_name", "Gamma")
    gamma.set_editor_property("default_value", 0.5)

    opacity_source = (texture, "A")
    if lit:
        time = mel.create_material_expression(material, unreal.MaterialExpressionTime, -700, 900)
        absolute = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -700, 1000)
        absolute.set_editor_property("world_position_shader_offset",
                                     unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
        relative = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -700, 1100)
        relative.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
        vertex_normal = mel.create_material_expression(material, unreal.MaterialExpressionVertexNormalWS, -700, 1200)
        camera = mel.create_material_expression(material, unreal.MaterialExpressionCameraVectorWS, -700, 1300)
        wetness = add_scalar(material, -700, 1400, "Wetness", 0.0)
        glass = add_scalar(material, -700, 1500, "Glass", 1.0 if blend_mode == unreal.BlendMode.BLEND_TRANSLUCENT else 0.0)
        # How far the scene is behind this surface (cm): only translucent surfaces can read it; the rest say "far".
        if blend_mode == unreal.BlendMode.BLEND_TRANSLUCENT and not vertex_alpha:
            scene_depth = mel.create_material_expression(material, unreal.MaterialExpressionSceneDepth, -900, 1600)
            pixel_depth = mel.create_material_expression(material, unreal.MaterialExpressionPixelDepth, -900, 1700)
            behind = mel.create_material_expression(material, unreal.MaterialExpressionSubtract, -700, 1650)
            mel.connect_material_expressions(scene_depth, "", behind, "A")
            mel.connect_material_expressions(pixel_depth, "", behind, "B")
        else:
            behind = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -700, 1650)
            behind.set_editor_property("r", 100000.0)
        common = {"Kind": (kind, ""), "Light": (uv2, ""), "VNormal": (vertex_normal, ""), "Wet": (wetness, ""), "Depth": (behind, "")}
        hd = hd_linear_array is not None
        if hd:
            # The HD twin reads its own arrays (albedo, normal, specular), with parallax on opaque blocks.
            read_uvw = uvw
            if blend_mode == unreal.BlendMode.BLEND_OPAQUE:
                read_uvw = make_pom(material, -2400, 1900, uv0, layer, common, {"View": (camera, ""), "Cam": (relative, "")}, hd_linear_array)
            texture = add_hd_sample(material, -350, 0, "BlocksHd", read_uvw, default_array, False)
            normal_map = add_hd_sample(material, -350, -300, "BlocksHdN", read_uvw, hd_linear_array, True)
            spec_map = add_hd_sample(material, -350, -450, "BlocksHdS", read_uvw, hd_linear_array, True)
            opacity_source = (texture, "A")
        else:
            across = add_array_sample(material, -350, -300, add_offset(material, -500, -300, uvw, (1.0 / 16.0, 0.0, 0.0)), default_array)
            down = add_array_sample(material, -350, -450, add_offset(material, -500, -450, uvw, (0.0, 1.0 / 16.0, 0.0)), default_array)

        albedo = add_custom(material, -100, -150, "McLitBase", LIT_BASE_KINDS_HLSL,
                            dict(common, Tex=(texture, "RGB"), Shade=(vertex_color, ""), Time=(time, ""), Pos=(absolute, "")))
        mel.connect_material_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
        emissive_code = LIT_EMISSIVE_KINDS_HLSL
        emissive_extra = {}
        if hd:
            # LabPBR emission: alpha 255 is none, else 0..254 scales the colour.
            emissive_code = emissive_code.replace("return e;", "float a = EmisA * 255.0;\nif (a < 254.5) e += Albedo * (a / 254.0) * 4.0;\nreturn e;")
            emissive_extra = {"EmisA": (spec_map, "A")}
        glow = add_custom(material, -100, 0, "McLitEmissive", emissive_code,
                          {**emissive_extra, "Kind": (kind, ""), "Albedo": (albedo, ""), "Light": (uv2, ""), "SkyFactor": (sky_factor, ""),
                           "Ambient": (add_scalar(material, -350, 700, "Ambient", 1.0), ""),
                           "BlockGlow": (add_scalar(material, -350, 800, "BlockGlow", 2.0), ""),
                           "Lava": (add_scalar(material, -350, 1600, "LavaGlow", 0.9), ""),
                           "Glow": (add_scalar(material, -350, 1700, "Glow", 2.5), ""),
                           "Time": (time, ""), "Pos": (absolute, "")})
        mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        if hd:
            normal = add_custom(material, -100, 150, "McHdNormal", LIT_NORMAL_HD_HLSL,
                                {"Kind": (kind, ""), "VNormal": (vertex_normal, ""), "NMap": (normal_map, "RGB"), "Cam": (relative, ""),
                                 "UV": (uv0, ""), "Pos": (absolute, ""), "Time": (time, ""),
                                 "Strength": (add_scalar(material, -350, 1800, "Bump", 1.0), "")})
            roughness = add_custom(material, -100, 450, "McHdRoughness",
                                   KIND_HLSL + WET_HLSL + "float r = max(pow(1.0 - S.r, 2.0), 0.04);\nif (k == 1.0) r = 0.02;\nelse if (k >= 9.0) r = 0.9;\nreturn lerp(r, 0.1, wet);\n",
                                   dict(common, S=(spec_map, "RGB")), scalar=True)
            specular = add_custom(material, -100, 550, "McHdSpecular",
                                  KIND_HLSL + WET_HLSL + "float s = (k == 1.0) ? 0.5 : saturate(S.g / 0.08);\nreturn lerp(s, 0.6, wet);\n",
                                  dict(common, S=(spec_map, "RGB")), scalar=True)
            metallic = add_custom(material, -100, 650, "McHdMetallic",
                                  KIND_HLSL + "return (k < 9.0 && k != 1.0 && S.g >= 0.9) ? 1.0 : 0.0;\n",
                                  {"Kind": (kind, ""), "S": (spec_map, "RGB")}, scalar=True)
            occlusion = add_custom(material, -100, 750, "McHdOcclusion", "return lerp(1.0, N.b, 0.8);\n",
                                   {"N": (normal_map, "RGB")}, scalar=True)
            mel.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
            mel.connect_material_property(occlusion, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
        else:
            normal = add_custom(material, -100, 150, "McLitNormal", LIT_NORMAL_HLSL,
                                {"Kind": (kind, ""), "VNormal": (vertex_normal, ""), "Tex": (texture, "RGB"),
                                 "TexX": (across, "RGB"), "TexY": (down, "RGB"), "Cam": (relative, ""), "UV": (uv0, ""),
                                 "Pos": (absolute, ""), "Time": (time, ""),
                                 "Strength": (add_scalar(material, -350, 1800, "Bump", 1.6), "")})
            roughness = add_custom(material, -100, 450, "McLitRoughness", LIT_ROUGHNESS_HLSL,
                                   dict(common, Tex=(texture, "RGB"), Glass=(glass, ""),
                                        Base=(add_scalar(material, -350, 1900, "Roughness", 0.85), "")), scalar=True)
            specular = add_custom(material, -100, 550, "McLitSpecular", LIT_SPECULAR_HLSL,
                                  dict(common, Glass=(glass, "")), scalar=True)
        mel.connect_material_property(normal, "", unreal.MaterialProperty.MP_NORMAL)
        mel.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
        if blend_mode != unreal.BlendMode.BLEND_OPAQUE:
            offset = add_custom(material, -100, 700, "McLitOffset", LIT_WPO_HLSL,
                                {"Kind": (kind, ""), "UV": (uv0, ""), "Pos": (absolute, ""), "Time": (time, ""), "Wet": (wetness, "")})
            mel.connect_material_property(offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        if blend_mode == unreal.BlendMode.BLEND_TRANSLUCENT and not vertex_alpha:
            fresnel = add_custom(material, -100, 300, "McLitOpacity", LIT_OPACITY_HLSL,
                                 {"Kind": (kind, ""), "A": (texture, "A"), "VNormal": (vertex_normal, ""), "View": (camera, ""), "Depth": (behind, "")},
                                 scalar=True)
            opacity_source = (fresnel, "")
    else:
        colour = add_custom(material, -100, 0, "McLightmap", LIGHTMAP_HLSL,
                            {"Tex": (texture, "RGB"), "Shade": (vertex_color, ""), "Light": (uv2, ""),
                             "SkyFactor": (sky_factor, ""), "Gamma": (gamma, ""), "Kind": (kind, "")})
        mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    if blend_mode == unreal.BlendMode.BLEND_MASKED:
        # Particles of one colour are solid whatever the texture under them.
        mask = add_custom(material, -100, 250, "McMask", "return (round(Kind) >= 9.0 && round(Kind) < 10.5) ? 1.0 : A;",
                          {"Kind": (kind, ""), "A": (texture, "A")}, scalar=True)
        mel.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    elif blend_mode == unreal.BlendMode.BLEND_TRANSLUCENT and vertex_alpha:
        opacity = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -100, 300)
        mel.connect_material_expressions(texture, "A", opacity, "A")
        mel.connect_material_expressions(vertex_color, "A", opacity, "B")
        mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    elif blend_mode == unreal.BlendMode.BLEND_TRANSLUCENT:
        # The game blends see-through blocks in sRGB, where Unreal blends in linear light: a texture's 40% glass looks
        # about 56% as strong there. Raising the opacity (alpha ** 0.63) makes the blend the same for glass, ice, water,
        # slime and honey (measured against the game's own shots; the 0.4 of stained glass lands on 0.56).
        srgb = add_custom(material, -100, 350, "McSrgbOpacity", "return pow(saturate(A), 0.63);",
                          {"A": (opacity_source[0], opacity_source[1])}, scalar=True)
        mel.connect_material_property(srgb, "", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(material)
    eal.save_loaded_asset(material)
    unreal.log(f"McMaterials: made {name}")


def make_sky_materials(default_array):
    for name in ["M_McSkyDome", "M_McSkyAdditive"]:
        if eal.does_asset_exist(f"{FOLDER}/{name}"):
            eal.delete_asset(f"{FOLDER}/{name}")

    # The dome: opaque, drawn from inside.
    dome = asset_tools.create_asset("M_McSkyDome", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    dome.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    dome.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    dome.set_editor_property("two_sided", True)
    vertex_color = mel.create_material_expression(dome, unreal.MaterialExpressionVertexColor, -500, 0)
    power = mel.create_material_expression(dome, unreal.MaterialExpressionPower, -250, 0)
    power.set_editor_property("const_exponent", 2.2)
    mel.connect_material_expressions(vertex_color, "", power, "Base")
    mel.connect_material_property(power, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(dome)
    eal.save_loaded_asset(dome)
    unreal.log("McMaterials: made M_McSkyDome")

    # The sun, moon and stars: added to what is behind them.
    add = asset_tools.create_asset("M_McSkyAdditive", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    add.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    add.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    add.set_editor_property("two_sided", True)
    uv0 = mel.create_material_expression(add, unreal.MaterialExpressionTextureCoordinate, -900, 0)
    uv0.set_editor_property("coordinate_index", 0)
    uv1 = mel.create_material_expression(add, unreal.MaterialExpressionTextureCoordinate, -900, 150)
    uv1.set_editor_property("coordinate_index", 1)
    layer = mel.create_material_expression(add, unreal.MaterialExpressionComponentMask, -700, 150)
    layer.set_editor_property("r", True)
    layer.set_editor_property("g", False)
    layer.set_editor_property("b", False)
    layer.set_editor_property("a", False)
    mel.connect_material_expressions(uv1, "", layer, "")
    uvw = mel.create_material_expression(add, unreal.MaterialExpressionAppendVector, -550, 50)
    mel.connect_material_expressions(uv0, "", uvw, "A")
    mel.connect_material_expressions(layer, "", uvw, "B")
    texture = mel.create_material_expression(add, unreal.MaterialExpressionTextureSampleParameter2DArray, -350, 0)
    texture.set_editor_property("parameter_name", "Blocks")
    texture.set_editor_property("texture", default_array)
    texture.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    mel.connect_material_expressions(uvw, "", texture, "UVs")
    vertex_color = mel.create_material_expression(add, unreal.MaterialExpressionVertexColor, -350, 250)
    solid = mel.create_material_expression(add, unreal.MaterialExpressionScalarParameter, -350, 450)
    solid.set_editor_property("parameter_name", "Solid")
    solid.set_editor_property("default_value", 0.0)
    tint = mel.create_material_expression(add, unreal.MaterialExpressionVectorParameter, -350, 550)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    colour = mel.create_material_expression(add, unreal.MaterialExpressionCustom, -100, 0)
    colour.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    colour.set_editor_property("description", "McSkyAdditive")
    inputs = []
    for input_name in ["Tex", "Shade", "Solid", "Tint"]:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        inputs.append(custom_input)
    colour.set_editor_property("inputs", inputs)
    colour.set_editor_property("code", SKY_ADDITIVE_HLSL)
    mel.connect_material_expressions(texture, "RGB", colour, "Tex")
    mel.connect_material_expressions(vertex_color, "", colour, "Shade")
    mel.connect_material_expressions(solid, "", colour, "Solid")
    mel.connect_material_expressions(tint, "", colour, "Tint")
    mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(add)
    eal.save_loaded_asset(add)
    unreal.log("McMaterials: made M_McSkyAdditive")


def make_linear_placeholder():
    """The default of the HD normal and specular arrays: like the placeholder but not sRGB, so a Linear Color sampler
    accepts it."""
    path = f"{FOLDER}/TA_McPlaceholderLinear"
    if eal.does_asset_exist(path):
        return unreal.load_asset(path)
    task = unreal.AssetImportTask()
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("filename", PLACEHOLDER_PNG)
    task.set_editor_property("destination_path", FOLDER)
    task.set_editor_property("destination_name", "T_McPlaceholderLinear")
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    asset_tools.import_asset_tasks([task])
    texture = unreal.load_asset(f"{FOLDER}/T_McPlaceholderLinear")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    eal.save_loaded_asset(texture)
    array = load_or_create("TA_McPlaceholderLinear", unreal.Texture2DArray, unreal.Texture2DArrayFactory())
    array.set_editor_property("source_textures", [texture])
    array.set_editor_property("srgb", False)
    array.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    eal.save_loaded_asset(array)
    return array


def make_hd_materials(default_array):
    linear = make_linear_placeholder()
    make_block_material("M_McBlockOpaqueHd", unreal.BlendMode.BLEND_OPAQUE, default_array, lit=True, hd_linear_array=linear)
    make_block_material("M_McBlockCutoutHd", unreal.BlendMode.BLEND_MASKED, default_array, lit=True, hd_linear_array=linear)
    make_block_material("M_McBlockTranslucentHd", unreal.BlendMode.BLEND_TRANSLUCENT, default_array, lit=True, hd_linear_array=linear)


def make_lit_materials(default_array):
    make_block_material("M_McBlockOpaqueLit", unreal.BlendMode.BLEND_OPAQUE, default_array, lit=True)
    make_block_material("M_McBlockCutoutLit", unreal.BlendMode.BLEND_MASKED, default_array, lit=True)
    make_block_material("M_McBlockTranslucentLit", unreal.BlendMode.BLEND_TRANSLUCENT, default_array, lit=True)


def main() -> None:
    if os.environ.get("MC_ONLY_SKY"):
        # Leaves the block materials (and the placeholder they hold) as they are.
        make_sky_materials(unreal.load_asset(f"{FOLDER}/TA_McPlaceholder"))
        return
    if os.environ.get("MC_ONLY_HD"):
        write_checker_png(PLACEHOLDER_PNG)
        make_hd_materials(unreal.load_asset(f"{FOLDER}/TA_McPlaceholder"))
        return
    if os.environ.get("MC_ONLY_LIT"):
        make_lit_materials(unreal.load_asset(f"{FOLDER}/TA_McPlaceholder"))
        return
    if os.environ.get("MC_ONLY_BLOCKS"):
        # The six block materials, keeping the placeholder texture they hold.
        placeholder = unreal.load_asset(f"{FOLDER}/TA_McPlaceholder")
        make_block_material("M_McBlockOpaque", unreal.BlendMode.BLEND_OPAQUE, placeholder)
        make_block_material("M_McBlockCutout", unreal.BlendMode.BLEND_MASKED, placeholder)
        make_block_material("M_McBlockTranslucent", unreal.BlendMode.BLEND_TRANSLUCENT, placeholder)
        make_lit_materials(placeholder)
        return
    if os.environ.get("MC_ONLY_WEATHER"):
        placeholder = unreal.load_asset(f"{FOLDER}/TA_McPlaceholder")
        make_block_material("M_McWeather", unreal.BlendMode.BLEND_TRANSLUCENT, placeholder, True, True)
        make_block_material("M_McClouds", unreal.BlendMode.BLEND_TRANSLUCENT, placeholder, True)
        return
    array = make_placeholder_array()
    make_block_material("M_McBlockOpaque", unreal.BlendMode.BLEND_OPAQUE, array)
    make_block_material("M_McBlockCutout", unreal.BlendMode.BLEND_MASKED, array)
    make_block_material("M_McBlockTranslucent", unreal.BlendMode.BLEND_TRANSLUCENT, array)
    make_lit_materials(array)
    make_hd_materials(array)
    # Rain and snow, and the clouds.
    make_block_material("M_McWeather", unreal.BlendMode.BLEND_TRANSLUCENT, array, True, True)
    make_block_material("M_McClouds", unreal.BlendMode.BLEND_TRANSLUCENT, array, True)
    make_sky_materials(array)


main()
