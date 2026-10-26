#include "McHUD.h"
#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "McPlayerPawn.h"
#include "McSectionMesher.h"
#include "McUI.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
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

	UTexture2D* MakeTexture(const TArray64<uint8>& Bgra, int32 Width, int32 Height)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Texture)
		{
			return nullptr;
		}
		Texture->Filter = TF_Nearest;
		Texture->SRGB = true;
		Texture->LODGroup = TEXTUREGROUP_Pixels2D;
		void* Data = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Bgra.GetData(), static_cast<SIZE_T>(Width) * Height * 4);
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	// A PNG from the jar, cut to its top square when it is an animation strip.
	UTexture2D* LoadPng(const FString& Path, bool bFirstFrame)
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
		return MakeTexture(Raw, Width, Height);
	}
}

int32 AMcHUD::GetGuiScale(int32 Width, int32 Height)
{
	int32 Result = 1;
	while (Width / (Result + 1) >= 320 && Height / (Result + 1) >= 240)
	{
		++Result;
	}
	return Result;
}

UTexture2D* AMcHUD::LoadSprite(const FString& RelativeAssetPath)
{
	if (TObjectPtr<UTexture2D>* Found = Sprites.Find(RelativeAssetPath))
	{
		return *Found;
	}
	UTexture2D* Texture = LoadPng(McCache::Asset(RelativeAssetPath), false);
	Sprites.Add(RelativeAssetPath, Texture);
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
	FIcon& Icon = Icons.Add(State);

	// The item definition says whether the item is drawn flat (a "layer0" sprite) or as its block model.
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FString Name = McCache::PathOf(Registry.GetBlockOf(State).Name.ToString());
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

	if (!FlatTexture.IsEmpty())
	{
		UTexture2D* Texture = GetLayerTexture(FName(*McCache::Namespaced(FlatTexture)));
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
		return Icon;
	}

	BuildBlockIcon(State, Icon);
	return Icon;
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
		UTexture2D* Texture = GetLayerTexture(Quad.Texture);
		const int32 Corners[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
		for (int32 T = 0; T < 2; ++T)
		{
			FIconTriangle& Triangle = Projected.Triangles[T];
			for (int32 I = 0; I < 3; ++I)
			{
				Triangle.Positions[I] = ToSlot(P[Corners[T][I]]);
				Triangle.UVs[I] = FVector2D(Quad.UVs[Corners[T][I]]);
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

void AMcHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	Scale = GetGuiScale(Canvas->SizeX, Canvas->SizeY);
	const float Width = static_cast<float>(Canvas->SizeX) / Scale;
	const float Height = static_cast<float>(Canvas->SizeY) / Scale;

	// Minecraft draws the crosshair inverting what is behind it; plain white for now.
	DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/crosshair.png")), FMath::FloorToFloat((Width - CrosshairSize) / 2), FMath::FloorToFloat((Height - CrosshairSize) / 2), CrosshairSize, CrosshairSize);

	const AMcPlayerPawn* Player = Cast<AMcPlayerPawn>(GetOwningPawn());
	if (!Player)
	{
		return;
	}
	const float HotbarX = FMath::FloorToFloat((Width - HotbarWidth) / 2);
	const float HotbarY = Height - HotbarHeight;
	DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/hotbar.png")), HotbarX, HotbarY, HotbarWidth, HotbarHeight);
	DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/hotbar_selection.png")), HotbarX - 1 + Player->SelectedSlot * SlotSpacing, HotbarY - 1, 24, 23);
	for (int32 Slot = 0; Slot < Player->Hotbar.Num(); ++Slot)
	{
		if (Player->Hotbar[Slot] != 0)
		{
			DrawIcon(GetIcon(Player->Hotbar[Slot]), HotbarX + 3 + Slot * SlotSpacing, HotbarY + 3);
		}
	}
}
