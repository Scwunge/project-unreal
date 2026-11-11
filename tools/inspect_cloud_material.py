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
        if not member.startswith("MP_") or member in ("MP_MAX", "MP_LAST_CUSTOMIZED_U_VS", "MP_FRONT_MATERIAL", "MP_MATERIAL_ATTRIBUTES"):
            continue
        unreal.log(f"CloudInspect: trying {member}")
        try:
            node = mel.get_material_property_input_node(material, getattr(unreal.MaterialProperty, member))
            if node:
                out = mel.get_material_property_input_node_output_name(material, getattr(unreal.MaterialProperty, member))
                unreal.log(f"CloudInspect: pin {member} <- {node.get_name()} ({node.get_class().get_name()}) output '{out}'")
                for inp in mel.get_inputs_for_material_expression(material, node):
                    unreal.log(f"CloudInspect:    input of {node.get_name()}: {inp.get_name() if inp else None} ({inp.get_class().get_name() if inp else ''})")
        except Exception as error:
            unreal.log(f"CloudInspect: pin {member} error {error}")
    for name in ("MP_SUBSURFACE_COLOR", "MP_OPACITY", "MP_OPACITY_MASK", "MP_REFRACTION", "MP_METALLIC", "MP_SPECULAR", "MP_ROUGHNESS", "MP_NORMAL", "MP_WORLD_POSITION_OFFSET", "MP_CUSTOMIZED_UV0", "MP_PIXEL_DEPTH_OFFSET", "MP_SHADING_MODEL"):
        prop = getattr(unreal.MaterialProperty, name, None)
        try:
            node = mel.get_material_property_input_node(material, prop) if prop is not None else "no such enum"
            unreal.log(f"CloudInspect: named {name} <- {node}")
        except Exception as error:
            unreal.log(f"CloudInspect: named {name} error {error}")
    try:
        collection = material.get_editor_property("expression_collection")
        for expression in collection.get_editor_property("expressions"):
            unreal.log(f"CloudInspect: expr {expression.get_name()} ({expression.get_class().get_name()}) {expression.get_editor_property('desc') if hasattr(expression, 'desc') else ''}")
    except Exception as error:
        unreal.log(f"CloudInspect: expressions unreadable: {error}")
    unreal.log(f"CloudInspect: textures {mel.get_used_textures(material) if hasattr(mel, 'get_used_textures') else '?'}")
    unreal.log(f"CloudInspect: expressions {mel.get_num_material_expressions(material)}")
unreal.log("CloudInspect: done")
