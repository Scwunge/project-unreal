// How likely a composter is to take a level from an item put in it (percent), 0 if it takes nothing.
//
// Measured with the reference client (tools/refscripts/gen_compost_all.py and gen_compost3: every item in the registry put
// into a composter by a hopper, then 320 of each compostable item; chance = levels gained / items used). The measured chances
// fall into five groups, about 0.30, 0.52, 0.69, 0.86 and 1.00 (the estimate runs a little high); they are rounded here to the
// 30, 50, 65, 85 and 100 percent the wiki's composter page also lists. Items not in the table are not compostable.

#include "McCompost.h"

namespace
{
	struct FGroup { int32 Percent; TArray<const TCHAR*> Items; };
}

int32 McCompostChance(FName Item)
{
	static const TMap<FName, int32> Table = []
	{
		const FGroup Groups[] = {
			{ 30, {
				TEXT("beetroot_seeds"), TEXT("oak_sapling"), TEXT("pink_petals"), TEXT("pale_oak_leaves"), TEXT("poplar_sapling"),
				TEXT("mangrove_roots"), TEXT("acacia_leaves"), TEXT("acacia_sapling"), TEXT("jungle_sapling"),
				TEXT("spruce_sapling"), TEXT("melon_seeds"), TEXT("pitcher_pod"), TEXT("pumpkin_seeds"), TEXT("birch_sapling"),
				TEXT("cherry_leaves"), TEXT("dark_oak_leaves"), TEXT("mangrove_leaves"), TEXT("red_shrub"), TEXT("sweet_berries"),
				TEXT("mangrove_propagule"), TEXT("kelp"), TEXT("cactus_flower"), TEXT("pale_hanging_moss"),
				TEXT("short_dry_grass"), TEXT("bush"), TEXT("firefly_bush"), TEXT("moss_carpet"), TEXT("seagrass"),
				TEXT("wildflowers"), TEXT("jungle_leaves"), TEXT("torchflower_seeds"), TEXT("birch_leaves"), TEXT("short_grass"),
				TEXT("small_dripleaf"), TEXT("spruce_leaves"), TEXT("red_poplar_leaves"), TEXT("dried_kelp"), TEXT("wheat_seeds"),
				TEXT("leaf_litter"), TEXT("tall_dry_grass"), TEXT("azalea_leaves"), TEXT("cherry_sapling"),
				TEXT("dark_oak_sapling"), TEXT("hanging_roots"), TEXT("yellow_poplar_leaves"), TEXT("glow_berries"),
				TEXT("pale_moss_carpet"), TEXT("pale_oak_sapling"), TEXT("oak_leaves"), TEXT("orange_poplar_leaves"),
			} },
			{ 50, {
				TEXT("sugar_cane"), TEXT("vine"), TEXT("melon_slice"), TEXT("cactus"), TEXT("flowering_azalea_leaves"),
				TEXT("twisting_vines"), TEXT("nether_sprouts"), TEXT("weeping_vines"), TEXT("dried_kelp_block"),
				TEXT("tall_grass"), TEXT("glow_lichen"),
			} },
			{ 65, {
				TEXT("red_mushroom"), TEXT("melon"), TEXT("orange_tulip"), TEXT("large_fern"), TEXT("beetroot"), TEXT("potato"),
				TEXT("shelf_mushroom"), TEXT("shroomlight"), TEXT("closed_eyeblossom"), TEXT("azalea"), TEXT("blue_orchid"),
				TEXT("lily_of_the_valley"), TEXT("pumpkin"), TEXT("lily_pad"), TEXT("apple"), TEXT("nether_wart"),
				TEXT("oxeye_daisy"), TEXT("wheat"), TEXT("rose_bush"), TEXT("crimson_roots"), TEXT("cornflower"),
				TEXT("crimson_fungus"), TEXT("allium"), TEXT("carrot"), TEXT("dandelion"), TEXT("fern"), TEXT("peony"),
				TEXT("pink_tulip"), TEXT("sea_pickle"), TEXT("wither_rose"), TEXT("big_dripleaf"), TEXT("cocoa_beans"),
				TEXT("red_tulip"), TEXT("moss_block"), TEXT("azure_bluet"), TEXT("white_tulip"), TEXT("open_eyeblossom"),
				TEXT("pale_moss_block"), TEXT("poppy"), TEXT("spore_blossom"), TEXT("sunflower"), TEXT("warped_roots"),
				TEXT("brown_mushroom"), TEXT("lilac"), TEXT("mushroom_stem"), TEXT("warped_fungus"), TEXT("carved_pumpkin"),
			} },
			{ 85, {
				TEXT("red_mushroom_block"), TEXT("straw_bed"), TEXT("warped_wart_block"), TEXT("hay_block"), TEXT("torchflower"),
				TEXT("baked_potato"), TEXT("bread"), TEXT("cookie"), TEXT("pitcher_plant"), TEXT("flowering_azalea"),
				TEXT("nether_wart_block"), TEXT("brown_mushroom_block"),
			} },
			{ 100, {
				TEXT("cake"), TEXT("pumpkin_pie"),
			} },
		};
		TMap<FName, int32> Map;
		for (const FGroup& Group : Groups)
		{
			for (const TCHAR* Item : Group.Items)
			{
				Map.Add(FName(*(FString(TEXT("minecraft:")) + Item)), Group.Percent);
			}
		}
		return Map;
	}();
	const int32* Found = Table.Find(Item);
	return Found ? *Found : 0;
}
