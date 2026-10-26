// The feature types each file registers with FMcFeatureLibrary::RegisterType. The library calls these once,
// before it parses its first feature.

#pragma once

// simple_block, ore, scattered_ore, the selectors, block_column, disk, spring_feature, lake, freeze_top_layer,
// block_pile, vines, block_blob (McFeatureTypes.cpp).
void McRegisterBasicFeatures();
