#include "McHUD.h"
#include "McMapStore.h"
#include "McFontRaster.h"
#include "McBanner.h"
#include "McGuiLayout.h"
#include "McBlockModels.h"
#include "McTextureTiles.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "McItemDefinition.h"
#include "McItemStack.h"
#include "McChunk.h"
#include "McEntityModel.h"
#include "McPlayerModel.h"
#include "McItemFrames.h"
#include "McPlayerPawn.h"
#include "McPot.h"
#include "McSectionMesher.h"
#include "McUI.h"
#include "Dom/JsonObject.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	// Hotbar layout in GUI pixels, from the sprite sizes: 182 x 22, slots 20 apart, items inset 3.
	constexpr float HotbarWidth = 182.0f;
	constexpr float HotbarHeight = 22.0f;
	constexpr float SlotSpacing = 20.0f;
	constexpr float CrosshairSize = 15.0f;

	// Block items are drawn as their block model turned 225 degrees about y, tilted 30 about x and scaled to
	// 0.625: the inventory view (the "gui" display transform of minecraft:block/block).
	FVector3d InventoryView(const FVector3f& P)
	{
		const double Yaw = FMath::DegreesToRadians(225.0);
		const double Tilt = FMath::DegreesToRadians(30.0);
		const FVector3d C = FVector3d(P) - FVector3d(0.5);
		const double X = C.X * FMath::Cos(Yaw) + C.Z * FMath::Sin(Yaw);
		const double Z = -C.X * FMath::Sin(Yaw) + C.Z * FMath::Cos(Yaw);
		const double Y = C.Y * FMath::Cos(Tilt) - Z * FMath::Sin(Tilt);
		const double Depth = C.Y * FMath::Sin(Tilt) + Z * FMath::Cos(Tilt);
		return FVector3d(X, Y, Depth);
	}

	UTexture2D* MakeTexture(const TArray64<uint8>& Bgra, int32 Width, int32 Height, bool bSmooth = false, bool bClamp = false)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}
		Texture->Filter = bSmooth ? TF_Bilinear : TF_Nearest;
		if (bClamp)
		{
			Texture->AddressX = TA_Clamp;
			Texture->AddressY = TA_Clamp;
		}
		Texture->SRGB = !bSmooth;
		Texture->LODGroup = TEXTUREGROUP_Pixels2D;
		void* Data = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Bgra.GetData(), static_cast<SIZE_T>(Width) * Height * 4);
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	UTexture2D* LoadPngBytes(const TArray<uint8>& File, const FString& Path, bool bFirstFrame, bool bGreyToAlpha);

	// A PNG from the jar, cut to its top square when it is an animation strip.
	UTexture2D* LoadPng(const FString& Path, bool bFirstFrame, bool bGreyToAlpha = false)
	{
		TArray<uint8> File;
		if (!FFileHelper::LoadFileToArray(File, *Path))
		{
			UE_LOG(LogMcUI, Warning, TEXT("Can't read %s"), *Path);
			return nullptr;
		}
		return LoadPngBytes(File, Path, bFirstFrame, bGreyToAlpha);
	}

	// The same from bytes already in memory (Path only names it in the log).
	UTexture2D* LoadPngBytes(const TArray<uint8>& File, const FString& Path, bool bFirstFrame, bool bGreyToAlpha)
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		TArray64<uint8> Raw;
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num()) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			UE_LOG(LogMcUI, Warning, TEXT("Can't decode %s"), *Path);
			return nullptr;
		}
		const int32 Width = Wrapper->GetWidth();
		int32 Height = Wrapper->GetHeight();
		if (bFirstFrame && Height > Width)
		{
			Height = Width;
			Raw.SetNum(static_cast<int64>(Width) * Height * 4);
		}
		if (bGreyToAlpha)
		{
			// Black with the picture's grey as its opacity (the screen-edge darkening: how much of the picture is lost).
			for (int64 Pixel = 0; Pixel < static_cast<int64>(Width) * Height; ++Pixel)
			{
				uint8* Bgra = Raw.GetData() + Pixel * 4;
				Bgra[3] = Bgra[0];
				Bgra[0] = Bgra[1] = Bgra[2] = 0;
			}
		}
		// The picture's own .png.mcmeta may ask for smooth filtering or clamping.
		const McCache::FTextureMeta Meta = McCache::ReadTextureMeta(Path);
		return MakeTexture(Raw, Width, Height, bGreyToAlpha || Meta.bBlur, Meta.bHasClamp && Meta.bClamp);
	}
}

int32 AMcHUD::GetGuiScale(int32 Width, int32 Height)
{
	return McGui::GetGuiScale(Width, Height);
}

FString AMcHUD::SpritePath(const FString& RelativeAssetPath)
{
	// "resources:" pictures come with the sounds, from the launcher's asset index, rather than in the jar (the title
	// panorama: the jar's is a 1 by 1 stand-in). A pack that has the same picture still replaces it.
	if (RelativeAssetPath.StartsWith(TEXT("resources:")))
	{
		const FString Relative = RelativeAssetPath.Mid(10);
		return McPacks::IsFromPack(Relative) ? McCache::Asset(Relative)
			: FPaths::Combine(McCache::Root(), TEXT("assets/minecraft"), Relative);
	}
	return McCache::Asset(RelativeAssetPath);
}

UTexture2D* AMcHUD::LoadSprite(const FString& RelativeAssetPath)
{
	if (TObjectPtr<UTexture2D>* Found = Sprites.Find(RelativeAssetPath))
	{
		return *Found;
	}
	const FString Path = SpritePath(RelativeAssetPath);
	UTexture2D* Texture = LoadPng(Path, false);
	Sprites.Add(RelativeAssetPath, Texture);
	return Texture;
}

