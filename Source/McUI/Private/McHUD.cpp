#include "McHUD.h"
#include "McBanner.h"
#include "McGuiLayout.h"
#include "McBlockModels.h"
#include "McTextureTiles.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "McChunk.h"
#include "McEntityModel.h"
#include "McPlayerModel.h"
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

	UTexture2D* MakeTexture(const TArray64<uint8>& Bgra, int32 Width, int32 Height, bool bSmooth = false)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}
		Texture->Filter = bSmooth ? TF_Bilinear : TF_Nearest;
		Texture->SRGB = !bSmooth;
		Texture->LODGroup = TEXTUREGROUP_Pixels2D;
		void* Data = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Bgra.GetData(), static_cast<SIZE_T>(Width) * Height * 4);
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	// A PNG from the jar, cut to its top square when it is an animation strip.
	UTexture2D* LoadPng(const FString& Path, bool bFirstFrame, bool bGreyToAlpha = false)
	{
		TArray<uint8> File;
		if (!FFileHelper::LoadFileToArray(File, *Path))
		{
			UE_LOG(LogMcUI, Warning, TEXT("Can't read %s"), *Path);
			return nullptr;
		}
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
		return MakeTexture(Raw, Width, Height, bGreyToAlpha);
	}
}

int32 AMcHUD::GetGuiScale(int32 Width, int32 Height)
{
	return McGui::GetGuiScale(Width, Height);
}

UTexture2D* AMcHUD::LoadSprite(const FString& RelativeAssetPath)
{
	if (TObjectPtr<UTexture2D>* Found = Sprites.Find(RelativeAssetPath))
	{
		return *Found;
	}
	// "resources:" pictures come with the sounds, from the launcher's asset index, rather than in the jar (the title
	// panorama: the jar's is a 1 by 1 stand-in).
	const FString Path = RelativeAssetPath.StartsWith(TEXT("resources:"))
		? FPaths::Combine(McCache::Root(), TEXT("assets/minecraft"), RelativeAssetPath.Mid(10)) : McCache::Asset(RelativeAssetPath);
	UTexture2D* Texture = LoadPng(Path, false);
	Sprites.Add(RelativeAssetPath, Texture);
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
	UTexture2D* Texture = LoadPng(McCache::Asset(TEXT("textures/") + McCache::PathOf(TextureId.ToString()) + TEXT(".png")), true);
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

void AMcHUD::BuildFlatQuad(const FString& TextureId, FIcon& Icon)
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
		Triangle.Color = FLinearColor::White;
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
			if (const TSharedPtr<FJsonObject> ItemModel = McCache::LoadJsonObject(McCache::Asset(TEXT("models/") + McCache::PathOf(ModelId) + TEXT(".json"))))
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
			const TSharedPtr<FJsonObject> ItemModel = McCache::LoadJsonObject(McCache::Asset(TEXT("models/") + McCache::PathOf(ModelId) + TEXT(".json")));
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

