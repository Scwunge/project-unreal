# Development: makes /Game/Mc/Materials/M_McVolumeClouds a plain duplicate of the engine's simple volumetric cloud
# material, to tell whether the black frame comes from our graph or from how the component is set up.
import unreal

eal = unreal.EditorAssetLibrary
target = "/Game/Mc/Materials/M_McVolumeClouds"
if eal.does_asset_exist(target):
    eal.delete_asset(target)
copy = eal.duplicate_asset("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud", target)
unreal.log(f"CloudCopy: {copy}")
if copy:
    eal.save_loaded_asset(copy)
unreal.log("CloudCopy: done")