UTexture2D* AMcHUD::LoadPackIcon(const FString& Key, const TArray<uint8>& Png)
{
	if (TObjectPtr<UTexture2D>* Found = Sprites.Find(Key))
	{
		return *Found;
	}
	UTexture2D* Texture = LoadPngBytes(Png, Key, false, false);
	Sprites.Add(Key, Texture);
	return Texture;
}

UTexture2D* AMcHUD::MakeGlyphTexture(const FMcRasterGlyph& Glyph)
{
	TArray64<uint8> Bgra;
	Bgra.Append(Glyph.Bgra.GetData(), Glyph.Bgra.Num());
	UTexture2D* Texture = MakeTexture(Bgra, Glyph.Width, Glyph.Height);
	if (Texture)
	{
		GlyphTextures.Add(Texture);
	}
	return Texture;
}

UTexture2D* AMcHUD::LoadVignette()
{
	const FString Key = TEXT("vignette:alpha");
	if (TObjectPtr<UTexture2D>* Found = Sprites.Find(Key))
	{
		return *Found;
	}
	UTexture2D* Texture = LoadPng(McCache::Asset(TEXT("textures/misc/vignette.png")), false, true);
	Sprites.Add(Key, Texture);
	return Texture;
}

UTexture2D* AMcHUD::GetLayerTexture(FName TextureId)
{
	if (TObjectPtr<UTexture2D>* Found = LayerTextures.Find(TextureId))
	{
		return *Found;
	}
	// "minecraft:block/stone" -> textures/block/stone.png
	UTexture2D* Texture = LoadPng(McCache::AssetOf(TextureId.ToString(), TEXT("textures/"), TEXT(".png")), true);
	LayerTextures.Add(TextureId, Texture);
	return Texture;
}

const AMcHUD::FIcon& AMcHUD::GetIcon(FMcStateId State)
{
	if (const FIcon* Found = Icons.Find(State))
	{
		return *Found;
	}
	FIcon Icon;
	const FString Name = McCache::PathOf(FMcBlockRegistry::Get().GetBlockOf(State).Name.ToString());
	if (!BuildFlatIcon(Name, Icon))
	{
		if (!FMcBlockModels::Get().IsBaked())
		{
			// The HUD draws before the world's models are baked: try again next frame rather than keep nothing.
			static const FIcon NotYet;
			return NotYet;
		}
		BuildBlockIcon(State, Icon);
	}
	return Icons.Add(State, MoveTemp(Icon));
}

const AMcHUD::FIcon& AMcHUD::GetItemIcon(FName Item)
{
	// The compass and the clock show the picture of the moment.
	Item = McItemFrames::Resolve(Item);
	if (const FIcon* Found = ItemIcons.Find(Item))
	{
		return *Found;
	}
	FIcon Icon;
	const FString Name = McCache::PathOf(Item.ToString());
	if (!BuildHeadIcon(Item, Icon, Item == TEXT("minecraft:decorated_pot") ? McPot::GetSides(FMcItemStack(Item)) : TArray<FName>())
		&& !BuildFlatIcon(Name, Icon))
	{
		// A block's item is drawn as the block; anything else we can't read the model of falls back on the sprite
		// of the same name, if the jar has one.
		const TOptional<FMcStateId> State = FMcBlockRegistry::Get().FindDefaultState(Item);
		if (State.IsSet() && !FMcBlockModels::Get().IsBaked())
		{
			// Not yet: see GetIcon.
			static const FIcon NotYet;
			return NotYet;
		}
		if (State.IsSet())
		{
			BuildBlockIcon(State.GetValue(), Icon);
		}
		if (Icon.Triangles.Num() == 0 && FPaths::FileExists(McCache::Asset(TEXT("textures/item/") + Name + TEXT(".png"))))
		{
			BuildFlatQuad(TEXT("minecraft:item/") + Name, Icon);
		}
	}
	return ItemIcons.Add(Item, MoveTemp(Icon));
}

const AMcHUD::FIcon& AMcHUD::GetPotIcon(const FMcItemStack& Pot)
{
	const TArray<FName> Sides = McPot::GetSides(Pot);
	TArray<FString> Names;
	for (const FName Side : Sides)
	{
		Names.Add(Side.ToString());
	}
	const FString Key = FString::Join(Names, TEXT(","));
	if (const FIcon* Found = PotIcons.Find(Key))
	{
		return *Found;
	}
	FIcon Icon;
	BuildHeadIcon(Pot.Item, Icon, Sides);
	return PotIcons.Add(Key, MoveTemp(Icon));
}

