#include "McEntityModel.h"
#include "McRenderMode.h"
#include "McCache.h"
#include "McDecor.h"
#include "McChunk.h"
#include "McRender.h"
#include "McRenderTextures.h"
#include "McSkyComponent.h"
#include "Engine/Texture2DArray.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"

namespace McEntityModel
{
	namespace
	{
		using McPlayerModel::EBoxFace;

		FBox Box(int32 U, int32 V, float X, float Y, float Z, int32 W, int32 H, int32 D, float Inflate = 0.0f, bool bMirror = false)
		{
			FBox Result;
			Result.UV = FIntPoint(U, V);
			Result.Min = FVector(X, Y, Z);
			Result.Size = FIntVector(W, H, D);
			Result.Inflate = Inflate;
			Result.bMirror = bMirror;
			return Result;
		}

		FPart Part(ERole Role, FVector Pivot, std::initializer_list<FBox> Boxes, float Pitch = 0.0f, float Yaw = 0.0f, float Roll = 0.0f, int32 Index = 0, int32 Layer = 0)
		{
			FPart Result;
			Result.Role = Role;
			Result.Index = Index;
			Result.Layer = Layer;
			Result.Pivot = Pivot;
			Result.RestPitch = Pitch;
			Result.RestYaw = Yaw;
			Result.RestRoll = Roll;
			Result.Boxes = Boxes;
			return Result;
		}

		// The four legs of a pig, cow or sheep.
		void AddLegs(FModel& Model, int32 U, int32 V, float Width, float Height, float X, float Front, float Back, float Top)
		{
			const FBox Leg = Box(U, V, -Width * 0.5f, -Height, -Width * 0.5f, static_cast<int32>(Width), static_cast<int32>(Height), static_cast<int32>(Width));
			Model.Parts.Add(Part(ERole::LegFrontRight, FVector(-X, Top, Front), { Leg }));
			Model.Parts.Add(Part(ERole::LegFrontLeft, FVector(X, Top, Front), { Leg }));
			Model.Parts.Add(Part(ERole::LegBackRight, FVector(-X, Top, Back), { Leg }));
			Model.Parts.Add(Part(ERole::LegBackLeft, FVector(X, Top, Back), { Leg }));
		}

		// A person: head, body, two arms and two legs laid out on the player's skin. The left limbs have their own
		// pictures on a 64 high skin and share the right's, mirrored, on a 32 high one.
		void AddPerson(FModel& Model, bool bThin, bool bOwnLeftLimbs)
		{
			const float Arm = bThin ? 2.0f : 4.0f;
			const int32 ArmSize = bThin ? 2 : 4;
			Model.Parts.Add(Part(ERole::Head, FVector(0, 24, 0), { Box(0, 0, -4, 0, -4, 8, 8, 8) }));
			Model.Parts.Add(Part(ERole::Static, FVector(0, 24, 0), { Box(16, 16, -4, -12, -2, 8, 12, 4) }));
			const float Shoulder = bThin ? 5.0f : 5.0f;
			Model.Parts.Add(Part(ERole::ArmRight, FVector(-Shoulder, 22, 0), { Box(40, 16, bThin ? -1.0f : -3.0f, -10, -Arm * 0.5f, ArmSize, 12, ArmSize) }));
			Model.Parts.Add(Part(ERole::ArmLeft, FVector(Shoulder, 22, 0),
				{ bOwnLeftLimbs ? Box(32, 48, -1, -10, -Arm * 0.5f, ArmSize, 12, ArmSize) : Box(40, 16, bThin ? -1.0f : -1.0f, -10, -Arm * 0.5f, ArmSize, 12, ArmSize, 0.0f, true) }));
			const float LegWidth = bThin ? 2.0f : 4.0f;
			const int32 LegSize = bThin ? 2 : 4;
			Model.Parts.Add(Part(ERole::LegRight, FVector(-1.9f, 12, 0), { Box(0, 16, -LegWidth * 0.5f, -12, -LegWidth * 0.5f, LegSize, 12, LegSize) }));
			Model.Parts.Add(Part(ERole::LegLeft, FVector(1.9f, 12, 0),
				{ bOwnLeftLimbs ? Box(16, 48, -LegWidth * 0.5f, -12, -LegWidth * 0.5f, LegSize, 12, LegSize) : Box(0, 16, -LegWidth * 0.5f, -12, -LegWidth * 0.5f, LegSize, 12, LegSize, 0.0f, true) }));
		}

