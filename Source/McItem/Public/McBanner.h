// Banners (minecraft.wiki: "Banner"): the colours dyes give them and how a banner's patterns are written down for
// the placed flag, which a client draws from one line of text: "white;s;stripe_downright=red,border=blue" (the base
// colour, s for standing or w for on a wall, then each pattern with its dye).

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

namespace McBanner
{
	struct FLayer
	{
		FString Pattern;
		FString Colour;
	};

	// What a dye of this name ("light_blue") tints a banner to; white for a name that isn't a dye.
	MCITEM_API FLinearColor GetDyeColour(const FString& Name);

	// The layers a banner item carries in "minecraft:banner_patterns".
	MCITEM_API TArray<FLayer> GetLayers(const FMcItemStack& Banner);
	// The colour a banner item is made in, from its name ("minecraft:red_banner" is "red").
	MCITEM_API FString GetBaseColour(FName Item);
	// The other way round: the patterns a placed banner holds, as a component value for its item.
	MCITEM_API TSharedPtr<FJsonValue> MakeComponent(const TArray<FLayer>& Layers);

	MCITEM_API FString Encode(const FString& Base, bool bWall, const TArray<FLayer>& Layers);
	MCITEM_API bool Decode(const FString& Text, FString& OutBase, bool& bOutWall, TArray<FLayer>& OutLayers);
}