const AMcHUD::FIcon& AMcHUD::GetBannerIcon(const FMcItemStack& Banner)
{
	FString Base = McCache::PathOf(Banner.Item.ToString());
	Base.RemoveFromEnd(TEXT("_banner"));
	FString Key = Base;
	TArray<TPair<FString, FString>> BannerLayers;
	const TSharedPtr<FJsonValue> Value = Banner.Get(TEXT("minecraft:banner_patterns"));
	if (Value.IsValid() && Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
		{
			const TSharedPtr<FJsonObject>* Object;
			FString Pattern, Colour;
			if (Entry->TryGetObject(Object) && (*Object)->TryGetStringField(TEXT("pattern"), Pattern) && (*Object)->TryGetStringField(TEXT("color"), Colour))
			{
				BannerLayers.Emplace(McCache::PathOf(Pattern), Colour);
				Key += TEXT("|") + Pattern + TEXT(":") + Colour;
			}
		}
	}
	if (const FIcon* Found = BannerIcons.Find(Key))
	{
		return *Found;
	}
	FIcon Icon;
	// The flag's front is 20 by 40 pixels of the 64 square sheet, from (1, 1); drawn 8 by 16 in the slot.
	const auto AddLayer = [&](const FString& TextureId, const FLinearColor& Colour)
	{
		UTexture2D* Texture = GetLayerTexture(FName(*McCache::Namespaced(TextureId)));
		const FVector2D Corners[4] = { FVector2D(4, 0), FVector2D(12, 0), FVector2D(12, 16), FVector2D(4, 16) };
		const FVector2D UVs[4] = { FVector2D(1.0 / 64, 1.0 / 64), FVector2D(21.0 / 64, 1.0 / 64), FVector2D(21.0 / 64, 41.0 / 64), FVector2D(1.0 / 64, 41.0 / 64) };
		for (const int32 First : { 0, 2 })
		{
			FIconTriangle& Triangle = Icon.Triangles.AddDefaulted_GetRef();
			const int32 Indices[3] = { 0, First == 0 ? 1 : 2, First == 0 ? 2 : 3 };
			for (int32 I = 0; I < 3; ++I)
			{
				Triangle.Positions[I] = Corners[Indices[I]];
				Triangle.UVs[I] = UVs[Indices[I]];
			}
			Triangle.Color = Colour;
			Triangle.Texture = Texture;
		}
	};
	AddLayer(TEXT("entity/banner/base"), McBanner::GetDyeColour(Base));
	for (const TPair<FString, FString>& Layer : BannerLayers)
	{
		AddLayer(TEXT("entity/banner/") + Layer.Key, McBanner::GetDyeColour(Layer.Value));
	}
	return BannerIcons.Add(Key, MoveTemp(Icon));
}

const AMcHUD::FIcon& AMcHUD::GetShieldIcon(const FMcItemStack& Shield)
{
	// A shield with no base colour shows the bare wooden face; one with a colour, the tinted base.
	FString Colour;
	if (const TSharedPtr<FJsonValue> Value = Shield.Get(TEXT("minecraft:base_color")); Value.IsValid())
	{
		Value->TryGetString(Colour);
	}
	const FString Key = TEXT("shield|") + Colour;
	if (const FIcon* Found = StackIcons.Find(Key))
	{
		return *Found;
	}
	FIcon Icon;
	UTexture2D* Texture = GetLayerTexture(FName(Colour.IsEmpty() ? TEXT("minecraft:entity/shield/shield_base_nopattern") : TEXT("minecraft:entity/shield/shield_base")));
	// Corners measured on the vanilla slot (top left, top right, bottom right, bottom left), in GUI pixels; the front
	// plate is the 13 by 23 texels at the corner of the 64 square sheet.
	const FVector2D Corners[4] = { FVector2D(3.3, 0.4), FVector2D(10.6, 1.8), FVector2D(9.4, 15.6), FVector2D(2.1, 13.9) };
	const FVector2D UVs[4] = { FVector2D(0, 0), FVector2D(13.0 / 64, 0), FVector2D(13.0 / 64, 23.0 / 64), FVector2D(0, 23.0 / 64) };
	const FLinearColor Tint = Colour.IsEmpty() ? FLinearColor::White : McBanner::GetDyeColour(Colour);
	for (const int32 First : { 0, 2 })
	{
		FIconTriangle& Triangle = Icon.Triangles.AddDefaulted_GetRef();
		const int32 Indices[3] = { 0, First == 0 ? 1 : 2, First == 0 ? 2 : 3 };
		for (int32 I = 0; I < 3; ++I)
		{
			Triangle.Positions[I] = Corners[Indices[I]];
			Triangle.UVs[I] = UVs[Indices[I]];
		}
		Triangle.Color = Tint;
		Triangle.Texture = Texture;
	}
	return StackIcons.Add(Key, MoveTemp(Icon));
}

void AMcHUD::BuildFlatQuad(const FString& TextureId, FIcon& Icon, const FLinearColor& Tint)
{
	UTexture2D* Texture = GetLayerTexture(FName(*McCache::Namespaced(TextureId)));
	const FVector2D Corners[4] = { FVector2D(0, 0), FVector2D(16, 0), FVector2D(16, 16), FVector2D(0, 16) };
	const FVector2D UVs[4] = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
	for (const int32 First : { 0, 2 })
	{
		FIconTriangle& Triangle = Icon.Triangles.AddDefaulted_GetRef();
		const int32 Indices[3] = { 0, First == 0 ? 1 : 2, First == 0 ? 2 : 3 };
		for (int32 I = 0; I < 3; ++I)
		{
			Triangle.Positions[I] = Corners[Indices[I]];
			Triangle.UVs[I] = UVs[Indices[I]];
		}
		Triangle.Color = Tint;
		Triangle.Texture = Texture;
	}
}

// The item definition says whether the item is drawn flat (a "layer0" sprite) or as its block model.
bool AMcHUD::BuildFlatIcon(const FString& Name, FIcon& Icon)
{
	FString FlatTexture;
	if (const TSharedPtr<FJsonObject> Item = McCache::LoadJsonObject(McCache::Asset(TEXT("items/") + Name + TEXT(".json"))))
	{
		const TSharedPtr<FJsonObject>* Model;
		FString ModelId;
		if (Item->TryGetObjectField(TEXT("model"), Model) && (*Model)->TryGetStringField(TEXT("model"), ModelId) && McCache::PathOf(ModelId).StartsWith(TEXT("item/")))
		{
			if (const TSharedPtr<FJsonObject> ItemModel = McCache::LoadJsonObject(McCache::AssetOf(ModelId, TEXT("models/"), TEXT(".json"))))
			{
				const TSharedPtr<FJsonObject>* Textures;
				if (ItemModel->TryGetObjectField(TEXT("textures"), Textures))
				{
					(*Textures)->TryGetStringField(TEXT("layer0"), FlatTexture);
				}
			}
		}
	}
	if (FlatTexture.IsEmpty())
	{
		return false;
	}
	BuildFlatQuad(FlatTexture, Icon);
	return true;
}