		TMap<FName, FModel> BuildModels()
		{
			TMap<FName, FModel> Models;
			constexpr float Flat = -HALF_PI;

			{
				FModel Pig;
				Pig.Kind = TEXT("minecraft:pig");
				Pig.TextureSize = FIntPoint(64, 64);
				Pig.Textures = { TEXT("pig/pig_temperate.png") };
				Pig.Parts.Add(Part(ERole::Static, FVector(0, 10, 0), { Box(28, 8, -5, -8, -4, 10, 16, 8) }, Flat));
				Pig.Parts.Add(Part(ERole::Head, FVector(0, 12, 8), { Box(0, 0, -4, -4, 0, 8, 8, 8), Box(16, 16, -2, -3, 8, 4, 3, 1) }));
				AddLegs(Pig, 0, 16, 4, 6, 3, 5, -5, 6);
				Models.Add(Pig.Kind, Pig);
			}
			{
				FModel Cow;
				Cow.Kind = TEXT("minecraft:cow");
				Cow.TextureSize = FIntPoint(64, 64);
				Cow.Textures = { TEXT("cow/cow_temperate.png") };
				Cow.Parts.Add(Part(ERole::Static, FVector(0, 17, 0), { Box(18, 4, -6, -9, -5, 12, 18, 10) }, Flat));
				Cow.Parts.Add(Part(ERole::Head, FVector(0, 20, 9), { Box(0, 0, -4, -4, 0, 8, 8, 6), Box(22, 0, -5, 3, 1, 1, 3, 1), Box(22, 0, 4, 3, 1, 1, 3, 1, 0.0f, true) }));
				AddLegs(Cow, 0, 16, 4, 12, 4, 7, -7, 12);
				Models.Add(Cow.Kind, Cow);
			}
			{
				FModel Sheep;
				Sheep.Kind = TEXT("minecraft:sheep");
				Sheep.Textures = { TEXT("sheep/sheep.png"), TEXT("sheep/sheep_wool.png") };
				Sheep.Parts.Add(Part(ERole::Static, FVector(0, 16, 0), { Box(28, 8, -4, -8, -3, 8, 16, 6) }, Flat));
				Sheep.Parts.Add(Part(ERole::Head, FVector(0, 19, 8), { Box(0, 0, -3, -3, 0, 6, 6, 8) }));
				AddLegs(Sheep, 0, 16, 4, 12, 3, 5, -5, 12);
				// The wool: a bigger body, a head covering, and the top of each leg.
				Sheep.Parts.Add(Part(ERole::Static, FVector(0, 16, 0), { Box(28, 8, -4, -8, -3, 8, 16, 6, 1.75f) }, Flat, 0, 0, 0, 1));
				Sheep.Parts.Add(Part(ERole::Head, FVector(0, 19, 8), { Box(0, 0, -3, -3, 0, 6, 6, 6, 0.6f) }, 0, 0, 0, 0, 1));
				const FBox WoolLeg = Box(0, 16, -2, -6, -2, 4, 6, 4, 0.5f);
				Sheep.Parts.Add(Part(ERole::LegFrontRight, FVector(-3, 12, 5), { WoolLeg }, 0, 0, 0, 0, 1));
				Sheep.Parts.Add(Part(ERole::LegFrontLeft, FVector(3, 12, 5), { WoolLeg }, 0, 0, 0, 0, 1));
				Sheep.Parts.Add(Part(ERole::LegBackRight, FVector(-3, 12, -5), { WoolLeg }, 0, 0, 0, 0, 1));
				Sheep.Parts.Add(Part(ERole::LegBackLeft, FVector(3, 12, -5), { WoolLeg }, 0, 0, 0, 0, 1));
				Models.Add(Sheep.Kind, Sheep);
			}
			{
				FModel Chicken;
				Chicken.Kind = TEXT("minecraft:chicken");
				Chicken.Textures = { TEXT("chicken/chicken_temperate.png") };
				// A chick is the same model at half the width.
				Chicken.BaseWidth = 0.4f;
				Chicken.Parts.Add(Part(ERole::Static, FVector(0, 8, 0), { Box(0, 9, -3, -4, -3, 6, 8, 6) }, Flat));
				Chicken.Parts.Add(Part(ERole::Head, FVector(0, 9, 4), { Box(0, 0, -2, 0, -2, 4, 6, 3), Box(14, 0, -2, 2, 1, 4, 2, 2), Box(14, 4, -1, 0, 1, 2, 2, 2) }));
				const FBox Leg = Box(26, 0, -1.5f, -5, -1.5f, 3, 5, 3);
				Chicken.Parts.Add(Part(ERole::LegRight, FVector(-2, 5, 0), { Leg }));
				Chicken.Parts.Add(Part(ERole::LegLeft, FVector(2, 5, 0), { Leg }));
				Chicken.Parts.Add(Part(ERole::WingRight, FVector(-3, 11, 0), { Box(24, 13, -1, -4, -3, 1, 4, 6) }));
				Chicken.Parts.Add(Part(ERole::WingLeft, FVector(3, 11, 0), { Box(24, 13, 0, -4, -3, 1, 4, 6) }));
				Models.Add(Chicken.Kind, Chicken);
			}
			{
				FModel Zombie;
				Zombie.Kind = TEXT("minecraft:zombie");
				Zombie.TextureSize = FIntPoint(64, 64);
				Zombie.Textures = { TEXT("zombie/zombie.png") };
				Zombie.bArmsForward = true;
				AddPerson(Zombie, false, true);
				Models.Add(Zombie.Kind, Zombie);
			}
			{
				FModel Skeleton;
				Skeleton.Kind = TEXT("minecraft:skeleton");
				Skeleton.Textures = { TEXT("skeleton/skeleton.png") };
				AddPerson(Skeleton, true, false);
				Models.Add(Skeleton.Kind, Skeleton);
			}
			{
				FModel Spider;
				Spider.Kind = TEXT("minecraft:spider");
				// The second picture is the eyes, which glow in the dark.
				Spider.Textures = { TEXT("spider/spider.png"), TEXT("spider/spider_eyes.png") };
				Spider.GlowLayers = 2;
				Spider.Parts.Add(Part(ERole::Head, FVector(0, 9, 7), { Box(32, 4, -4, -4, -4, 8, 8, 8) }));
				Spider.Parts.Add(Part(ERole::Head, FVector(0, 9, 7), { Box(32, 4, -4, -4, -4, 8, 8, 8, 0.05f) }, 0, 0, 0, 0, 1));
				Spider.Parts.Add(Part(ERole::Static, FVector(0, 9, 0), { Box(0, 0, -3, -3, -3, 6, 6, 6) }));
				Spider.Parts.Add(Part(ERole::Static, FVector(0, 9, -9), { Box(0, 12, -5, -4, -6, 10, 8, 12) }));
				// Four legs a side, fanned out and sloping down.
				const float Fan[4] = { 0.78f, 0.39f, -0.39f, -0.78f };
				const float Spread[4] = { 4.0f, 1.5f, -1.0f, -3.5f };
				for (int32 Row = 0; Row < 4; ++Row)
				{
					Spider.Parts.Add(Part(ERole::SpiderLeg, FVector(-4, 9, Spread[Row]), { Box(18, 0, -16, -1, -1, 16, 2, 2) }, 0, Fan[Row], 0.58f, Row * 2));
					Spider.Parts.Add(Part(ERole::SpiderLeg, FVector(4, 9, Spread[Row]), { Box(18, 0, 0, -1, -1, 16, 2, 2, 0.0f, true) }, 0, -Fan[Row], -0.58f, Row * 2 + 1));
				}
				Models.Add(Spider.Kind, Spider);
			}
			{
				// A creeper: a head, a body and four stubs of legs (minecraft.wiki, "Creeper": 0.6 by 1.7).
				FModel Creeper;
				Creeper.Kind = TEXT("minecraft:creeper");
				Creeper.Textures = { TEXT("creeper/creeper.png") };
				Creeper.Parts.Add(Part(ERole::Head, FVector(0, 18, 0), { Box(0, 0, -4, 0, -4, 8, 8, 8) }));
				Creeper.Parts.Add(Part(ERole::Static, FVector(0, 18, 0), { Box(16, 16, -4, -12, -2, 8, 12, 4) }));
				const FBox Leg = Box(0, 16, -2, -6, -2, 4, 6, 4);
				Creeper.Parts.Add(Part(ERole::LegBackRight, FVector(-2, 6, -4), { Leg }));
				Creeper.Parts.Add(Part(ERole::LegBackLeft, FVector(2, 6, -4), { Leg }));
				Creeper.Parts.Add(Part(ERole::LegFrontRight, FVector(-2, 6, 4), { Leg }));
				Creeper.Parts.Add(Part(ERole::LegFrontLeft, FVector(2, 6, 4), { Leg }));
				Models.Add(Creeper.Kind, Creeper);
			}
			{
				// An enderman: a person with a thin body and arms and legs 30 pixels long (0.6 by 2.9). The second picture
				// is the eyes.
				FModel Enderman;
				Enderman.Kind = TEXT("minecraft:enderman");
				Enderman.Textures = { TEXT("enderman/enderman.png"), TEXT("enderman/enderman_eyes.png") };
				Enderman.GlowLayers = 2;
				Enderman.MaxLimbSwing = 0.4f;
				Enderman.Parts.Add(Part(ERole::Head, FVector(0, 38, 0), { Box(0, 0, -4, 0, -4, 8, 8, 8) }));
				Enderman.Parts.Add(Part(ERole::Head, FVector(0, 38, 0), { Box(0, 0, -4, 0, -4, 8, 8, 8, 0.05f) }, 0, 0, 0, 0, 1));
				Enderman.Parts.Add(Part(ERole::Static, FVector(0, 38, 0), { Box(32, 16, -4, -12, -2, 8, 12, 4) }));
				Enderman.Parts.Add(Part(ERole::ArmRight, FVector(-5, 36, 0), { Box(56, 0, -1, -28, -1, 2, 30, 2) }));
				Enderman.Parts.Add(Part(ERole::ArmLeft, FVector(5, 36, 0), { Box(56, 0, -1, -28, -1, 2, 30, 2, 0.0f, true) }));
				Enderman.Parts.Add(Part(ERole::LegRight, FVector(-2, 29, 0), { Box(56, 0, -1, -30, -1, 2, 30, 2) }));
				Enderman.Parts.Add(Part(ERole::LegLeft, FVector(2, 29, 0), { Box(56, 0, -1, -30, -1, 2, 30, 2, 0.0f, true) }));
				Models.Add(Enderman.Kind, Enderman);
			}
			{
				// A slime of size 1 (0.52 across): a solid core with eyes and a mouth, in a see-through shell. Bigger sizes
				// are the same model grown with the hitbox.
				FModel Slime;
				Slime.Kind = TEXT("minecraft:slime");
				Slime.Textures = { TEXT("slime/slime.png"), TEXT("slime/slime.png") };
				Slime.TranslucentLayers = 2;
				Slime.BaseWidth = 0.52f;
				Slime.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 16, -3, 1, -3, 6, 6, 6), Box(32, 0, -3.25f, 4, 1.5f, 2, 2, 2), Box(32, 4, 1.25f, 4, 1.5f, 2, 2, 2), Box(32, 8, 0, 2, 2.5f, 1, 1, 1) }));
				Slime.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 0, -4, 0, -4, 8, 8, 8) }, 0, 0, 0, 0, 1));
				Models.Add(Slime.Kind, Slime);
			}
			{
				// A minecart: a floor and four walls, the long way along the rail (z).
				FModel Cart;
				Cart.Kind = TEXT("minecraft:minecart");
				Cart.TextureSize = FIntPoint(64, 32);
				Cart.Textures = { TEXT("minecart/minecart.png") };
				Cart.Parts.Add(Part(ERole::Static, FVector(0, 3, 0), { Box(0, 10, -10, -8, -1, 20, 16, 2) }, Flat, HALF_PI));
				Cart.Parts.Add(Part(ERole::Static, FVector(0, 3, 9), { Box(0, 0, -8, 0, -1, 16, 8, 2) }));
				Cart.Parts.Add(Part(ERole::Static, FVector(0, 3, -9), { Box(0, 0, -8, 0, -1, 16, 8, 2) }));
				Cart.Parts.Add(Part(ERole::Static, FVector(7, 3, 0), { Box(0, 0, -8, 0, -1, 16, 8, 2) }, 0, HALF_PI));
				Cart.Parts.Add(Part(ERole::Static, FVector(-7, 3, 0), { Box(0, 0, -8, 0, -1, 16, 8, 2) }, 0, HALF_PI));
				Models.Add(Cart.Kind, Cart);
			}
			{
				// A boat: a hull floor and four sides (the paddles are left out). The wood is chosen by the texture.
				FModel Boat;
				Boat.Kind = TEXT("minecraft:boat");
				Boat.TextureSize = FIntPoint(128, 64);
				Boat.Textures = { TEXT("boat/oak.png") };
				Boat.Parts.Add(Part(ERole::Static, FVector(0, 2, 0), { Box(0, 0, -14, -8, -3, 28, 16, 3) }, Flat, HALF_PI));
				Boat.Parts.Add(Part(ERole::Static, FVector(0, 3, 15), { Box(0, 27, -8, 0, -1, 16, 6, 2) }));
				Boat.Parts.Add(Part(ERole::Static, FVector(0, 3, -15), { Box(0, 19, -9, 0, -1, 18, 6, 2) }));
				Boat.Parts.Add(Part(ERole::Static, FVector(9, 3, 0), { Box(0, 35, -14, 0, -1, 28, 6, 2) }, 0, HALF_PI));
				Boat.Parts.Add(Part(ERole::Static, FVector(-9, 3, 0), { Box(0, 43, -14, 0, -1, 28, 6, 2) }, 0, HALF_PI));
				Models.Add(Boat.Kind, Boat);
			}
			return Models;
		}

		// Minecraft's shading: 1 from above, 0.5 from below, 0.8 from north and south, 0.6 from east and west.
		FColor Shade(const FVector& Normal, float Hurt)
		{
			const double Y = Normal.Y;
			const double Value = Normal.X * Normal.X * 0.6 + Normal.Z * Normal.Z * 0.8 + Y * Y * (Y > 0.0 ? 1.0 : 0.5);
			const auto Byte = [](double V) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(V * 255.0), 0, 255)); };
			// A hurt mob goes red: the other channels fade towards a third.
			const double Rest = FMath::Lerp(1.0, 0.3, FMath::Clamp(Hurt, 0.0f, 1.0f));
			return FColor(Byte(Value), Byte(Value * Rest), Byte(Value * Rest), 255);
		}

		void AddBox(const FBox& Box, const FPart& Part, const FQuat& PartRotation, const FVector& Pivot, float BodyYaw, FIntPoint TextureSize, float Hurt, bool bGlow, McPlayerModel::FMeshData& Out)
		{
			const FVector Min = Box.Min * Part.Scale - FVector(Box.Inflate);
			const FVector Max = (Box.Min + FVector(Box.Size.X, Box.Size.Y, Box.Size.Z)) * Part.Scale + FVector(Box.Inflate);
			const FQuat ModelRotation(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw));
			const auto Place = [&](const FVector& Local)
			{
				return ModelRotation.RotateVector(Pivot + PartRotation.RotateVector(Local));
			};

			struct FFace { EBoxFace Face; FVector Normal; FVector Corners[4]; };
			const FFace Faces[] = {
				{ EBoxFace::South, FVector(0, 0, 1),  { FVector(Min.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Max.Z), FVector(Max.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z) } },
				{ EBoxFace::North, FVector(0, 0, -1), { FVector(Max.X, Min.Y, Min.Z), FVector(Min.X, Min.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z) } },
				{ EBoxFace::East,  FVector(1, 0, 0),  { FVector(Max.X, Min.Y, Max.Z), FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Max.Y, Min.Z), FVector(Max.X, Max.Y, Max.Z) } },
				{ EBoxFace::West,  FVector(-1, 0, 0), { FVector(Min.X, Min.Y, Min.Z), FVector(Min.X, Min.Y, Max.Z), FVector(Min.X, Max.Y, Max.Z), FVector(Min.X, Max.Y, Min.Z) } },
				{ EBoxFace::Up,    FVector(0, 1, 0),  { FVector(Min.X, Max.Y, Max.Z), FVector(Max.X, Max.Y, Max.Z), FVector(Max.X, Max.Y, Min.Z), FVector(Min.X, Max.Y, Min.Z) } },
				{ EBoxFace::Down,  FVector(0, -1, 0), { FVector(Min.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Min.Z), FVector(Max.X, Min.Y, Max.Z), FVector(Min.X, Min.Y, Max.Z) } },
			};
			for (const FFace& Face : Faces)
			{
				EBoxFace PictureFace = Face.Face;
				if (Box.bMirror)
				{
					// A mirrored box wears its twin's net backwards: east and west trade places, and every picture flips.
					PictureFace = Face.Face == EBoxFace::East ? EBoxFace::West : Face.Face == EBoxFace::West ? EBoxFace::East : Face.Face;
				}
				const FIntRect Rect = McPlayerModel::GetFaceRect(Box.UV, Box.Size, PictureFace);
				const double Left = Box.bMirror ? Rect.Max.X : Rect.Min.X;
				const double Right = Box.bMirror ? Rect.Min.X : Rect.Max.X;
				const FVector2D UVs[4] = {
					FVector2D(Left, Rect.Max.Y), FVector2D(Right, Rect.Max.Y),
					FVector2D(Right, Rect.Min.Y), FVector2D(Left, Rect.Min.Y),
				};
				const FVector Normal = ModelRotation.RotateVector(PartRotation.RotateVector(Face.Normal));
				const FVector UnrealNormal(Normal.X, Normal.Z, Normal.Y);
				const FColor Color = bGlow ? FColor::White : Shade(Normal, Hurt);
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector Model = Place(Face.Corners[Corner]);
					Out.Positions.Add(FVector(Model.X, Model.Z, Model.Y) * (McCoords::UnitsPerBlock / 16.0));
					Out.Normals.Add(UnrealNormal);
					Out.UV0.Add(FVector2D(UVs[Corner].X / TextureSize.X, UVs[Corner].Y / TextureSize.Y));
					Out.Colors.Add(Color);
				}
				Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			}
		}

		float Swing(float WalkPos, float WalkAmount, float Phase)
		{
			return FMath::Cos(WalkPos * 0.6662f + Phase) * 1.4f * WalkAmount;
		}
	}

	const FModel* Find(FName Kind)
	{
		static const TMap<FName, FModel> Models = BuildModels();
		return Models.Find(Kind);
	}

	void BuildBannerMesh(bool bWall, bool bFlag, float BodyYaw, McPlayerModel::FMeshData& Out)
	{
		// Pixels before the two thirds the game scales a banner by; the flag faces south (+z) and hangs from the bar.
		static const FModel Pole = []
		{
			FModel Result;
			Result.TextureSize = FIntPoint(64, 64);
			Result.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(44, 0, -1, 0, -1, 2, 42, 2), Box(0, 42, -10, 42, -1, 20, 2, 2) }));
			return Result;
		}();
		static const FModel WallBar = []
		{
			FModel Result;
			Result.TextureSize = FIntPoint(64, 64);
			Result.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 42, -10, 22, -12, 20, 2, 2) }));
			return Result;
		}();
		static const FModel Flag = []
		{
			FModel Result;
			Result.TextureSize = FIntPoint(64, 64);
			Result.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 0, -10, 2, 1, 20, 40, 1) }));
			return Result;
		}();
		static const FModel WallFlag = []
		{
			FModel Result;
			Result.TextureSize = FIntPoint(64, 64);
			Result.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 0, -10, -18, -10, 20, 40, 1) }));
			return Result;
		}();
		FAnim Anim;
		Anim.BodyYaw = BodyYaw;
		BuildMesh(bFlag ? (bWall ? WallFlag : Flag) : (bWall ? WallBar : Pole), Anim, 0, Out);
		for (FVector& Position : Out.Positions)
		{
			Position *= 2.0 / 3.0;
		}
	}

	namespace
	{
		// The way an item frame or painting faces (the game's directions: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east):
		// U runs to the right of a viewer in front of it, V up its face, N out of it, all in Minecraft's axes.
		void GetDecorBasis(int32 Facing, FVector& U, FVector& V, FVector& N)
		{
			V = FVector(0, 1, 0);
			switch (Facing)
			{
			case 0: N = FVector(0, -1, 0); U = FVector(1, 0, 0); V = FVector(0, 0, 1); break;
			case 1: N = FVector(0, 1, 0); U = FVector(1, 0, 0); V = FVector(0, 0, -1); break;
			case 2: N = FVector(0, 0, -1); U = FVector(-1, 0, 0); break;
			case 4: N = FVector(-1, 0, 0); U = FVector(0, 0, 1); break;
			case 5: N = FVector(1, 0, 0); U = FVector(0, 0, -1); break;
			default: N = FVector(0, 0, 1); U = FVector(1, 0, 0); break;
			}
		}

		// One quad, its corners bottom-left, bottom-right, top-right, top-left seen from outside, in blocks about the entity.
		void AddDecorQuad(const FVector (&Corners)[4], const FVector2D (&UVs)[4], bool bGlow, McPlayerModel::FMeshData& Out)
		{
			const FVector Normal = FVector::CrossProduct(Corners[1] - Corners[0], Corners[3] - Corners[0]).GetSafeNormal();
			const FColor Color = bGlow ? FColor::White : Shade(Normal, 0.0f);
			const int32 First = Out.Positions.Num();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Out.Positions.Add(FVector(Corners[Corner].X, Corners[Corner].Z, Corners[Corner].Y) * McCoords::UnitsPerBlock);
				Out.Normals.Add(FVector(Normal.X, Normal.Z, Normal.Y));
				Out.UV0.Add(UVs[Corner]);
				Out.Colors.Add(Color);
			}
			Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}

		// A flat picture in the plane spanned by U (right) and V (up), centred at Centre, Width by Height blocks, showing
		// the rectangle of the texture Rect = [U0, V0, U1, V1]. Seen from behind (bBack) the right-hand side is the other one.
		void AddDecorPicture(const FVector& Centre, const FVector& U, const FVector& V, double Width, double Height, const FVector4& Rect, bool bBack, bool bGlow, McPlayerModel::FMeshData& Out)
		{
			const FVector Right = U * (Width * 0.5);
			const FVector Up = V * (Height * 0.5);
			FVector Corners[4] = { Centre - Right - Up, Centre + Right - Up, Centre + Right + Up, Centre - Right + Up };
			FVector2D UVs[4] = { FVector2D(Rect.X, Rect.W), FVector2D(Rect.Z, Rect.W), FVector2D(Rect.Z, Rect.Y), FVector2D(Rect.X, Rect.Y) };
			if (bBack)
			{
				Swap(Corners[0], Corners[1]);
				Swap(Corners[2], Corners[3]);
				Swap(UVs[0], UVs[1]);
				Swap(UVs[2], UVs[3]);
			}
			AddDecorQuad(Corners, UVs, bGlow, Out);
		}

		// A box of the item frame's model, in pixels of a block, for a frame hung on a wall to the south
		// (assets: models/block/template_item_frame.json), with each face's picture rectangle in sixteenths of its texture;
		// a face with no rectangle (-1) is not drawn. Faces in the order down, up, north, south, west, east.
		struct FFrameElement
		{
			FVector From;
			FVector To;
			bool bBack;
			int32 Faces[6][4];
		};

		const FFrameElement* GetFrameElements(int32& OutNum)
		{
			static const FFrameElement Elements[] = {
				// The back plate, a picture of its own.
				{ FVector(3, 3, 15.5), FVector(13, 13, 16), true, {
					{ -1, -1, -1, -1 }, { -1, -1, -1, -1 }, { 3, 3, 13, 13 }, { 3, 3, 13, 13 }, { -1, -1, -1, -1 }, { -1, -1, -1, -1 } } },
				// The wood: bottom, top, left, right.
				{ FVector(2, 2, 15), FVector(14, 3, 16), false, {
					{ 2, 0, 14, 1 }, { 2, 15, 14, 16 }, { 2, 13, 14, 14 }, { 2, 13, 14, 14 }, { 15, 13, 16, 14 }, { 0, 13, 1, 14 } } },
				{ FVector(2, 13, 15), FVector(14, 14, 16), false, {
					{ 2, 0, 14, 1 }, { 2, 15, 14, 16 }, { 2, 2, 14, 3 }, { 2, 2, 14, 3 }, { 15, 2, 16, 3 }, { 0, 2, 1, 3 } } },
				{ FVector(2, 3, 15), FVector(3, 13, 16), false, {
					{ -1, -1, -1, -1 }, { -1, -1, -1, -1 }, { 13, 3, 14, 13 }, { 2, 3, 3, 13 }, { 15, 3, 16, 13 }, { 0, 3, 1, 13 } } },
				{ FVector(13, 3, 15), FVector(14, 13, 16), false, {
					{ -1, -1, -1, -1 }, { -1, -1, -1, -1 }, { 2, 3, 3, 13 }, { 13, 3, 14, 13 }, { 15, 3, 16, 13 }, { 0, 3, 1, 13 } } },
			};
			OutNum = UE_ARRAY_COUNT(Elements);
			return Elements;
		}
	}

	int32 GetDecorLayers(const McDecor::FDecor& Decor)
	{
		switch (Decor.Kind)
		{
		case McDecor::EKind::Painting: return 2;
		case McDecor::EKind::ArmorStand: return 5;
		default: return Decor.Item.IsEmpty() ? 2 : 3;
		}
	}

	// One armor slot of a stand (layer 1 head, 2 chest, 3 legs, 4 feet): the armor model's parts as the game's model measured
	// them (pivot and cubes in its y-down axes), each turned by the pose of the stand part it covers. A small stand wears
	// them at half size.
	void BuildStandArmor(const McDecor::FDecor& Decor, int32 Layer, McPlayerModel::FMeshData& Out)
	{
		const float Scale = Decor.bSmall ? 0.5f : 1.0f;
		const auto Make = [&](McDecor::EPart Pose, FVector Pivot, std::initializer_list<FBox> Boxes) -> FPart
		{
			FPart Result;
			const float Down = Decor.bSmall ? 12.0f + Pivot.Y * Scale : Pivot.Y;
			Result.Pivot = FVector(Pivot.X * Scale, 24.0 - Down, -Pivot.Z);
			Result.Scale = Scale;
			Result.Boxes = Boxes;
			const FVector3f Angles = Decor.Pose[static_cast<int32>(Pose)];
			Result.RestPitch = -FMath::DegreesToRadians(Angles.X);
			Result.RestYaw = FMath::DegreesToRadians(Angles.Y);
			Result.RestRoll = FMath::DegreesToRadians(Angles.Z);
			return Result;
		};
		FModel Armor;
		Armor.TextureSize = FIntPoint(64, 32);
		switch (Layer)
		{
		case 1:
			Armor.Parts.Add(Make(McDecor::EPart::Head, FVector(0, 1, 0), { Box(0, 0, -4, 0, -4, 8, 8, 8, 1.0f), Box(32, 0, -4, 0, -4, 8, 8, 8, 1.5f) }));
			break;
		case 2:
			Armor.Parts.Add(Make(McDecor::EPart::Body, FVector(0, 0, 0), { Box(16, 16, -4, -12, -2, 8, 12, 4, 1.0f) }));
			Armor.Parts.Add(Make(McDecor::EPart::RightArm, FVector(-5, 2, 0), { Box(40, 16, -3, -10, -2, 4, 12, 4, 1.0f) }));
			Armor.Parts.Add(Make(McDecor::EPart::LeftArm, FVector(5, 2, 0), { Box(40, 16, -1, -10, -2, 4, 12, 4, 1.0f, true) }));
			break;
		case 3:
			Armor.Parts.Add(Make(McDecor::EPart::Body, FVector(0, 0, 0), { Box(16, 16, -4, -12, -2, 8, 12, 4, 0.5f) }));
			Armor.Parts.Add(Make(McDecor::EPart::RightLeg, FVector(-1.9f, 11, 0), { Box(0, 16, -2, -12, -2, 4, 12, 4, 0.4f) }));
			Armor.Parts.Add(Make(McDecor::EPart::LeftLeg, FVector(1.9f, 11, 0), { Box(0, 16, -2, -12, -2, 4, 12, 4, 0.4f, true) }));
			break;
		default:
			Armor.Parts.Add(Make(McDecor::EPart::RightLeg, FVector(-1.9f, 11, 0), { Box(0, 16, -2, -12, -2, 4, 12, 4, 0.9f) }));
			Armor.Parts.Add(Make(McDecor::EPart::LeftLeg, FVector(1.9f, 11, 0), { Box(0, 16, -2, -12, -2, 4, 12, 4, 0.9f, true) }));
			break;
		}
		FAnim Anim;
		Anim.BodyYaw = Decor.Yaw;
		Anim.HeadYaw = Decor.Yaw;
		BuildMesh(Armor, Anim, 0, Out);
	}

	void BuildDecorMesh(const McDecor::FDecor& Decor, int32 Layer, FVector2D PictureBlocks, float ItemVMax, McPlayerModel::FMeshData& Out)
	{
		FVector U, V, N;
		GetDecorBasis(Decor.Facing, U, V, N);
		const bool bGlow = Decor.Kind == McDecor::EKind::GlowItemFrame;
		if (Decor.Kind == McDecor::EKind::Painting)
		{
			// A thin slab about the entity: its picture on the front (layer 0), its back and edges plain (layer 1).
			const double Thickness = 1.0 / 16.0;
			const FVector Front = N * (Thickness * 0.5);
			const FVector4 Whole(0, 0, 1, 1);
			if (Layer == 0)
			{
				AddDecorPicture(Front, U, V, PictureBlocks.X, PictureBlocks.Y, Whole, false, false, Out);
			}
			else
			{
				AddDecorPicture(-Front, U, V, PictureBlocks.X, PictureBlocks.Y, Whole, true, false, Out);
				AddDecorPicture(U * (PictureBlocks.X * 0.5), N, V, Thickness, PictureBlocks.Y, Whole, false, false, Out);
				AddDecorPicture(-U * (PictureBlocks.X * 0.5), N, V, Thickness, PictureBlocks.Y, Whole, true, false, Out);
				AddDecorPicture(V * (PictureBlocks.Y * 0.5), U, N, PictureBlocks.X, Thickness, Whole, false, false, Out);
				AddDecorPicture(-V * (PictureBlocks.Y * 0.5), U, N, PictureBlocks.X, Thickness, Whole, true, false, Out);
			}
			return;
		}
		if (Decor.Kind == McDecor::EKind::ArmorStand)
		{
			// The stand's parts as the game's model measured them (pivot and cubes in its y-down, z-back axes; here y up and
			// z forward), a small stand's parts scaled about their own pivots. Poses turn a part by the game's angles.
			if (Layer > 0)
			{
				BuildStandArmor(Decor, Layer, Out);
				return;
			}
			const float Scale = Decor.bSmall ? 0.5f : 1.0f;
			const float HeadScale = Decor.bSmall ? 0.75f : 1.0f;
			const auto Make = [&](McDecor::EPart Pose, bool bHasPose, FVector Pivot, float PartScale, std::initializer_list<FBox> Boxes) -> FPart
			{
				FPart Result;
				Result.Pivot = FVector(Pivot.X, 24.0 - Pivot.Y, -Pivot.Z);
				Result.Scale = PartScale;
				Result.Boxes = Boxes;
				if (bHasPose)
				{
					const FVector3f Angles = Decor.Pose[static_cast<int32>(Pose)];
					Result.RestPitch = -FMath::DegreesToRadians(Angles.X);
					Result.RestYaw = FMath::DegreesToRadians(Angles.Y);
					Result.RestRoll = FMath::DegreesToRadians(Angles.Z);
				}
				return Result;
			};
			static const McDecor::EPart None = McDecor::EPart::Head;
			FModel Stand;
			Stand.TextureSize = FIntPoint(64, 64);
			const auto Rest = [&](float Big, float Small) { return Decor.bSmall ? Small : Big; };
			// Box(U, V, minX, -maxY, -maxZ, W, H, D) from the cubes' x, y and z extents.
			Stand.Parts.Add(Make(McDecor::EPart::Body, true, FVector(0, Rest(0, 12), 0), Scale, { Box(16, 0, -3, -10, -1, 2, 7, 2), Box(48, 16, 1, -10, -1, 2, 7, 2), Box(0, 48, -4, -12, -1, 8, 2, 2) }));
			if (!Decor.bInvisible)
			{
				Stand.Parts.Add(Make(McDecor::EPart::Body, true, FVector(0, Rest(0, 12), 0), Scale, { Box(0, 26, -6, -3, -1.5f, 12, 3, 3) }));
				Stand.Parts.Add(Make(McDecor::EPart::RightLeg, true, FVector(-1.9f * Scale, Rest(12, 18), 0), Scale, { Box(8, 0, -1, -11, -1, 2, 11, 2) }));
				Stand.Parts.Add(Make(McDecor::EPart::LeftLeg, true, FVector(1.9f * Scale, Rest(12, 18), 0), Scale, { Box(40, 16, -1, -11, -1, 2, 11, 2) }));
				if (Decor.bArms)
				{
					Stand.Parts.Add(Make(McDecor::EPart::RightArm, true, FVector(-5.0f * Scale, Rest(2, 13), 0), Scale, { Box(24, 0, -2, -10, -1, 2, 12, 2) }));
					Stand.Parts.Add(Make(McDecor::EPart::LeftArm, true, FVector(5.0f * Scale, Rest(2, 13), 0), Scale, { Box(32, 16, 0, -10, -1, 2, 12, 2) }));
				}
			}
			Stand.Parts.Add(Make(McDecor::EPart::Head, true, FVector(0, Decor.bSmall ? 12.75f : 1.0f, 0), HeadScale, { Box(0, 0, -1, 0, -1, 2, 7, 2) }));
			if (Decor.bBasePlate)
			{
				Stand.Parts.Add(Make(None, false, FVector(0, Decor.bSmall ? 18.0f : 12.0f, 0), Scale, { Box(0, 32, -6, -12, -6, 12, 1, 12) }));
			}
			FAnim Anim;
			Anim.BodyYaw = Decor.Yaw;
			Anim.HeadYaw = Decor.Yaw;
			BuildMesh(Stand, Anim, 0, Out);
			return;
		}
		if (Decor.Kind != McDecor::EKind::ItemFrame && Decor.Kind != McDecor::EKind::GlowItemFrame)
		{
			return;
		}
		if (Layer == 2)
		{
			// The item stands flat in the frame, half a block across, turned by eighths of a turn.
			const double Angle = FMath::DegreesToRadians(-45.0 * (Decor.ItemRotation & 7));
			const FVector RotU = U * FMath::Cos(Angle) + V * FMath::Sin(Angle);
			const FVector RotV = V * FMath::Cos(Angle) - U * FMath::Sin(Angle);
			const FVector4 Rect(0, 0, 1, ItemVMax);
			AddDecorPicture(N * (-1.0 / 32.0), RotU, RotV, 0.5, 0.5, Rect, false, bGlow, Out);
			AddDecorPicture(N * (-1.0 / 32.0), RotU, RotV, 0.5, 0.5, Rect, true, bGlow, Out);
			return;
		}
		if (Decor.bInvisible)
		{
			return;
		}
		int32 NumElements = 0;
		const FFrameElement* Elements = GetFrameElements(NumElements);
		// The model hangs on a wall to the south and the plate's centre is the entity: block pixels (x, y, z) become
		// U to the viewer's right, V up and N out of the wall.
		const auto Place = [&](double X, double Y, double Z)
		{
			return U * ((8.0 - X) / 16.0) + V * ((Y - 8.0) / 16.0) - N * ((Z - 15.5) / 16.0);
		};
		for (int32 Index = 0; Index < NumElements; ++Index)
		{
			const FFrameElement& Element = Elements[Index];
			if (Element.bBack != (Layer == 1))
			{
				continue;
			}
			const double MinX = Element.From.X, MinY = Element.From.Y, MinZ = Element.From.Z;
			const double MaxX = Element.To.X, MaxY = Element.To.Y, MaxZ = Element.To.Z;
			const FVector Corners[6][4] = {
				{ Place(MinX, MinY, MinZ), Place(MaxX, MinY, MinZ), Place(MaxX, MinY, MaxZ), Place(MinX, MinY, MaxZ) },  // down
				{ Place(MinX, MaxY, MaxZ), Place(MaxX, MaxY, MaxZ), Place(MaxX, MaxY, MinZ), Place(MinX, MaxY, MinZ) },  // up
				{ Place(MaxX, MinY, MinZ), Place(MinX, MinY, MinZ), Place(MinX, MaxY, MinZ), Place(MaxX, MaxY, MinZ) },  // north
				{ Place(MinX, MinY, MaxZ), Place(MaxX, MinY, MaxZ), Place(MaxX, MaxY, MaxZ), Place(MinX, MaxY, MaxZ) },  // south
				{ Place(MinX, MinY, MinZ), Place(MinX, MinY, MaxZ), Place(MinX, MaxY, MaxZ), Place(MinX, MaxY, MinZ) },  // west
				{ Place(MaxX, MinY, MaxZ), Place(MaxX, MinY, MinZ), Place(MaxX, MaxY, MinZ), Place(MaxX, MaxY, MaxZ) },  // east
			};
			for (int32 Face = 0; Face < 6; ++Face)
			{
				const int32* Rect = Element.Faces[Face];
				if (Rect[0] < 0)
				{
					continue;
				}
				const FVector2D UVs[4] = {
					FVector2D(Rect[0] / 16.0, Rect[3] / 16.0), FVector2D(Rect[2] / 16.0, Rect[3] / 16.0),
					FVector2D(Rect[2] / 16.0, Rect[1] / 16.0), FVector2D(Rect[0] / 16.0, Rect[1] / 16.0),
				};
				AddDecorQuad(Corners[Face], UVs, false, Out);
			}
		}
	}

	void BuildMesh(const FModel& Model, const FAnim& Anim, int32 Layer, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		for (const FPart& Part : Model.Parts)
		{
			if (Part.Layer != Layer)
			{
				continue;
			}
			float Pitch = Part.RestPitch;
			float Yaw = Part.RestYaw;
			float Roll = Part.RestRoll;
			const float RestPitch = Pitch;
			const bool bGlow = Part.bGlow || ((Model.GlowLayers >> Layer) & 1u) != 0 || Anim.bFlash;
			const float Pos = Anim.WalkPos;
			const float Amount = Anim.WalkAmount;
			switch (Part.Role)
			{
			case ERole::Head:
				Pitch += FMath::DegreesToRadians(Anim.HeadPitch);
				Yaw -= FMath::DegreesToRadians(FMath::Clamp(FMath::UnwindDegrees(Anim.HeadYaw - Anim.BodyYaw), -75.0f, 75.0f));
				break;
			case ERole::LegFrontRight:
			case ERole::LegBackLeft:
			case ERole::LegRight:
				Pitch += Swing(Pos, Amount, 0.0f);
				break;
			case ERole::LegFrontLeft:
			case ERole::LegBackRight:
			case ERole::LegLeft:
				Pitch += Swing(Pos, Amount, PI);
				break;
			case ERole::ArmRight:
			case ERole::ArmLeft:
			{
				const bool bRight = Part.Role == ERole::ArmRight;
				if (Model.bArmsForward)
				{
					// Out in front, swaying a little as it walks.
					Pitch += -HALF_PI + FMath::Cos(Pos * 0.6662f + (bRight ? PI : 0.0f)) * 0.2f * Amount + FMath::Sin(Anim.Age * 1.2f) * 0.04f;
				}
				else
				{
					Pitch += FMath::Cos(Pos * 0.6662f + (bRight ? PI : 0.0f)) * Amount;
				}
				break;
			}
			case ERole::WingRight:
			case ERole::WingLeft:
			{
				// Folded in, and beating when it runs.
				const float Side = Part.Role == ERole::WingRight ? 1.0f : -1.0f;
				Roll += Side * (0.08f + (Amount > 0.4f ? FMath::Abs(FMath::Sin(Anim.Age * 25.0f)) * 0.9f : 0.0f));
				break;
			}
			case ERole::SpiderLeg:
			{
				const int32 Row = Part.Index / 2;
				const bool bLeft = (Part.Index & 1) != 0;
				const float Phase = (Row & 1 ? PI : 0.0f) + (bLeft ? PI * 0.5f : 0.0f);
				const float Side = bLeft ? -1.0f : 1.0f;
				Yaw += Side * -FMath::Cos(Pos * 1.3324f + Phase) * 0.4f * Amount;
				Roll += Side * -FMath::Abs(FMath::Sin(Pos * 0.6662f + Phase)) * 0.4f * Amount;
				break;
			}
			default:
				break;
			}
			if (Model.MaxLimbSwing > 0.0f && (Part.Role == ERole::LegRight || Part.Role == ERole::LegLeft || Part.Role == ERole::ArmRight || Part.Role == ERole::ArmLeft))
			{
				Pitch = RestPitch + FMath::Clamp(Pitch - RestPitch, -Model.MaxLimbSwing, Model.MaxLimbSwing);
			}
			// Turn, tip and lean, in the order the player's parts are turned.
			const FQuat Rotation = FQuat(FVector::YAxisVector, Yaw) * FQuat(FVector::ZAxisVector, Roll) * FQuat(FVector::XAxisVector, Pitch);
			for (const FBox& Box : Part.Boxes)
			{
				AddBox(Box, Part, Rotation, Part.Pivot, Anim.BodyYaw, Model.TextureSize, Anim.Hurt, bGlow, Out);
			}
		}
	}

	int32 GetItemCopies(int32 Count)
	{
		return Count > 48 ? 5 : Count > 32 ? 4 : Count > 16 ? 3 : Count > 1 ? 2 : 1;
	}

	FVector2D GetSwellScale(float Swelling)
	{
		const float S = FMath::Clamp(Swelling, 0.0f, 1.0f);
		// A little shake that grows with the fuse, and the swelling itself, slow at first and quick at the end.
		const float Shake = 1.0f + FMath::Sin(S * 100.0f) * S * 0.01f;
		const float Grow = S * S * S * S;
		return FVector2D((1.0f + Grow * 0.4f) * Shake, (1.0f + Grow * 0.1f) / Shake);
	}

	bool IsSwellFlash(float Swelling)
	{
		return FMath::FloorToInt32(FMath::Clamp(Swelling, 0.0f, 1.0f) * 10.0f) % 2 == 1;
	}

	void FSquish::Tick(float Rise)
	{
		if (Rise > 0.05f && LastRise <= 0.01f)
		{
			Target = 1.0f;
		}
		else if (Rise > -0.01f && LastRise < -0.05f)
		{
			Target = -0.5f;
		}
		LastRise = Rise;
		Previous = Value;
		Value += (Target - Value) * 0.5f;
		Target *= 0.6f;
	}

	FVector2D FSquish::GetScale(float Alpha, float Size) const
	{
		const float Amount = FMath::Lerp(Previous, Value, FMath::Clamp(Alpha, 0.0f, 1.0f)) / (Size * 0.5f + 1.0f);
		const float Across = 1.0f / (Amount + 1.0f);
		return FVector2D(Across, 1.0f / Across);
	}

	void BuildArrowMesh(float BodyYaw, float Pitch, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		// The arrow lies along z (its tip at +z), turned an eighth so the two shafts make an X from the front. Pixels: the
		// game draws it 16 units long at 0.05625 of a block a unit, 0.9 of a pixel. The picture is 32 square: the
		// fletching at the top left (5 square) and the shaft below it (16 by 5).
		constexpr float Unit = 0.9f;
		constexpr float Length = 8.0f * Unit;
		constexpr float Half = 2.0f * Unit;
		const FQuat Pose = FQuat(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw)) * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Pitch))
			* FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(45.0f));
		// Half a hitbox up from the feet.
		const FVector Centre(0.0, 4.0, 0.0);
		const auto AddQuad = [&](const FVector (&Corners)[4], const FVector2D (&Pixels)[4], const FVector& Normal)
		{
			for (const bool bBack : { false, true })
			{
				const FVector FaceNormal = Pose.RotateVector(bBack ? -Normal : Normal);
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector Model = Centre + Pose.RotateVector(Corners[Corner]);
					Out.Positions.Add(FVector(Model.X, Model.Z, Model.Y) * (McCoords::UnitsPerBlock / 16.0));
					Out.Normals.Add(FVector(FaceNormal.X, FaceNormal.Z, FaceNormal.Y));
					Out.UV0.Add(Pixels[Corner] / 32.0);
					Out.Colors.Add(FColor::White);
				}
				if (bBack)
				{
					Out.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
				}
				else
				{
					Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
				}
			}
		};
		// The shaft: one plane up and down, one across.
		const FVector2D ShaftPixels[4] = { FVector2D(0, 10), FVector2D(16, 10), FVector2D(16, 5), FVector2D(0, 5) };
		const FVector Up[4] = { FVector(0, -Half, -Length), FVector(0, -Half, Length), FVector(0, Half, Length), FVector(0, Half, -Length) };
		AddQuad(Up, ShaftPixels, FVector(1, 0, 0));
		const FVector Across[4] = { FVector(-Half, 0, -Length), FVector(-Half, 0, Length), FVector(Half, 0, Length), FVector(Half, 0, -Length) };
		AddQuad(Across, ShaftPixels, FVector(0, 1, 0));
		// The fletching at the tail.
		const FVector2D FletchPixels[4] = { FVector2D(0, 5), FVector2D(5, 5), FVector2D(5, 0), FVector2D(0, 0) };
		const float Tail = -7.0f * Unit;
		const FVector Fletch[4] = { FVector(-Half, -Half, Tail), FVector(Half, -Half, Tail), FVector(Half, Half, Tail), FVector(-Half, Half, Tail) };
		AddQuad(Fletch, FletchPixels, FVector(0, 0, -1));
	}

	// A thrown item: its picture 0.25 across on two squares crossed at the middle of the 0.25 body, seen from any side.
	void BuildTntMesh(int32 Layer, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double Half = 0.49 * McCoords::UnitsPerBlock;
		const double Tall = 0.98 * McCoords::UnitsPerBlock;
		// Faces as (normal, right, up) with the cube centred over the feet; both sides of each are drawn so the winding is moot.
		struct FFace { FVector Normal, Right, Up; double Lift; };
		TArray<FFace> Faces;
		if (Layer == 0)
		{
			Faces = { { FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), 0.0 }, { FVector(-1, 0, 0), FVector(0, -1, 0), FVector(0, 0, 1), 0.0 },
				{ FVector(0, 1, 0), FVector(-1, 0, 0), FVector(0, 0, 1), 0.0 }, { FVector(0, -1, 0), FVector(1, 0, 0), FVector(0, 0, 1), 0.0 } };
		}
		else
		{
			Faces = { { FVector(0, 0, Layer == 1 ? 1 : -1), FVector(1, 0, 0), FVector(0, 1, 0), 0.0 } };
		}
		const FVector2D UVs[4] = { FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0), FVector2D(0, 0) };
		for (const FFace& Face : Faces)
		{
			const bool bSide = Layer == 0;
			const FVector Centre = Face.Normal * Half + FVector(0, 0, Tall * 0.5) + (bSide ? FVector::ZeroVector : Face.Normal * (Tall * 0.5 - Half));
			const double Across = Half, Up = bSide ? Tall * 0.5 : Half;
			const FVector Corners[4] = { Centre - Face.Right * Across - Face.Up * Up, Centre + Face.Right * Across - Face.Up * Up,
				Centre + Face.Right * Across + Face.Up * Up, Centre - Face.Right * Across + Face.Up * Up };
			for (const bool bBack : { false, true })
			{
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const int32 Index = bBack ? 3 - Corner : Corner;
					Out.Positions.Add(Corners[Index]);
					Out.Normals.Add(bBack ? -Face.Normal : Face.Normal);
					Out.UV0.Add(UVs[Index]);
					Out.Colors.Add(FColor::White);
				}
				Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			}
		}
	}

	void BuildThrownMesh(float VMax, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double Units = McCoords::UnitsPerBlock;
		const double Half = 0.125 * Units;
		const FVector2D UVs[4] = { FVector2D(0, VMax), FVector2D(1, VMax), FVector2D(1, 0), FVector2D(0, 0) };
		for (const FVector& Across : { FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0) })
		{
			const FVector Normal(Across.Y, Across.X, 0.0);
			const FVector Centre(0.0, 0.0, Half);
			const FVector Corners[4] = { Centre - Across * Half - FVector(0, 0, Half), Centre + Across * Half - FVector(0, 0, Half),
				Centre + Across * Half + FVector(0, 0, Half), Centre - Across * Half + FVector(0, 0, Half) };
			for (const bool bBack : { false, true })
			{
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Out.Positions.Add(Corners[bBack ? 3 - Corner : Corner]);
					Out.Normals.Add(bBack ? -Normal : Normal);
					Out.UV0.Add(UVs[bBack ? 3 - Corner : Corner]);
					Out.Colors.Add(FColor::White);
				}
				Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			}
		}
	}

	void BuildItemMesh(float Age, int32 Count, float Width, float VMax, McPlayerModel::FMeshData& Out)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		const double Units = McCoords::UnitsPerBlock;
		// Floats a little above the ground, bobbing, and turns.
		const double Height = (0.2 + FMath::Sin(Age * 2.0f) * 0.05 + 0.05 + Width * 0.5) * Units;
		const double Spin = Age * FMath::DegreesToRadians(70.0f);
		const FQuat Turn(FVector::ZAxisVector, Spin);
		const int32 Copies = GetItemCopies(Count);
		const double Half = Width * 0.5 * Units;
		for (int32 Copy = 0; Copy < Copies; ++Copy)
		{
			// The copies are scattered a little around the middle.
			const FVector Offset = Copies == 1 ? FVector::ZeroVector
				: FVector(FMath::Sin(Copy * 2.4) * 0.07, FMath::Cos(Copy * 2.4) * 0.07, 0.0) * Units;
			for (const bool bBack : { false, true })
			{
				const FVector Normal = Turn.RotateVector(FVector(0.0, bBack ? -1.0 : 1.0, 0.0));
				const FVector Across = Turn.RotateVector(FVector(bBack ? -1.0 : 1.0, 0.0, 0.0));
				const FVector Centre = FVector(0.0, 0.0, Height) + Turn.RotateVector(Offset);
				const FVector Corners[4] = {
					Centre - Across * Half - FVector(0, 0, Half), Centre + Across * Half - FVector(0, 0, Half),
					Centre + Across * Half + FVector(0, 0, Half), Centre - Across * Half + FVector(0, 0, Half),
				};
				const FVector2D UVs[4] = { FVector2D(0, VMax), FVector2D(1, VMax), FVector2D(1, 0), FVector2D(0, 0) };
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Out.Positions.Add(Corners[Corner]);
					Out.Normals.Add(Normal);
					Out.UV0.Add(UVs[Corner]);
					Out.Colors.Add(FColor::White);
				}
				if (bBack)
				{
					Out.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
				}
				else
				{
					Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
				}
			}
		}
	}
}

