# Development: logs the engine's simple volumetric cloud material (domain, blend mode, and what feeds which pin),
# to rebuild Unreal Mode's volume cloud material the same way.
import unreal

mel = unreal.MaterialEditingLibrary
path = "/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud"
material = unreal.load_asset(path)
unreal.log(f"CloudInspect: {path} -> {material}")
if material:
    for prop in ("material_domain", "blend_mode", "shading_model", "two_sided"):
        try:
            unreal.log(f"CloudInspect: {prop} = {material.get_editor_property(prop)}")
        except Exception as error:
            unreal.log(f"CloudInspect: {prop} unreadable: {error}")
    for member in dir(unreal.MaterialProperty):
        if not member.startswith("MP_"):
            continue
        try:
            node = mel.get_material_property_input_node(material, getattr(unreal.MaterialProperty, member))
            if node:
                out = mel.get_material_property_input_node_output_name(material, getattr(unreal.MaterialProperty, member))
                unreal.log(f"CloudInspect: pin {member} <- {node.get_name()} ({node.get_class().get_name()}) output '{out}'")
                for inp in mel.get_inputs_for_material_expression(material, node):
                    unreal.log(f"CloudInspect:    input of {node.get_name()}: {inp.get_name() if inp else None} ({inp.get_class().get_name() if inp else ''})")
        except Exception as error:
            unreal.log(f"CloudInspect: pin {member} error {error}")
    unreal.log(f"CloudInspect: textures {mel.get_used_textures(material) if hasattr(mel, 'get_used_textures') else '?'}")
    unreal.log(f"CloudInspect: expressions {mel.get_num_material_expressions(material)}")
unreal.log("CloudInspect: done")
