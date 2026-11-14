// An item's definition (assets/<namespace>/items/<item>.json, 26.x): a tree that picks which model or models draw a stack
// from what the stack and its surroundings are. Node types read: "model", "empty", "composite", "special" (its base model),
// "condition", "select" and "range_dispatch" with the properties that need only the stack (custom_model_data, damage,
// count, has_component, damaged, broken ...) or a fixed answer where the game has no such state here (not using the item,
// not selected, the main hand the right one). A property it doesn't know takes the node's fallback (or on_false).

#pragma once

#include "CoreMinimal.h"

struct FMcItemStack;
class FJsonObject;

namespace McItemDef
{
	// What the model is drawn for: select's "display_context" matches it.
	enum class EContext : uint8
	{
		Gui,
		FirstPersonRightHand,
		Ground,
		Fixed,
	};

	struct FModelRef
	{
		FString Model;
		// The tints of the node, by tint index, as 0xAARRGGBB; a layer or face whose index has none is drawn untinted.
		TArray<uint32> Tints;
	};

	// The model(s) for a stack, in drawing order; empty when the definition says nothing is drawn or there is none.
	MCUI_API TArray<FModelRef> Resolve(const FMcItemStack& Stack, EContext Context);
	MCUI_API TArray<FModelRef> ResolveJson(const TSharedPtr<FJsonObject>& Definition, const FMcItemStack& Stack, EContext Context);

	// Whether the definition, or a model it picks for this stack, comes from a resource pack (so the game's own icon code,
	// which knows only the game's definitions, doesn't apply).
	MCUI_API bool UsesPackModels(const FMcItemStack& Stack);

	// The picture a chest's item draws its chest in, by its "minecraft:special" model of type "minecraft:chest" ("normal",
	// "trapped", "ender", "copper_weathered"... and "christmas" when the "minecraft:local_time" "MM-dd" select matches Now),
	// or empty when the definition draws no chest. OutBase gets that node's base model (its display transforms).
	MCUI_API FString FindChestTexture(const TSharedPtr<FJsonObject>& Definition, const FDateTime& Now, FString* OutBase = nullptr);
}