namespace
{
	// Pictures stay loaded for as long as the game runs; there are few of them.
	TMap<FString, TStrongObjectPtr<UTexture2DArray>>& TextureCache()
	{
		static TMap<FString, TStrongObjectPtr<UTexture2DArray>> Cache;
		return Cache;
	}

	struct FLoaded
	{
		UTexture2DArray* Texture = nullptr;
		int32 Width = 0;
		int32 Height = 0;
	};

	TMap<FString, FIntPoint>& SizeCache()
	{
		static TMap<FString, FIntPoint> Cache;
		return Cache;
	}

	// Loads (once) the picture at Path; null if it isn't there.
	UTexture2DArray* LoadCached(const FString& Path, FIntPoint& OutSize, bool bWhite = false)
	{
		const FString Key = bWhite ? Path + TEXT("#white") : Path;
		if (const TStrongObjectPtr<UTexture2DArray>* Found = TextureCache().Find(Key))
		{
			OutSize = SizeCache().FindRef(Key);
			return Found->Get();
		}
		int32 Width = 0, Height = 0;
		// Kept for every world that comes after, so not inside this one.
		UTexture2DArray* Texture = McRenderTextures::LoadPngAsArray(GetTransientPackage(), Path, Width, Height, bWhite);
		TextureCache().Add(Key, TStrongObjectPtr<UTexture2DArray>(Texture));
		SizeCache().Add(Key, FIntPoint(Width, Height));
		OutSize = FIntPoint(Width, Height);
		return Texture;
	}

