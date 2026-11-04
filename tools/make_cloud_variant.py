# Development: M_McVolumeClouds as the smallest graph (constants) with every pin through a ShadingPathSwitch, as the
# engine's own cloud material does, to find out whether the switch is what keeps our graph from rendering.
import unreal

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
FOLDER = "/Game/Mc/Materials"
name = "M_McVolumeClouds"
if eal.does_asset_exist(f"{FOLDER}/{name}"):
    eal.delete_asset(f"{FOLDER}/{name}")
material = tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)


def switched(value_node, prop, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionShadingPathSwitch, x, y)
    worked = []
    for input_name in ("Default", "Deferred", "Forward", "Mobile", "Inputs[0]", "Inputs[1]", "Inputs[2]", "Inputs[3]", ""):
        try:
            if mel.connect_material_expressions(value_node, "", node, input_name):
                worked.append(input_name)
        except Exception as error:
            unreal.log(f"CloudVariant: connect '{input_name}' error {error}")
    unreal.log(f"CloudVariant: {prop} switch inputs connected: {worked}")
    mel.connect_material_property(node, "", prop)


def const3(r, g, b, x, y):
    node = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, x, y)
    node.set_editor_property("constant", unreal.LinearColor(r, g, b, 1))
    return node


one = mel.create_material_expression(material, unreal.MaterialExpressionConstant, -200, 300)
one.set_editor_property("r", 1.0)
switched(const3(0.3, 0.3, 0.3, -200, 0), unreal.MaterialProperty.MP_SUBSURFACE_COLOR, 0, 0)
switched(const3(1, 1, 1, -200, 100), unreal.MaterialProperty.MP_BASE_COLOR, 0, 100)
switched(const3(0, 0, 0, -200, 200), unreal.MaterialProperty.MP_EMISSIVE_COLOR, 0, 200)
switched(one, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION, 0, 300)
advanced = getattr(unreal, "MaterialExpressionVolumetricAdvancedMaterialOutput", None)
if advanced:
    node = mel.create_material_expression(material, advanced, 300, 0)
    for input_name in ("ConservativeDensity", "Conservative Density", "ConservativeDensity_Input"):
        try:
            unreal.log(f"CloudVariant: advanced '{input_name}' -> {mel.connect_material_expressions(one, '', node, input_name)}")
        except Exception as error:
            unreal.log(f"CloudVariant: advanced '{input_name}' error {error}")
    unreal.log(f"CloudVariant: advanced node props {[p for p in dir(node) if not p.startswith('_')][:60]}")
else:
    unreal.log("CloudVariant: no advanced output class")
mel.recompile_material(material)
eal.save_loaded_asset(material)
unreal.log("CloudVariant: done")
