// The feature types each file registers with FMcFeatureLibrary::RegisterType. The library calls these once,
// before it parses its first feature.

#pragma once

// simple_block, ore, scattered_ore, the selectors, block_column, disk, spring_feature, lake, freeze_top_layer,
// block_pile, vines, block_blob (McFeatureTypes.cpp).
void McRegisterBasicFeatures();

// vegetation_patch, bamboo, mushrooms, ice, dripstone, geode, monster_room and more (McFeatureExtra.cpp).
void McRegisterExtraFeatures();
// tree (McTreeFeature.cpp).
void McRegisterTreeFeatures();
// template, fossil (McTemplateFeature.cpp).
void McRegisterTemplateFeatures();
void McRegisterWorldFeatures();