	// The texture of an item: its own picture, or its block's side, top or front.
	FString FindItemTexture(FName Item)
	{
		const FString Name = McCache::PathOf(Item.ToString());
		for (const FString& Candidate : { TEXT("item/") + Name, TEXT("block/") + Name, TEXT("block/") + Name + TEXT("_side"), TEXT("block/") + Name + TEXT("_top"), TEXT("block/") + Name + TEXT("_front") })
		{
			const FString Path = McCache::Asset(TEXT("textures/") + Candidate + TEXT(".png"));
			if (IFileManager::Get().FileExists(*Path))
			{
				return Path;
			}
		}
		return FString();
	}
}

UMcEntityModelComponent::UMcEntityModelComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetUsingAbsoluteRotation(true);
	bUseAsyncCooking = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cutout(TEXT("/Game/Mc/Materials/M_McBlockCutout.M_McBlockCutout"));
	BaseMaterial = Cutout.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Translucent(TEXT("/Game/Mc/Materials/M_McBlockTranslucent.M_McBlockTranslucent"));
	TranslucentMaterial = Translucent.Object;
}

void UMcEntityModelComponent::SetKind(FName InKind, FName InItem, int32 InCount)
{
	if (Kind == InKind && Item == InItem)
	{
		Count = InCount;
		return;
	}
	Kind = InKind;
	Item = InItem;
	Count = InCount;
	bIsItem = Kind == TEXT("minecraft:item") || Kind == TEXT("minecraft:snowball") || Kind == TEXT("minecraft:egg") || Kind == TEXT("minecraft:ender_pearl");
	bIsArrow = Kind == TEXT("minecraft:arrow");
	bIsThrown = Kind == TEXT("minecraft:snowball") || Kind == TEXT("minecraft:egg") || Kind == TEXT("minecraft:ender_pearl");
	bIsBanner = Kind == TEXT("mc:banner");
	bIsTnt = Kind == TEXT("minecraft:tnt");
	bIsDecor = Kind == TEXT("mc:decor") && McDecor::Decode(Item.ToString(), Decor);
	if (bIsBanner)
	{
		McBanner::Decode(Item.ToString(), BannerBase, bBannerWall, BannerLayers);
	}
	Model = bIsItem || bIsArrow || bIsBanner || bIsTnt || bIsDecor ? nullptr : McEntityModel::Find(Kind);
	Materials.Reset();
	ClearAllMeshSections();
	NumSections = 0;
	bItemFailed = false;
	bFlashing = false;
	bDirty = true;
}

