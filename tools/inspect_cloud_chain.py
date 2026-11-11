# Development: logs the chain of nodes behind each pin of the engine's simple volumetric cloud material, to find the
# node that makes its density, so a copy can have that node replaced by ours.
import unreal

mel = unreal.MaterialEditingLibrary
material = unreal.load_asset("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud")
seen = set()


def describe(node):
    extra = ""
    for prop in ("parameter_name", "r", "code", "constant", "texture", "default_value", "function"):
        try:
            value = node.get_editor_property(prop)
            text = str(value).replace("\n", " ")[:160]
            extra += f" {prop}={text}"
        except Exception:
            pass
    return f"{node.get_name()} ({node.get_class().get_name()}){extra}"


def walk(node, depth):
    key = node.get_name()
    if key in seen:
        unreal.log(f"CloudChain: {'  ' * depth}{key} (seen)")
        return
    seen.add(key)
    unreal.log(f"CloudChain: {'  ' * depth}{describe(node)}")
    if depth >= 7:
        return
    for inp in mel.get_inputs_for_material_expression(material, node):
        if inp:
            walk(inp, depth + 1)


for name in ("MP_SUBSURFACE_COLOR", "MP_BASE_COLOR", "MP_EMISSIVE_COLOR", "MP_AMBIENT_OCCLUSION"):
    node = mel.get_material_property_input_node(material, getattr(unreal.MaterialProperty, name))
    unreal.log(f"CloudChain: PIN {name} <- {node.get_name() if node else None}")
    if node:
        walk(node, 1)
unreal.log("CloudChain: done")
