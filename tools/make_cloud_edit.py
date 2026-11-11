# Development: M_McVolumeClouds as a duplicate of the engine's simple volumetric cloud material with only its extinction
# (the pin behind the density) replaced by our mask-driven density; the other three pins keep the engine's graph.
import unreal

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
target = "/Game/Mc/Materials/M_McVolumeClouds"
if eal.does_asset_exist(target):
    eal.delete_asset(target)
material = eal.duplicate_asset("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud", target)
switch = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
unreal.log(f"CloudEdit: extinction pin <- {switch.get_name()} ({switch.get_class().get_name()})")


def scalar(name, value, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -3000, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    return node


def custom(y, description, code, sources, scalar_out):
    node = mel.create_material_expression(material, unreal.MaterialExpressionCustom, -2400, y)
    node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1 if scalar_out else unreal.CustomMaterialOutputType.CMOT_FLOAT3)
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


position = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -3000, 0)
time = mel.create_material_expression(material, unreal.MaterialExpressionTime, -3000, 100)
mask = mel.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, -3000, 200)
mask.set_editor_property("parameter_name", "CloudMask")
mask.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture"))
mask.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
code = """
float2 uv = float2((Pos.x / 100.0 + Drift) / 12.0 / 256.0, (Pos.y / 100.0) / 12.0 / 256.0);
float M = saturate(MaskTex.SampleLevel(MaskTexSampler, uv, 0).r);
float cover = smoothstep(0.35, 0.65, M);
float h = saturate((Pos.z - Bottom) / Thick);
float profile = smoothstep(0.0, 0.22, h) * (1.0 - smoothstep(0.45 + 0.4 * cover, 1.0, h));
float n = 0.5 + 0.25 * sin(Pos.x * 0.0021 + Time * 0.03) * sin(Pos.y * 0.0027 - Time * 0.02)
        + 0.25 * sin(Pos.z * 0.004 + Pos.x * 0.0043) * cos(Pos.y * 0.0051 + Time * 0.025);
float d = saturate(cover * profile * (0.55 + 0.9 * n) - (1.0 - cover) * 0.5);
return d * Dens * (1.0 + Storm);
"""
density = custom(0, "McCloudDensity", code, {
    "MaskTex": (mask, ""), "Drift": (scalar("DriftX", 0.0, 300), ""), "Bottom": (scalar("BottomZ", 19200.0, 360), ""),
    "Thick": (scalar("ThickZ", 2000.0, 420), ""), "Pos": (position, ""), "Time": (time, ""),
    "Dens": (scalar("Density", 6.0, 480), ""), "Storm": (scalar("StormMc", 0.0, 540), "")}, True)
spread = custom(100, "McCloudExtinction", "return float3(D, D, D);", {"D": (density, "")}, False)
worked = []
for input_name in ("Default", "Deferred", "Forward", "Mobile", "Inputs[0]", "Inputs[1]", "Inputs[2]", "Inputs[3]", ""):
    try:
        if mel.connect_material_expressions(spread, "", switch, input_name):
            worked.append(input_name)
    except Exception as error:
        unreal.log(f"CloudEdit: connect '{input_name}' error {error}")
unreal.log(f"CloudEdit: switch inputs replaced: {worked}")
mel.recompile_material(material)
eal.save_loaded_asset(material)
unreal.log("CloudEdit: done")