bool UMcEntityModelComponent::HasModel() const
{
	return bIsItem || bIsArrow || bIsBanner || bIsTnt || bIsDecor || Model != nullptr;
}

void UMcEntityModelComponent::SetHitboxWidth(float InWidth)
{
	HitboxWidth = InWidth;
}

void UMcEntityModelComponent::SetSwelling(float InSwelling)
{
	TargetSwelling = FMath::Clamp(InSwelling, 0.0f, 1.0f);
}

void UMcEntityModelComponent::SetLook(float InBodyYaw, float InHeadYaw, float InHeadPitch)
{
	TargetBodyYaw = InBodyYaw;
	TargetHeadYaw = InHeadYaw;
	TargetHeadPitch = InHeadPitch;
	if (!bHasLook)
	{
		bHasLook = true;
		Anim.BodyYaw = InBodyYaw;
		Anim.HeadYaw = InHeadYaw;
		Anim.HeadPitch = InHeadPitch;
	}
}

void UMcEntityModelComponent::SetHurtCount(int32 InHurtCount)
{
	// The first count seen is how it already was; each rise after that is a hit.
	if (HurtCount >= 0 && InHurtCount != HurtCount)
	{
		Anim.Hurt = 1.0f;
	}
	HurtCount = InHurtCount;
}