bool AMcHUD::BuildHeadIcon(FName Item, FIcon& Icon, const TArray<FName>& PotSides)
{
	float WornScale;
	const bool bPot = Item == TEXT("minecraft:decorated_pot");
	const FName Kind = bPot ? FName(TEXT("mc:decorated_pot")) : McPlayerModel::GetWornHeadModel(Item, WornScale);
	const McEntityModel::FModel* Model = bPot || Kind.ToString().StartsWith(TEXT("mc:head/")) ? McEntityModel::Find(Kind) : nullptr;
	if (!Model || Model->Textures.IsEmpty())
	{
		return false;
	}
	// Each layer of the model with its picture: a head has the one; a pot its body and then its sides front, right, back
	// and left (McEntityModel), each the plain side or its sherd's pattern.
	struct FLayerMesh
	{
		McPlayerModel::FMeshData Mesh;
		UTexture2D* Texture = nullptr;
	};
	TArray<FLayerMesh> LayerMeshes;
	for (int32 Layer = 0; Layer < (bPot ? Model->Textures.Num() : 1); ++Layer)
	{
		FLayerMesh& Built = LayerMeshes.AddDefaulted_GetRef();
		McEntityModel::BuildMesh(*Model, McEntityModel::FAnim(), Layer, Built.Mesh);
		static const int32 PotSide[4] = { 3, 2, 0, 1 };
		const FString Picture = bPot && Layer >= 1 && PotSides.IsValidIndex(PotSide[(Layer - 1) & 3]) ? McPot::GetPattern(PotSides[PotSide[(Layer - 1) & 3]])
			: TEXT("entity/") + FPaths::GetBaseFilename(Model->Textures[Layer], false);
		Built.Texture = GetLayerTexture(FName(TEXT("minecraft:") + Picture));
	}
	// The item's base model's "gui" display (template_skull: turned 45 and tipped 30 about the block's middle, lifted 3
	// pixels, at full size where a block's item is 0.625; the dragon's at 0.6, 2 pixels left), found up its parents. The
	// head stands on the middle of the block's floor, its face south, which this turn puts on the right, as vanilla's
	// hotbar shows (ref_heads_hotbar).
	FVector3d Rotation(30.0, 45.0, 0.0), Translation(0.0, 3.0, 0.0), Scaling(1.0);
	{
		FString ModelId;
		const TSharedPtr<FJsonObject> Definition = McCache::LoadJsonObject(McCache::Asset(TEXT("items/") + McCache::PathOf(Item.ToString()) + TEXT(".json")));
		const TSharedPtr<FJsonObject>* Special;
		if (Definition && Definition->TryGetObjectField(TEXT("model"), Special))
		{
			(*Special)->TryGetStringField(TEXT("base"), ModelId);
		}
		for (int32 Depth = 0; Depth < 8 && !ModelId.IsEmpty(); ++Depth)
		{
			const TSharedPtr<FJsonObject> ItemModel = McCache::LoadJsonObject(McCache::AssetOf(ModelId, TEXT("models/"), TEXT(".json")));
			const TSharedPtr<FJsonObject>* Display;
			const TSharedPtr<FJsonObject>* Gui;
			if (ItemModel && ItemModel->TryGetObjectField(TEXT("display"), Display) && (*Display)->TryGetObjectField(TEXT("gui"), Gui))
			{
				const auto Read = [&](const TCHAR* Field, FVector3d& Out)
				{
					const TArray<TSharedPtr<FJsonValue>>* Values;
					if ((*Gui)->TryGetArrayField(Field, Values) && Values->Num() == 3)
					{
						Out = FVector3d((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
					}
				};
				Read(TEXT("rotation"), Rotation);
				Read(TEXT("translation"), Translation);
				Read(TEXT("scale"), Scaling);
				break;
			}
			ModelId.Reset();
			if (ItemModel)
			{
				ItemModel->TryGetStringField(TEXT("parent"), ModelId);
			}
		}
	}
	const double Yaw = FMath::DegreesToRadians(Rotation.Y);
	const double Tilt = FMath::DegreesToRadians(Rotation.X);
	const auto View = [&](const FVector& Unreal)
	{
		const FVector3d C = (FVector3d(Unreal.X, Unreal.Z, Unreal.Y) / McCoords::UnitsPerBlock + FVector3d(0.0, -0.5, 0.0)) * Scaling;
		const double X = C.X * FMath::Cos(Yaw) + C.Z * FMath::Sin(Yaw);
		const double Z = -C.X * FMath::Sin(Yaw) + C.Z * FMath::Cos(Yaw);
		const double Y = C.Y * FMath::Cos(Tilt) - Z * FMath::Sin(Tilt);
		const double Depth = C.Y * FMath::Sin(Tilt) + Z * FMath::Cos(Tilt);
		return FVector3d(X + Translation.X / 16.0, Y + Translation.Y / 16.0, Depth);
	};
	struct FProjected
	{
		double Depth;
		FIconTriangle Triangle;
	};
	TArray<FProjected> Triangles;
	for (const FLayerMesh& Built : LayerMeshes)
	{
		const McPlayerModel::FMeshData& Mesh = Built.Mesh;
		UTexture2D* const Texture = Built.Texture;
		for (int32 First = 0; First + 2 < Mesh.Triangles.Num(); First += 3)
		{
			FVector3d P[3];
			for (int32 I = 0; I < 3; ++I)
			{
				P[I] = View(Mesh.Positions[Mesh.Triangles[First + I]]);
			}
			// Faces towards the viewer only, by the mesh's own normals (turned as the points are, without the lift).
			const FVector3d N = (View(Mesh.Normals[Mesh.Triangles[First]] * McCoords::UnitsPerBlock) - View(FVector::ZeroVector)).GetSafeNormal();
			if (N.Z <= 1e-6)
			{
				continue;
			}
			const float Shade = N.Y > 0.7 ? 1.0f : N.Y < -0.7 ? 0.5f : (N.X < 0.0 ? 0.8f : 0.6f);
			FProjected& Projected = Triangles.AddDefaulted_GetRef();
			Projected.Depth = (P[0].Z + P[1].Z + P[2].Z) / 3.0;
			for (int32 I = 0; I < 3; ++I)
			{
				Projected.Triangle.Positions[I] = FVector2D(8.0 + P[I].X * 16.0, 8.0 - P[I].Y * 16.0);
				Projected.Triangle.UVs[I] = Mesh.UV0[Mesh.Triangles[First + I]];
			}
			Projected.Triangle.Color = FLinearColor(Shade, Shade, Shade);
			Projected.Triangle.Texture = Texture;
		}
	}
	if (Triangles.IsEmpty())
	{
		return false;
	}
	// Far faces first, so a hat layer lies over the head.
	Triangles.StableSort([](const FProjected& A, const FProjected& B) { return A.Depth < B.Depth; });
	for (const FProjected& Projected : Triangles)
	{
		Icon.Triangles.Add(Projected.Triangle);
	}
	return true;
}

void AMcHUD::BuildBlockIcon(FMcStateId State, FIcon& Icon)
{
	const FMcBlockModels& Models = FMcBlockModels::Get();
	if (!Models.IsBaked() || State >= Models.NumStates())
	{
		return;
	}
	const FColor Tint = McBlockMesher::GetBlockTint(FMcBlockRegistry::Get(), State);

	struct FProjected
	{
		double Depth;
		FIconTriangle Triangles[2];
	};
	TArray<FProjected> Quads;
	for (const FMcBakedQuad& Quad : Models.GetModel(State).Quads)
	{
		FVector3d P[4];
		for (int32 I = 0; I < 4; ++I)
		{
			P[I] = InventoryView(Quad.Positions[I]);
		}
		// Only faces turned towards the viewer (+depth); the quads wind counter-clockwise seen from outside.
		const FVector3d Normal = FVector3d::CrossProduct(P[1] - P[0], P[2] - P[0]);
		if (Normal.Z <= 1e-6)
		{
			continue;
		}
		// Inventory lighting: tops full, the left face brighter than the right.
		const FVector3d N = Normal.GetSafeNormal();
		const float Shade = N.Y > 0.7 ? 1.0f : N.Y < -0.7 ? 0.5f : (N.X < 0.0 ? 0.8f : 0.6f);
		FLinearColor Color = FLinearColor(Shade, Shade, Shade);
		if (Quad.TintIndex >= 0)
		{
			Color *= FLinearColor(Tint.R / 255.0f, Tint.G / 255.0f, Tint.B / 255.0f);
		}
		// 0.625 of a 16-pixel slot per block, centred, screen y down.
		auto ToSlot = [](const FVector3d& V) { return FVector2D(8.0 + V.X * 10.0, 8.0 - V.Y * 10.0); };

		FProjected& Projected = Quads.AddDefaulted_GetRef();
		Projected.Depth = (P[0].Z + P[1].Z + P[2].Z + P[3].Z) * 0.25;
		// A tile of a big picture (McTextureTiles.h) is drawn from the whole picture, its UVs moved onto the tile.
		FName Picture = Quad.Texture;
		FIntPoint Tile(0, 0), Grid(1, 1);
		McTextureTiles::ParseTileId(Quad.Texture, Picture, Tile, Grid);
		UTexture2D* Texture = GetLayerTexture(Picture);
		const int32 Corners[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
		for (int32 T = 0; T < 2; ++T)
		{
			FIconTriangle& Triangle = Projected.Triangles[T];
			for (int32 I = 0; I < 3; ++I)
			{
				Triangle.Positions[I] = ToSlot(P[Corners[T][I]]);
				Triangle.UVs[I] = (FVector2D(Quad.UVs[Corners[T][I]]) + FVector2D(Tile)) / FVector2D(Grid);
			}
			Triangle.Color = Color;
			Triangle.Texture = Texture;
		}
	}
	// Far faces first.
	Quads.Sort([](const FProjected& A, const FProjected& B) { return A.Depth < B.Depth; });
	for (const FProjected& Quad : Quads)
	{
		Icon.Triangles.Add(Quad.Triangles[0]);
		Icon.Triangles.Add(Quad.Triangles[1]);
	}
}

UTexture2D* AMcHUD::GetMapTexture(const FMcItemStack& Stack)
{
	GetMapIcon(Stack);
	const TObjectPtr<UTexture2D>* Found = MapTextures.Find(McMapStore::GetId(Stack));
	return Found ? Found->Get() : nullptr;
}

void AMcHUD::DrawTurnedSprite(UTexture2D* Texture, float CentreX, float CentreY, float Size, float Degrees)
{
	if (!Texture || !Texture->GetResource())
	{
		return;
	}
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Degrees));
	const FVector2D Right(FMath::Cos(Radians), FMath::Sin(Radians));
	const FVector2D Down(-FMath::Sin(Radians), FMath::Cos(Radians));
	const FVector2D Middle(CentreX, CentreY);
	const double Half = Size * 0.5;
	const FVector2D Corners[4] = { Middle - Right * Half - Down * Half, Middle + Right * Half - Down * Half, Middle + Right * Half + Down * Half, Middle - Right * Half + Down * Half };
	const FVector2D UVs[4] = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
	for (const int32 First : { 0, 2 })
	{
		const int32 A = 0, B = First == 0 ? 1 : 2, C = First == 0 ? 2 : 3;
		FCanvasTriangleItem Item(Corners[A] * Scale, Corners[B] * Scale, Corners[C] * Scale, UVs[A], UVs[B], UVs[C], Texture->GetResource());
		Item.SetColor(FLinearColor::White);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}
}

void AMcHUD::DrawHeldMap(int32 Width, int32 Height, const FMcItemStack& Stack, float Pitch, const FVector& PlayerPosition, float PlayerYaw)
{
	UTexture2D* Picture = GetMapTexture(Stack);
	if (!Picture)
	{
		return;
	}
	// Fitted from vanilla shots (ref run maphand2, one pitch every few degrees, 1280 x 720): the frame is 600 pixels wide
	// (142 map pixels: the 128 of the picture and 7 round it), centred, and rises from its top at y 544 (pitch 7 and
	// below) to y 30 (pitch 49.5 and above) along a smoothstep of the pitch.
	const float Side = 600.0f / 720.0f * static_cast<float>(Height);
	const float T = FMath::Clamp((Pitch - 7.0f) / (49.5f - 7.0f), 0.0f, 1.0f);
	const float Rise = T * T * (3.0f - 2.0f * T);
	const float Top = (30.0f + (544.0f - 30.0f) * (1.0f - Rise)) / 720.0f * static_cast<float>(Height);
	if (Top >= static_cast<float>(Height))
	{
		return;
	}
	const float Left = (static_cast<float>(Width) - Side) * 0.5f;
	DrawSprite(LoadSprite(TEXT("textures/map/map_background.png")), Left, Top, Side, Side);
	const float Border = Side * 7.0f / 142.0f;
	const float Inner = Side - 2.0f * Border;
	DrawSprite(Picture, Left + Border, Top + Border, Inner, Inner);
	// The holder's own marker (ref run maphand1, pitch 90): the 8 by 8 picture is one map pixel per texel, centred on the
	// player's place, and shown turned by its step of a sixteenth of a turn plus half a turn (the picture points up and
	// the step 0 is facing south, which the game shows pointing down).
	const FMcMapData* Map = McMapStore::Find(McMapStore::GetId(Stack));
	if (Map && !PlayerPosition.ContainsNaN())
	{
		bool bOffX = false, bOffZ = false;
		const int8 MarkerX = Map->MarkerByte(PlayerPosition.X, Map->CenterX, bOffX);
		const int8 MarkerZ = Map->MarkerByte(PlayerPosition.Z, Map->CenterZ, bOffZ);
		if (!bOffX && !bOffZ)
		{
			const float Unit = Inner / 128.0f;
			const float CentreX = Left + Border + (MarkerX * 0.5f + 64.0f) * Unit;
			const float CentreY = Top + Border + (MarkerZ * 0.5f + 64.0f) * Unit;
			DrawTurnedSprite(LoadSprite(TEXT("textures/map/decorations/player.png")), CentreX, CentreY, 8.0f * Unit,
				FMcMapData::MarkerRotation(PlayerYaw) * 22.5f + 180.0f);
		}
	}
}

const AMcHUD::FIcon& AMcHUD::GetMapIcon(const FMcItemStack& Stack)
{
	const int32 Id = McMapStore::GetId(Stack);
	const FMcMapData* Map = McMapStore::Find(Id);
	static const FIcon None;
	if (!Map)
	{
		return None;
	}
	TObjectPtr<UTexture2D>& Texture = MapTextures.FindOrAdd(Id);
	if (!Texture)
	{
		Texture = UTexture2D::CreateTransient(FMcMapData::Size, FMcMapData::Size, PF_B8G8R8A8);
		Texture->Filter = TF_Nearest;
		Texture->SRGB = true;
		MapIconRevisions.Remove(Id);
		MapIcons.Remove(Id);
	}
	const int32* Seen = MapIconRevisions.Find(Id);
	if (!Seen || *Seen != Map->Revision)
	{
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		uint32* Out = static_cast<uint32*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Index = 0; Index < FMcMapData::Size * FMcMapData::Size; ++Index)
		{
			Out[Index] = McMapColors::Argb(Map->Colors[Index]);
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		MapIconRevisions.Add(Id, Map->Revision);
	}
	FIcon& Icon = MapIcons.FindOrAdd(Id);
	if (Icon.Triangles.IsEmpty())
	{
		const FVector2D Corners[4] = { FVector2D(0, 0), FVector2D(16, 0), FVector2D(16, 16), FVector2D(0, 16) };
		const FVector2D UVs[4] = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
		for (const int32 First : { 0, 2 })
		{
			FIconTriangle& Triangle = Icon.Triangles.AddDefaulted_GetRef();
			const int32 Indices[3] = { 0, First == 0 ? 1 : 2, First == 0 ? 2 : 3 };
			for (int32 I = 0; I < 3; ++I)
			{
				Triangle.Positions[I] = Corners[Indices[I]];
				Triangle.UVs[I] = UVs[Indices[I]];
			}
			Triangle.Color = FLinearColor::White;
			Triangle.Texture = Texture;
		}
	}
	return Icon;
}

const AMcHUD::FIcon& AMcHUD::GetStackIcon(const FMcItemStack& Stack)
{
	if (Stack.Item == TEXT("minecraft:filled_map") && McMapStore::GetId(Stack) != INDEX_NONE)
	{
		return GetMapIcon(Stack);
	}
	if (Stack.Item.ToString().EndsWith(TEXT("_banner")))
	{
		return GetBannerIcon(Stack);
	}
	if (Stack.Item == TEXT("minecraft:shield"))
	{
		return GetShieldIcon(Stack);
	}
	if (!McItemDef::UsesPackModels(Stack))
	{
		return GetItemIcon(McItemFrames::ResolveStack(Stack));
	}
	const TArray<McItemDef::FModelRef> Refs = McItemDef::Resolve(Stack, McItemDef::EContext::Gui);
	FString Key;
	for (const McItemDef::FModelRef& Ref : Refs)
	{
		Key += Ref.Model;
		for (const uint32 Tint : Ref.Tints)
		{
			Key += FString::Printf(TEXT(":%08x"), Tint);
		}
		Key += TEXT("|");
	}
	if (const FIcon* Found = StackIcons.Find(Key))
	{
		return *Found;
	}
	FIcon Icon;
	for (const McItemDef::FModelRef& Ref : Refs)
	{
		BuildModelIcon(Ref.Model, Ref.Tints, Icon);
	}
	return StackIcons.Add(Key, MoveTemp(Icon));
}

// A model's "gui" display: the model centred on its middle, scaled, turned about z then y then x, moved, and 16 pixels to a
// block in the slot (the same view InventoryView gives block/block's [30, 225, 0] at 0.625).
void AMcHUD::BuildModelIcon(const FString& ModelId, const TArray<uint32>& Tints, FIcon& Icon)
{
	const FMcItemModel Model = McBakeItemModel(ModelId);
	if (Model.bFailed)
	{
		return;
	}
	const auto TintOf = [&Tints](int32 Index)
	{
		return Tints.IsValidIndex(Index) ? FLinearColor::FromSRGBColor(FColor((Tints[Index] >> 16) & 0xFF, (Tints[Index] >> 8) & 0xFF, Tints[Index] & 0xFF)) : FLinearColor::White;
	};
	if (Model.bGenerated)
	{
		for (int32 Layer = 0; Layer < Model.Layers.Num(); ++Layer)
		{
			BuildFlatQuad(Model.Layers[Layer].ToString(), Icon, TintOf(Layer));
		}
		return;
	}

	const FMcModelDisplay* Display = Model.Display.Find(TEXT("gui"));
	const FVector Rotation = Display ? Display->Rotation : FVector::ZeroVector;
	const FVector Translation = Display ? Display->Translation : FVector::ZeroVector;
	const FVector DisplayScale = Display ? Display->Scale : FVector::OneVector;
	const auto View = [&](const FVector3f& P)
	{
		FVector3d V = (FVector3d(P) - FVector3d(0.5)) * DisplayScale;
		const auto Turn = [](double A, double B, double Degrees, double& OutA, double& OutB)
		{
			const double C = FMath::Cos(FMath::DegreesToRadians(Degrees)), S = FMath::Sin(FMath::DegreesToRadians(Degrees));
			OutA = A * C + B * S;
			OutB = -A * S + B * C;
		};
		// z: (x, y) by the standard turn; y: (x, z); x: (y, z) with the signs of InventoryView.
		{ const double X = V.X * FMath::Cos(FMath::DegreesToRadians(Rotation.Z)) - V.Y * FMath::Sin(FMath::DegreesToRadians(Rotation.Z)); const double Y = V.X * FMath::Sin(FMath::DegreesToRadians(Rotation.Z)) + V.Y * FMath::Cos(FMath::DegreesToRadians(Rotation.Z)); V.X = X; V.Y = Y; }
		{ double X, Z; Turn(V.X, V.Z, Rotation.Y, X, Z); V.X = X; V.Z = Z; }
		{ const double Y = V.Y * FMath::Cos(FMath::DegreesToRadians(Rotation.X)) - V.Z * FMath::Sin(FMath::DegreesToRadians(Rotation.X)); const double Z = V.Y * FMath::Sin(FMath::DegreesToRadians(Rotation.X)) + V.Z * FMath::Cos(FMath::DegreesToRadians(Rotation.X)); V.Y = Y; V.Z = Z; }
		return V + FVector3d(Translation) / 16.0;
	};

	struct FProjected
	{
		double Depth;
		FIconTriangle Triangles[2];
	};
	TArray<FProjected> Quads;
	for (const FMcBakedQuad& Quad : Model.Quads)
	{
		FVector3d P[4];
		for (int32 I = 0; I < 4; ++I)
		{
			P[I] = View(Quad.Positions[I]);
		}
		const FVector3d Normal = FVector3d::CrossProduct(P[1] - P[0], P[2] - P[0]);
		if (Normal.Z <= 1e-6)
		{
			continue;
		}
		float Shade = 1.0f;
		if (!Model.bGuiFront && Quad.bShade)
		{
			const FVector3d N = Normal.GetSafeNormal();
			Shade = N.Y > 0.7 ? 1.0f : N.Y < -0.7 ? 0.5f : (N.X < 0.0 ? 0.8f : 0.6f);
		}
		FLinearColor Color = FLinearColor(Shade, Shade, Shade);
		if (Quad.TintIndex >= 0)
		{
			Color *= TintOf(Quad.TintIndex);
		}
		FProjected& Projected = Quads.AddDefaulted_GetRef();
		Projected.Depth = (P[0].Z + P[1].Z + P[2].Z + P[3].Z) * 0.25;
		UTexture2D* Texture = GetLayerTexture(Quad.Texture);
		const int32 Corners[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
		for (int32 T = 0; T < 2; ++T)
		{
			FIconTriangle& Triangle = Projected.Triangles[T];
			for (int32 I = 0; I < 3; ++I)
			{
				const FVector3d& V = P[Corners[T][I]];
				Triangle.Positions[I] = FVector2D(8.0 + V.X * 16.0, 8.0 - V.Y * 16.0);
				Triangle.UVs[I] = FVector2D(Quad.UVs[Corners[T][I]]);
			}
			Triangle.Color = Color;
			Triangle.Texture = Texture;
		}
	}
	Quads.Sort([](const FProjected& A, const FProjected& B) { return A.Depth < B.Depth; });
	for (const FProjected& Quad : Quads)
	{
		Icon.Triangles.Add(Quad.Triangles[0]);
		Icon.Triangles.Add(Quad.Triangles[1]);
	}
}

void AMcHUD::DrawSprite(UTexture2D* Texture, float X, float Y, float Width, float Height)
{
	if (!Texture || !Texture->GetResource())
	{
		return;
	}
	FCanvasTileItem Tile(FVector2D(X, Y) * Scale, Texture->GetResource(), FVector2D(Width, Height) * Scale, FLinearColor::White);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void AMcHUD::DrawMobPreview(FName Kind, FIntPoint Origin)
{
	const McEntityModel::FModel* Model = McEntityModel::Find(Kind);
	if (!Model)
	{
		return;
	}
	const TArray<FString>& Textures = Model->Variants.Num() > 0 ? Model->Variants[0] : Model->Textures;
	const McEntityModel::FAnim Anim;
	constexpr double Turn = 0.4363;  // 25 degrees
	const double CosT = FMath::Cos(Turn);
	const double SinT = FMath::Sin(Turn);
	struct FPlaced
	{
		FIconTriangle Triangle;
		double Depth = 0.0;
	};
	TArray<FPlaced> Placed;
	double MinY = 1e9;
	double MaxY = -1e9;
	for (int32 Layer = 0; Layer < Textures.Num(); ++Layer)
	{
		McPlayerModel::FMeshData Mesh;
		McEntityModel::BuildMesh(*Model, Anim, Layer, Mesh);
		UTexture2D* Texture = LoadSprite(TEXT("textures/entity/") + Textures[Layer]);
		if (!Texture)
		{
			continue;
		}
		for (int32 T = 0; T + 2 < Mesh.Triangles.Num(); T += 3)
		{
			const int32 I[3] = { Mesh.Triangles[T], Mesh.Triangles[T + 1], Mesh.Triangles[T + 2] };
			const FVector UN = Mesh.Normals.IsValidIndex(I[0]) ? Mesh.Normals[I[0]] : FVector::ZeroVector;
			// The mesh is in Unreal axes (z up); the turn is done in Minecraft's (y up, z south, facing the viewer).
			const FVector N(UN.X, UN.Z, UN.Y);
			// The viewer is on +z looking back: a face shows when its turned normal points that way.
			if (-N.X * SinT + N.Z * CosT <= 0.0)
			{
				continue;
			}
			FPlaced Item;
			Item.Triangle.Texture = Texture;
			double Depth = 0.0;
			for (int32 K = 0; K < 3; ++K)
			{
				const FVector& P = Mesh.Positions[I[K]];
				const double X = P.X * CosT + P.Z * SinT;
				const double Z = -P.X * SinT + P.Z * CosT;
				Item.Triangle.Positions[K] = FVector2D(X, -P.Y);
				Item.Triangle.UVs[K] = Mesh.UV0[I[K]];
				Depth += Z;
				if (Layer == 0)
				{
					MinY = FMath::Min(MinY, -P.Y);
					MaxY = FMath::Max(MaxY, -P.Y);
				}
			}
			const FColor Shade = Mesh.Colors.IsValidIndex(I[0]) ? Mesh.Colors[I[0]] : FColor::White;
			Item.Triangle.Color = FLinearColor(Shade.R / 255.0f, Shade.G / 255.0f, Shade.B / 255.0f, 1.0f);
			Item.Depth = Depth / 3.0 + Layer * 0.01;
			Placed.Add(Item);
		}
	}
	if (Placed.IsEmpty() || MaxY <= MinY)
	{
		return;
	}
	Placed.Sort([](const FPlaced& A, const FPlaced& B) { return A.Depth < B.Depth; });
	// Fit: 37.7 GUI pixels from the top of the head down to the feet, the feet 8 above the bottom of the box, centred across it.
	const double Fit = 37.7 / (MaxY - MinY);
	FIcon Icon;
	for (FPlaced& Item : Placed)
	{
		for (int32 K = 0; K < 3; ++K)
		{
			Item.Triangle.Positions[K] = FVector2D(53.0 + Item.Triangle.Positions[K].X * Fit, 64.0 + (Item.Triangle.Positions[K].Y - MaxY) * Fit);
		}
		Icon.Triangles.Add(Item.Triangle);
	}
	DrawIcon(Icon, static_cast<float>(Origin.X), static_cast<float>(Origin.Y));
}

void AMcHUD::DrawIcon(const FIcon& Icon, float X, float Y)
{
	for (const FIconTriangle& Triangle : Icon.Triangles)
	{
		if (!Triangle.Texture || !Triangle.Texture->GetResource())
		{
			continue;
		}
		FCanvasTriangleItem Item(
			(FVector2D(X, Y) + Triangle.Positions[0]) * Scale, (FVector2D(X, Y) + Triangle.Positions[1]) * Scale, (FVector2D(X, Y) + Triangle.Positions[2]) * Scale,
			Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2], Triangle.Texture->GetResource());
		Item.SetColor(Triangle.Color);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}
}