FString UMcEntityModelComponent::GetTexturePath(int32 Layer) const
{
	if (bIsItem)
	{
		return FindItemTexture(Item);
	}
	if (bIsArrow)
	{
		return McCache::Asset(TEXT("textures/entity/projectiles/arrow.png"));
	}
	if (bIsTnt)
	{
		static const TCHAR* Names[3] = { TEXT("tnt_side"), TEXT("tnt_top"), TEXT("tnt_bottom") };
		return McCache::Asset(FString(TEXT("textures/block/")) + Names[FMath::Clamp(Layer, 0, 2)] + TEXT(".png"));
	}
	if (bIsDecor)
	{
		switch (Decor.Kind)
		{
		case McDecor::EKind::Painting:
			return McCache::Asset(TEXT("textures/painting/") + (Layer == 0 ? Decor.Variant : FString(TEXT("back"))) + TEXT(".png"));
		case McDecor::EKind::ArmorStand:
			if (Layer > 0)
			{
				// The worn piece's material names its picture; leggings have a sheet of their own.
				FString Name = Decor.Equipment[Layer - 1];
				if (Name.IsEmpty())
				{
					return FString();
				}
				Name.RemoveFromStart(TEXT("minecraft:"));
				for (const TCHAR* Suffix : { TEXT("_helmet"), TEXT("_chestplate"), TEXT("_leggings"), TEXT("_boots") })
				{
					Name.RemoveFromEnd(Suffix);
				}
				Name = Name == TEXT("golden") ? FString(TEXT("gold")) : Name == TEXT("turtle") ? FString(TEXT("turtle_scute")) : Name;
				return McCache::Asset(FString(TEXT("textures/entity/equipment/")) + (Layer == 3 ? TEXT("humanoid_leggings/") : TEXT("humanoid/")) + Name + TEXT(".png"));
			}
			return McCache::Asset(TEXT("textures/entity/armorstand/armorstand.png"));
		default:
			if (Layer == 2)
			{
				return FindItemTexture(FName(*Decor.Item));
			}
			return McCache::Asset(Layer == 0 ? FString(TEXT("textures/block/birch_planks.png"))
				: FString(Decor.Kind == McDecor::EKind::GlowItemFrame ? TEXT("textures/block/glow_item_frame.png") : TEXT("textures/block/item_frame.png")));
		}
	}
	if (bIsBanner)
	{
		// The pole and bar have a picture of their own; the flag has the base and then one for each pattern.
		if (Layer == 0)
		{
			return McCache::Asset(TEXT("textures/entity/banner/banner_base.png"));
		}
		return McCache::Asset(TEXT("textures/entity/banner/") + (Layer < 2 ? FString(TEXT("base")) : BannerLayers.IsValidIndex(Layer - 2) ? BannerLayers[Layer - 2].Pattern : FString(TEXT("base"))) + TEXT(".png"));
	}
	if (Kind == TEXT("minecraft:boat"))
	{
		FString Wood = Item.IsNone() ? FString(TEXT("oak")) : Item.ToString();
		Wood.RemoveFromStart(TEXT("minecraft:"));
		return McCache::Asset(TEXT("textures/entity/boat/") + Wood + TEXT(".png"));
	}
	if (Model && Model->Textures.IsValidIndex(Layer))
	{
		return McCache::Asset(TEXT("textures/entity/") + Model->Textures[Layer]);
	}
	return FString();
}

UMaterialInstanceDynamic* UMcEntityModelComponent::GetMaterial(int32 Layer)
{
	if (Materials.IsValidIndex(Layer) && Materials[Layer])
	{
		return Materials[Layer];
	}
	const bool bTranslucent = Model && ((Model->TranslucentLayers >> Layer) & 1u) != 0;
	UMaterialInterface* Parent = bTranslucent && TranslucentMaterial ? TranslucentMaterial.Get() : BaseMaterial.Get();
	if (!Parent)
	{
		return nullptr;
	}
	const FString Path = GetTexturePath(Layer);
	if (Path.IsEmpty())
	{
		return nullptr;
	}
	FIntPoint Size;
	UTexture2DArray* Texture = LoadCached(Path, Size);
	if (!Texture)
	{
		return nullptr;
	}
	if (bIsItem || (bIsDecor && Layer == 2 && Decor.Kind != McDecor::EKind::Painting))
	{
		// An animated item is a column of frames; the first one is the square on top.
		ItemVMax = Size.Y > Size.X && Size.X > 0 ? static_cast<float>(Size.X) / Size.Y : 1.0f;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(McRenderMode::Pick(Parent), this);
	MaterialGeneration = McRenderMode::GetGeneration();
	Material->SetTextureParameterValue(TEXT("Blocks"), Texture);
	McRenderMode::ApplyHdPicture(Material, Path);
	if (Materials.Num() <= Layer)
	{
		Materials.SetNum(Layer + 1);
	}
	Materials[Layer] = Material;
	return Material;
}

void UMcEntityModelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AActor* Owner = GetOwner();
	if (!Owner || !HasModel() || ((bIsItem || bIsArrow) && bItemFailed))
	{
		return;
	}
	Anim.Age += DeltaTime;
	if (Materials.Num() > 0 && MaterialGeneration != McRenderMode::GetGeneration())
	{
		// The look changed: the materials are made again, and the mesh with them.
		Materials.Reset();
		ClearAllMeshSections();
		NumSections = 0;
		bFlashing = false;
	}

	// Walking, from how far the entity moved: the same 20 ticks a second as the game.
	const FVector Feet = McCoords::UnrealToBlock(Owner->GetActorLocation());
	const FVector2D Position(Feet.X, Feet.Z);
	if (!bHasPosition)
	{
		LastPosition = Position;
		bHasPosition = true;
	}
	TickAccumulator += DeltaTime;
	int32 Ticks = 0;
	while (TickAccumulator >= 0.05f)
	{
		TickAccumulator -= 0.05f;
		if (++Ticks > 5)
		{
			continue;
		}
		double Moved = FVector2D::Distance(Position, LastPosition);
		if (Moved > 8.0)
		{
			Moved = 0.0;
		}
		// The distance was over every tick since last time: spread it.
		LastPosition = Position;
		Squish.Tick(bHasY ? static_cast<float>(Feet.Y - LastY) : 0.0f);
		LastY = Feet.Y;
		bHasY = true;
		Walking +=(FMath::Min(static_cast<float>(Moved) * 4.0f, 1.0f) - Walking) * 0.4f;
		Anim.WalkAmount = Walking;
		Anim.WalkPos += Walking;
	}
	Anim.Hurt = FMath::Max(0.0f, Anim.Hurt - DeltaTime * 2.5f);

	// Turn towards where the entity is told to face.
	const float Turn = FMath::Min(1.0f, DeltaTime * 12.0f);
	Anim.BodyYaw += FMath::UnwindDegrees(TargetBodyYaw - Anim.BodyYaw) * Turn;
	Anim.HeadYaw += FMath::UnwindDegrees(TargetHeadYaw - Anim.HeadYaw) * Turn;
	Anim.HeadPitch += (TargetHeadPitch - Anim.HeadPitch) * Turn;

	// A creeper's fuse swells it about its feet; a slime grows with its hitbox and squishes as it hops.
	Swelling += (TargetSwelling - Swelling) * FMath::Min(1.0f, DeltaTime * 20.0f);
	if (TargetSwelling == 0.0f && Swelling < 0.01f)
	{
		Swelling = 0.0f;
	}
	{
		FVector2D Swell = bIsTnt ? FVector2D(1.0, 1.0) : McEntityModel::GetSwellScale(Swelling);
		const bool bGrows = Model && Model->BaseWidth > 0.0f && HitboxWidth > 0.0f;
		const float Grow = bGrows ? HitboxWidth / Model->BaseWidth : 1.0f;
		if (bGrows)
		{
			Swell *= Squish.GetScale(TickAccumulator / 0.05f, Grow);
		}
		SetRelativeScale3D(FVector(Swell.X * Grow, Swell.X * Grow, Swell.Y * Grow));
	}

	if (UMaterialInstanceDynamic* Base = GetMaterial(0))
	{
		Base->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
		Base->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	}
	else
	{
		if (bIsItem || bIsArrow)
		{
			bItemFailed = true;
		}
		return;
	}
	if (bIsItem || bIsArrow)
	{
		McPlayerModel::FMeshData Data;
		if (bIsArrow)
		{
			McEntityModel::BuildArrowMesh(TargetBodyYaw, TargetHeadPitch, Data);
		}
		else if (bIsThrown)
		{
			McEntityModel::BuildThrownMesh(ItemVMax, Data);
		}
		else
		{
			McEntityModel::BuildItemMesh(Anim.Age, Count, 0.45f, ItemVMax, Data);
		}
		TArray<FVector2D> UV1, UV2;
		UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
		UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
		if (NumSections == 0)
		{
			CreateMeshSection(0, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
			SetMaterial(0, Materials[0]);
			NumSections = 1;
		}
		else
		{
			UpdateMeshSection(0, Data.Positions, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>());
		}
		return;
	}
	if (bIsTnt)
	{
		// Flashes white every other five ticks of the fuse.
		const int32 Fuse = FMath::RoundToInt32(TargetSwelling * 80.0f);
		const bool bFlash = (Fuse / 5) % 2 == 0;
		for (int32 Layer = 0; Layer < 3; ++Layer)
		{
			UMaterialInstanceDynamic* Material = GetMaterial(Layer);
			if (!Material)
			{
				continue;
			}
			Material->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
			Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
			if (bFlash != bFlashing || NumSections <= Layer)
			{
				FIntPoint Size;
				if (UTexture2DArray* Texture = LoadCached(GetTexturePath(Layer), Size, bFlash))
				{
					Material->SetTextureParameterValue(TEXT("Blocks"), Texture);
					McRenderMode::ApplyHdPicture(Material, GetTexturePath(Layer), bFlash);
				}
			}
			if (NumSections <= Layer)
			{
				McPlayerModel::FMeshData Data;
				McEntityModel::BuildTntMesh(Layer, Data);
				TArray<FVector2D> UV1, UV2;
				UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
				UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
				CreateMeshSection(Layer, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
				SetMaterial(Layer, Material);
				NumSections = Layer + 1;
			}
		}
		bFlashing = bFlash;
		return;
	}
	if (bIsDecor)
	{
		const int32 NumDecorLayers = McEntityModel::GetDecorLayers(Decor);
		FVector2D Picture(1.0, 1.0);
		if (Decor.Kind == McDecor::EKind::Painting)
		{
			// A painting is as many blocks across as its picture has sixteenths.
			FIntPoint Size;
			if (!LoadCached(GetTexturePath(0), Size))
			{
				return;
			}
			Picture = FVector2D(Size.X / 16.0, Size.Y / 16.0);
		}
		for (int32 Layer = 0; Layer < NumDecorLayers; ++Layer)
		{
			UMaterialInstanceDynamic* Material = GetMaterial(Layer);
			if (!Material)
			{
				continue;
			}
			const bool bGlow = Decor.Kind == McDecor::EKind::GlowItemFrame && Layer == 2;
			Material->SetScalarParameterValue(TEXT("SkyFactor"), bGlow ? 1.0f : McSky::GetSkyLightFactor());
			Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
			if (NumSections > Layer)
			{
				continue;
			}
			McPlayerModel::FMeshData Data;
			McEntityModel::BuildDecorMesh(Decor, Layer, Picture, ItemVMax, Data);
			if (Data.Positions.IsEmpty())
			{
				continue;
			}
			TArray<FVector2D> UV1, UV2;
			UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
			UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
			CreateMeshSection(Layer, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
			SetMaterial(Layer, Material);
			NumSections = Layer + 1;
		}
		return;
	}
	if (bIsBanner)
	{
		const int32 NumBannerLayers = 2 + BannerLayers.Num();
		for (int32 Layer = 0; Layer < NumBannerLayers; ++Layer)
		{
			UMaterialInstanceDynamic* Material = GetMaterial(Layer);
			if (!Material)
			{
				continue;
			}
			Material->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
			Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
			if (NumSections > Layer)
			{
				continue;
			}
			McPlayerModel::FMeshData Data;
			McEntityModel::BuildBannerMesh(bBannerWall, Layer > 0, TargetBodyYaw, Data);
			if (Layer > 0)
			{
				// The flag takes the dye's colour (the pole and bar keep the wood).
				const FColor Tint = McBanner::GetDyeColour(Layer == 1 ? BannerBase : BannerLayers[Layer - 2].Colour).ToFColor(true);
				for (FColor& Color : Data.Colors)
				{
					Color = FColor(static_cast<uint8>(Color.R * Tint.R / 255), static_cast<uint8>(Color.G * Tint.G / 255), static_cast<uint8>(Color.B * Tint.B / 255), 255);
				}
				// Each layer lies a hair above the one below, on both faces, so they do not fight over the same depth.
				for (int32 V = 0; V < Data.Positions.Num(); ++V)
				{
					Data.Positions[V] += FVector(Data.Normals[V]) * (0.1 * (Layer - 1));
				}
			}
			TArray<FVector2D> UV1, UV2;
			UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
			UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
			CreateMeshSection(Layer, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
			SetMaterial(Layer, Material);
			NumSections = Layer + 1;
		}
		return;
	}
	const int32 NumLayers = Model->Textures.Num();
	// Every other tenth of the fuse the creeper goes white.
	const bool bFlash = Swelling > 0.0f && McEntityModel::IsSwellFlash(Swelling);
	Anim.bFlash = bFlash;
	for (int32 Layer = 0; Layer < NumLayers; ++Layer)
	{
		UMaterialInstanceDynamic* Material = GetMaterial(Layer);
		if (!Material)
		{
			continue;
		}
		const bool bGlow = ((Model->GlowLayers >> Layer) & 1u) != 0;
		Material->SetScalarParameterValue(TEXT("SkyFactor"), bGlow ? 1.0f : McSky::GetSkyLightFactor());
		Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
		if (Layer == 0 && bFlash != bFlashing)
		{
			// The picture swapped for its white silhouette, and back.
			FIntPoint Size;
			if (UTexture2DArray* Texture = LoadCached(GetTexturePath(0), Size, bFlash))
			{
				Material->SetTextureParameterValue(TEXT("Blocks"), Texture);
				McRenderMode::ApplyHdPicture(Material, GetTexturePath(0), bFlash);
				bFlashing = bFlash;
			}
		}
		McPlayerModel::FMeshData Data;
		McEntityModel::BuildMesh(*Model, Anim, Layer, Data);
		TArray<FVector2D> UV1, UV2;
		UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
		UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
		if (NumSections <= Layer)
		{
			CreateMeshSection(Layer, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
			SetMaterial(Layer, Material);
			NumSections = Layer + 1;
		}
		else
		{
			UpdateMeshSection(Layer, Data.Positions, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>());
		}
	}
}
