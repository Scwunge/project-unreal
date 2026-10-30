#include "McEntityModel.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "McRenderMode.h"
#include "McCache.h"
#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTextures.h"
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

	// A boat's two paddles: a shaft and a blade each, cut from the boat's picture (the left and right pictures are where the texture has them: stacked on a boat's, side by side on a raft's).
	// They are mounted on the sides (HalfWidth from the middle, on a hull's gunwales or a raft's deck edges, clear of a
	// chest) a little aft of the middle, and rest slanting out and back.
	void AddPaddles(FModel& Model, float Height, float HalfWidth, FIntPoint Left, FIntPoint Right)
	{
		Model.Parts.Add(Part(ERole::PaddleLeft, FVector(HalfWidth, Height, -2), { Box(Left.X, Left.Y, -1, -1, -5, 2, 2, 18), Box(Left.X, Left.Y, -1.001f, -3, 8, 1, 6, 7) }));
		Model.Parts.Add(Part(ERole::PaddleRight, FVector(-HalfWidth, Height, -2), { Box(Right.X, Right.Y, -1, -1, -5, 2, 2, 18), Box(Right.X, Right.Y, 0.001f, -3, 8, 1, 6, 7) }));
	}

	// A chest (minecraft.wiki: "Chest"): a base 10 high with a lid 5 high over it, 14 across and a pixel in from the
	// block's sides, the lid's top at 14, and a lock on the front (-z) where they meet. Half of a double chest is 15
	// across, reaching the block's side its other half is on (Seam: +1 for +x, -1 for -x, 0 for a single chest), with
	// a lock 1 across at that side. A chest's picture is laid out the other way up from a mob's (the lid's top is where
	// a mob's net has its bottom, and the sides are upside down), so its boxes are turned half round the z axis: they
	// are given with x and y negated.
	void AddChest(FModel& Model, FVector Origin, float Scale, int32 Layer, int32 Seam)
	{
		const int32 Width = Seam == 0 ? 14 : 15;
		const float MinX = Seam > 0 ? -8.0f : -7.0f;
		const float LockX = Seam == 0 ? -1.0f : Seam > 0 ? -8.0f : 7.0f;
		const int32 LockWidth = Seam == 0 ? 2 : 1;
		FPart Base = Part(ERole::Static, Origin, { Box(0, 19, MinX, -10, -7, Width, 10, 14) }, 0, 0, PI, 0, Layer);
		Base.Scale = Scale;
		Model.Parts.Add(Base);
		// The lid and its lock swing about the lid's back edge.
		FPart Lid = Part(ERole::ChestLid, Origin + FVector(0, 9, 7) * Scale, { Box(0, 0, MinX, -5, -14, Width, 5, 14), Box(0, 0, LockX, -2, -15, LockWidth, 4, 1) }, 0, 0, PI, 0, Layer);
		Lid.Scale = Scale;
		Model.Parts.Add(Lid);
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
				// A minecart: a floor and four walls, the long way along the rail (z). The ones with a load have a block
				// standing in it at three quarters size: a chest (the entity's own picture), or a furnace, hopper or TNT from
				// the block's own model and pictures.
				const struct { const TCHAR* Kind; const TCHAR* Block; } Carts[] = {
					{ TEXT("minecraft:minecart"), nullptr }, { TEXT("minecraft:chest_minecart"), nullptr },
					{ TEXT("minecraft:furnace_minecart"), TEXT("minecraft:furnace[facing=south,lit=false]") },
					{ TEXT("minecraft:hopper_minecart"), TEXT("minecraft:hopper[enabled=true,facing=down]") },
					{ TEXT("minecraft:tnt_minecart"), TEXT("minecraft:tnt") } };
				for (const auto& Variant : Carts)
				{
					FModel Cart;
					Cart.Kind = Variant.Kind;
					Cart.TextureSize = FIntPoint(64, 32);
					Cart.Textures = { TEXT("minecart/minecart.png") };
					Cart.Parts.Add(Part(ERole::Static, FVector(0, 3, 0), { Box(0, 10, -10, -8, -1, 20, 16, 2) }, Flat, HALF_PI));
					Cart.Parts.Add(Part(ERole::Static, FVector(0, 3, 9), { Box(0, 0, -8, 0, -1, 16, 8, 2) }));
					Cart.Parts.Add(Part(ERole::Static, FVector(0, 3, -9), { Box(0, 0, -8, 0, -1, 16, 8, 2) }));
					Cart.Parts.Add(Part(ERole::Static, FVector(7, 3, 0), { Box(0, 0, -8, 0, -1, 16, 8, 2) }, 0, HALF_PI));
					Cart.Parts.Add(Part(ERole::Static, FVector(-7, 3, 0), { Box(0, 0, -8, 0, -1, 16, 8, 2) }, 0, HALF_PI));
					if (Variant.Block)
					{
						Cart.Block = Variant.Block;
						Cart.BlockOffset = FVector(0, 9, 0);
					}
					else if (Cart.Kind == TEXT("minecraft:chest_minecart"))
					{
						// A chest has no block model (it is drawn from its own picture), so it is boxes on a second layer.
						Cart.Textures.Add(TEXT("chest/normal.png"));
						Cart.LayerTextureSizes = { FIntPoint(0, 0), FIntPoint(64, 64) };
						AddChest(Cart, FVector(0, 3, 0), 0.75f, 1, 0);
					}
					Models.Add(Cart.Kind, Cart);
				}
			}
			{
				// A boat: a hull floor and four sides and two paddles. The wood is chosen by the texture.
				// A chest boat has the same hull with a chest on its floor, cut from a taller texture.
				for (const bool bChest : { false, true })
				{
					FModel Boat;
					Boat.Kind = bChest ? TEXT("minecraft:chest_boat") : TEXT("minecraft:boat");
					Boat.TextureSize = FIntPoint(128, bChest ? 128 : 64);
					Boat.Textures = { bChest ? TEXT("chest_boat/oak.png") : TEXT("boat/oak.png") };
					Boat.Parts.Add(Part(ERole::Static, FVector(0, 2, 0), { Box(0, 0, -14, -8, -3, 28, 16, 3) }, Flat, HALF_PI));
					Boat.Parts.Add(Part(ERole::Static, FVector(0, 3, 15), { Box(0, 27, -8, 0, -1, 16, 6, 2) }));
					Boat.Parts.Add(Part(ERole::Static, FVector(0, 3, -15), { Box(0, 19, -9, 0, -1, 18, 6, 2) }));
					Boat.Parts.Add(Part(ERole::Static, FVector(9, 3, 0), { Box(0, 35, -14, 0, -1, 28, 6, 2) }, 0, HALF_PI));
					Boat.Parts.Add(Part(ERole::Static, FVector(-9, 3, 0), { Box(0, 43, -14, 0, -1, 28, 6, 2) }, 0, HALF_PI));
					AddPaddles(Boat, 9.0f, 8.0f, FIntPoint(62, 0), FIntPoint(62, 20));
					// The space inside the sides, up to their tops: water is not drawn there.
					Boat.MaskLayer = 1;
					Boat.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 0, -8, 3, -14, 16, 6, 28) }, 0, 0, 0, 0, 1));
					if (bChest)
					{
						// The chest sits towards the stern, its lock facing the bow.
						Boat.Parts.Add(Part(ERole::Static, FVector(0, 5, -4), { Box(0, 76, -6, 0, -6, 12, 8, 12), Box(0, 59, -6, 8, -6, 12, 4, 12), Box(0, 59, -1, 6, 6, 2, 4, 1) }));
					}
					Models.Add(Boat.Kind, Boat);
				}
				// The bamboo raft is one flat deck (28 by 20, 4 thick), with the chest on it for the chest raft.
				for (const bool bChest : { false, true })
				{
					FModel Raft;
					Raft.Kind = bChest ? TEXT("minecraft:bamboo_chest_raft") : TEXT("minecraft:bamboo_raft");
					Raft.TextureSize = FIntPoint(128, bChest ? 128 : 64);
					Raft.Textures = { bChest ? TEXT("chest_boat/bamboo.png") : TEXT("boat/bamboo.png") };
					// It floats higher than a hull, its deck clear of the water.
					Raft.Parts.Add(Part(ERole::Static, FVector(0, 8, 0), { Box(0, 0, -14, -10, -4, 28, 20, 4) }, Flat, HALF_PI));
					AddPaddles(Raft, 9.0f, 10.0f, FIntPoint(0, 24), FIntPoint(40, 24));
					if (bChest)
					{
						Raft.Parts.Add(Part(ERole::Static, FVector(0, 11, -4), { Box(0, 76, -6, 0, -6, 12, 8, 12), Box(0, 59, -6, 8, -6, 12, 4, 12), Box(0, 59, -1, 6, 6, 2, 4, 1) }));
					}
					Models.Add(Raft.Kind, Raft);
				}
			}
			{
				// Placed chests (McChestComponent), one model for each picture: "mc:chest/normal", "mc:chest/normal_left"...
				// A "_left" picture is the half whose other half is on its +x side, a "_right" one the half on the -x side.
				for (const TCHAR* Picture : { TEXT("normal"), TEXT("trapped"), TEXT("christmas"), TEXT("ender"), TEXT("copper"), TEXT("copper_exposed"),
					TEXT("copper_weathered"), TEXT("copper_oxidized") })
				{
					for (const int32 Seam : { 0, 1, -1 })
					{
						// An ender chest doesn't pair.
						if (Seam != 0 && FCString::Strcmp(Picture, TEXT("ender")) == 0)
						{
							continue;
						}
						const FString Name = FString(Picture) + (Seam > 0 ? TEXT("_left") : Seam < 0 ? TEXT("_right") : TEXT(""));
						FModel Chest;
						Chest.Kind = FName(TEXT("mc:chest/") + Name);
						Chest.TextureSize = FIntPoint(64, 64);
						Chest.Textures = { TEXT("chest/") + Name + TEXT(".png") };
						AddChest(Chest, FVector::ZeroVector, 1.0f, 0, Seam);
						Models.Add(Chest.Kind, Chest);
					}
				}
			}
			{
				// Placed shulker boxes (McChestComponent), one model for each colour: "mc:shulker_box/shulker",
				// "mc:shulker_box/shulker_red"... (minecraft.wiki, "Shulker Box"). A base 8 high and a lid 12 high over it,
				// whole blocks across, the lid's rim over the base's top; the origin is the block's middle, so the box can be
				// turned to any face. The lid rises and turns as the box opens.
				const TCHAR* const Colours[] = { TEXT(""), TEXT("_white"), TEXT("_orange"), TEXT("_magenta"), TEXT("_light_blue"), TEXT("_yellow"),
					TEXT("_lime"), TEXT("_pink"), TEXT("_gray"), TEXT("_light_gray"), TEXT("_cyan"), TEXT("_purple"), TEXT("_blue"), TEXT("_brown"),
					TEXT("_green"), TEXT("_red"), TEXT("_black") };
				for (const TCHAR* Colour : Colours)
				{
					const FString Picture = FString(TEXT("shulker")) + Colour;
					FModel Shulker;
					Shulker.Kind = FName(TEXT("mc:shulker_box/") + Picture);
					Shulker.TextureSize = FIntPoint(64, 64);
					Shulker.Textures = { TEXT("shulker/") + Picture + TEXT(".png") };
					Shulker.Parts.Add(Part(ERole::Static, FVector::ZeroVector, { Box(0, 28, -8, -8, -8, 16, 8, 16) }));
					Shulker.Parts.Add(Part(ERole::ShulkerLid, FVector::ZeroVector, { Box(0, 0, -8, -4, -8, 16, 12, 16, 0.01f) }));
					Models.Add(Shulker.Kind, Shulker);
				}
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
			const FVector Min = Box.Min - FVector(Box.Inflate);
			const FVector Max = Box.Min + FVector(Box.Size.X, Box.Size.Y, Box.Size.Z) + FVector(Box.Inflate);
			const FQuat ModelRotation(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw));
			const auto Place = [&](const FVector& Local)
			{
				return ModelRotation.RotateVector(Pivot + PartRotation.RotateVector(Local * Part.Scale));
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
			FVector Pivot = Part.Pivot;
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
			case ERole::PaddleLeft:
			case ERole::PaddleRight:
			{
				// At rest the blade trails out over the side; rowing swings it fore and aft, dipping it on the pull.
				const bool bLeft = Part.Role == ERole::PaddleLeft;
				const float Side = bLeft ? 1.0f : -1.0f;
				const float Stroke = bLeft ? Anim.PaddleLeft : Anim.PaddleRight;
				const bool bRowing = bLeft ? Anim.bPaddleLeft : Anim.bPaddleRight;
				float Swing = 0.0f;
				float Dip = 0.0f;
				if (bRowing)
				{
					Swing = FMath::Sin(Stroke) * 0.55f;
					Dip = (FMath::Cos(Stroke) * 0.5f + 0.5f) * 0.45f;
				}
				Yaw += Side * (2.0f + Swing);
				Pitch += 0.35f + Dip;
				break;
			}
			case ERole::ShulkerLid:
			{
				// Up half a block and three quarters of a turn, easing in and out.
				const float Open = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(Anim.Lid, 0.0f, 1.0f));
				Pivot.Y += Open * 8.0f;
				Yaw += Open * PI * 1.5f;
				break;
			}
			case ERole::ChestLid:
			{
				// It eases open fast and slows at the top.
				const float Open = 1.0f - FMath::Cube(1.0f - FMath::Clamp(Anim.Lid, 0.0f, 1.0f));
				Pitch -= Open * HALF_PI;
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
			const FIntPoint Size = Model.LayerTextureSizes.IsValidIndex(Layer) && Model.LayerTextureSizes[Layer].X > 0 ? Model.LayerTextureSizes[Layer] : Model.TextureSize;
			for (const FBox& Box : Part.Boxes)
			{
				AddBox(Box, Part, Rotation, Pivot, Anim.BodyYaw, Size, Anim.Hurt, bGlow, Out);
			}
		}
	}

	void BuildBlockMesh(const FModel& Model, FMcStateId State, float BodyYaw, bool bFlash, McPlayerModel::FMeshData& Out, TArray<FVector2D>& UV1)
	{
		Out.Positions.Reset();
		Out.Normals.Reset();
		Out.UV0.Reset();
		Out.Colors.Reset();
		Out.Triangles.Reset();
		UV1.Reset();
		const FMcBlockModels& Models = FMcBlockModels::Get();
		if (!Models.IsBaked() || State >= Models.NumStates())
		{
			return;
		}
		const FQuat ModelRotation(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw));
		for (const FMcBakedQuad& Quad : Models.GetModel(State).Quads)
		{
			FVector Normal = FVector::ZeroVector;
			switch (Quad.Face)
			{
			case EMcDirection::Down: Normal = FVector(0, -1, 0); break;
			case EMcDirection::Up: Normal = FVector(0, 1, 0); break;
			case EMcDirection::North: Normal = FVector(0, 0, -1); break;
			case EMcDirection::South: Normal = FVector(0, 0, 1); break;
			case EMcDirection::West: Normal = FVector(-1, 0, 0); break;
			case EMcDirection::East: Normal = FVector(1, 0, 0); break;
			default: break;
			}
			Normal = ModelRotation.RotateVector(Normal);
			const FColor Color = bFlash ? FColor::White : Shade(Normal, 0.0f);
			const float Layer = static_cast<float>(FMcBlockTextures::Get().GetLayer(Quad.Texture));
			const int32 First = Out.Positions.Num();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const FVector Local = (FVector(Quad.Positions[Corner]) - FVector(0.5)) * 16.0 * Model.BlockScale + Model.BlockOffset;
				const FVector Placed = ModelRotation.RotateVector(Local);
				Out.Positions.Add(FVector(Placed.X, Placed.Z, Placed.Y) * (McCoords::UnitsPerBlock / 16.0));
				Out.Normals.Add(FVector(Normal.X, Normal.Z, Normal.Y));
				Out.UV0.Add(bFlash ? FVector2D(0.2, 0.6) : FVector2D(Quad.UVs[Corner]));
				Out.Colors.Add(Color);
				UV1.Add(FVector2D(bFlash ? 0.0f : Layer, 0.0f));
			}
			Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
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
	bIsItem = Kind == TEXT("minecraft:item");
	bIsArrow = Kind == TEXT("minecraft:arrow");
	bIsBanner = Kind == TEXT("mc:banner");
	if (bIsBanner)
	{
		McBanner::Decode(Item.ToString(), BannerBase, bBannerWall, BannerLayers);
	}
	Model = bIsItem || bIsArrow || bIsBanner ? nullptr : McEntityModel::Find(Kind);
	Materials.Reset();
	BlockMaterial = nullptr;
	FlashMaterial = nullptr;
	BlockText.Reset();
	BlockVertices = 0;
	ClearAllMeshSections();
	NumSections = 0;
	bItemFailed = false;
	bFlashing = false;
	bDirty = true;
}

bool UMcEntityModelComponent::HasModel() const
{
	return bIsItem || bIsArrow || bIsBanner || Model != nullptr;
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
	if (bIsBanner)
	{
		// The pole and bar have a picture of their own; the flag has the base and then one for each pattern.
		if (Layer == 0)
		{
			return McCache::Asset(TEXT("textures/entity/banner/banner_base.png"));
		}
		return McCache::Asset(TEXT("textures/entity/banner/") + (Layer < 2 ? FString(TEXT("base")) : BannerLayers.IsValidIndex(Layer - 2) ? BannerLayers[Layer - 2].Pattern : FString(TEXT("base"))) + TEXT(".png"));
	}
	if (Kind == TEXT("minecraft:boat") || Kind == TEXT("minecraft:chest_boat") || Kind == TEXT("minecraft:bamboo_raft") || Kind == TEXT("minecraft:bamboo_chest_raft"))
	{
		FString Wood = Item.IsNone() ? FString(TEXT("oak")) : Item.ToString();
		Wood.RemoveFromStart(TEXT("minecraft:"));
		const bool bChest = Kind == TEXT("minecraft:chest_boat") || Kind == TEXT("minecraft:bamboo_chest_raft");
		return McCache::Asset((bChest ? TEXT("textures/entity/chest_boat/") : TEXT("textures/entity/boat/")) + Wood + TEXT(".png"));
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
	if (bIsItem)
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
	if (!Owner || Kind.IsNone() || ((bIsItem || bIsArrow) && bItemFailed))
	{
		return;
	}
	if (!HasModel())
	{
		TickStandIn();
		return;
	}
	Anim.Age += DeltaTime;
	if (Materials.Num() > 0 && MaterialGeneration != McRenderMode::GetGeneration())
	{
		// The look changed: the materials are made again, and the mesh with them.
		Materials.Reset();
		BlockMaterial = nullptr;
		FlashMaterial = nullptr;
		BlockVertices = 0;
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
		// A boat's paddles go round the stroke while they row (Count's bits), and start again from the top.
		// Development: -McPaddling rows every boat's paddles (for a -McShot of them).
		static const bool bAlwaysPaddling = FParse::Param(FCommandLine::Get(), TEXT("McPaddling"));
		Anim.bPaddleLeft = bAlwaysPaddling || (Count & 1) != 0;
		Anim.bPaddleRight = bAlwaysPaddling || (Count & 2) != 0;
		PaddlePhase[0] = Anim.bPaddleLeft ? PaddlePhase[0] + PI / 8.0f : 0.0f;
		PaddlePhase[1] = Anim.bPaddleRight ? PaddlePhase[1] + PI / 8.0f : 0.0f;
		Anim.PaddleLeft = PaddlePhase[0];
		Anim.PaddleRight = PaddlePhase[1];
		// A chest's lid moves a tenth of the way each tick.
		PreviousLid = Lid;
		Lid = FMath::Clamp(Lid + (bOpen ? 0.1f : -0.1f), 0.0f, 1.0f);
		Walking +=(FMath::Min(static_cast<float>(Moved) * 4.0f, 1.0f) - Walking) * 0.4f;
		Anim.WalkAmount = Walking;
		Anim.WalkPos += Walking;
	}
	Anim.Hurt = FMath::Max(0.0f, Anim.Hurt - DeltaTime * 2.5f);
	Anim.Lid = FMath::Lerp(PreviousLid, Lid, TickAccumulator / 0.05f);

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
		FVector2D Swell = McEntityModel::GetSwellScale(Swelling);
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
	if (Model->MaskLayer != INDEX_NONE)
	{
		if (!Mask)
		{
			Mask = NewObject<UProceduralMeshComponent>(GetOwner());
			Mask->SetupAttachment(this);
			Mask->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mask->SetCastShadow(false);
			Mask->SetRenderInMainPass(false);
			Mask->SetRenderInDepthPass(false);
			Mask->SetRenderCustomDepth(true);
			Mask->SetCustomDepthStencilValue(McEntityModel::HullStencil);
			Mask->RegisterComponent();
		}
		McPlayerModel::FMeshData Data;
		McEntityModel::BuildMesh(*Model, Anim, Model->MaskLayer, Data);
		if (Mask->GetNumSections() == 0)
		{
			Mask->CreateMeshSection(0, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, Data.Colors, TArray<FProcMeshTangent>(), false);
			Mask->SetMaterial(0, BaseMaterial);
		}
		else
		{
			Mask->UpdateMeshSection(0, Data.Positions, Data.Normals, Data.UV0, Data.Colors, TArray<FProcMeshTangent>());
		}
	}
	else if (Mask)
	{
		Mask->DestroyComponent();
		Mask = nullptr;
	}
	if (!Model->Block.IsEmpty())
	{
		TickBlock(NumLayers);
	}
}

void UMcEntityModelComponent::TickStandIn()
{
	if (Materials.Num() > 0 && MaterialGeneration != McRenderMode::GetGeneration())
	{
		Materials.Reset();
		ClearAllMeshSections();
		NumSections = 0;
	}
	if (Materials.Num() == 0)
	{
		// Drawn with the white silhouette of the minecart's picture, from a corner that is opaque, and tinted magenta: a
		// lit engine material would be black here, where the light is in the pictures' vertex colours.
		FIntPoint Size;
		UTexture2DArray* White = LoadCached(McCache::Asset(TEXT("textures/entity/minecart/minecart.png")), Size, true);
		if (!White || !BaseMaterial)
		{
			return;
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial), this);
		MaterialGeneration = McRenderMode::GetGeneration();
		Material->SetTextureParameterValue(TEXT("Blocks"), White);
		Materials = { Material };
	}
	Materials[0]->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
	Materials[0]->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	const FVector2D Hitbox(FMath::Max(HitboxWidth, 0.25f), FMath::Max(HitboxHeight, 0.25f));
	if (NumSections > 0 && StandInSize == Hitbox)
	{
		return;
	}
	StandInSize = Hitbox;
	McEntityModel::FModel Box;
	const int32 Width = FMath::Max(1, FMath::RoundToInt32(Hitbox.X * 16.0f));
	const int32 Height = FMath::Max(1, FMath::RoundToInt32(Hitbox.Y * 16.0f));
	McEntityModel::FPart Part;
	Part.Boxes.Add({ FIntPoint::ZeroValue, FVector(-Width * 0.5f, 0.0f, -Width * 0.5f), FIntVector(Width, Height, Width) });
	Box.Parts.Add(Part);
	McPlayerModel::FMeshData Data;
	McEntityModel::BuildMesh(Box, McEntityModel::FAnim(), 0, Data);
	for (int32 Index = 0; Index < Data.Positions.Num(); ++Index)
	{
		Data.UV0[Index] = FVector2D(0.2, 0.6);
		Data.Colors[Index] = FColor(Data.Colors[Index].R, 0, Data.Colors[Index].R, 255);
	}
	TArray<FVector2D> UV1, UV2;
	UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
	UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
	ClearAllMeshSections();
	CreateMeshSection(0, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
	SetMaterial(0, Materials[0]);
	NumSections = 1;
}

void UMcEntityModelComponent::TickBlock(int32 Section)
{
	// A lit furnace burns; a TNT cart (its count is the fuse left) flashes white every five ticks.
	FString Text = Model->Block;
	if (Kind == TEXT("minecraft:furnace_minecart") && Count > 0)
	{
		Text.ReplaceInline(TEXT("lit=false"), TEXT("lit=true"));
	}
	const bool bBlockFlash = Kind == TEXT("minecraft:tnt_minecart") && Count > 0 && (Count / 5) % 2 == 0;
	FMcBlockTextures& Textures = FMcBlockTextures::Get();
	if (!FMcBlockModels::Get().EnsureBaked() || !Textures.EnsureLoaded())
	{
		return;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	TOptional<FMcStateId> State = Registry.ParseState(Text);
	if (!State.IsSet())
	{
		FString Name = Text;
		Name.Split(TEXT("["), &Name, nullptr);
		State = Registry.FindDefaultState(FName(*Name));
	}
	if (!State.IsSet())
	{
		return;
	}
	if (!BlockMaterial)
	{
		BlockMaterial = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial), this);
		BlockMaterial->SetTextureParameterValue(TEXT("Blocks"), Textures.GetTextureArray());
	}
	if (bBlockFlash && !FlashMaterial)
	{
		FIntPoint Size;
		if (UTexture2DArray* White = LoadCached(GetTexturePath(0), Size, true))
		{
			FlashMaterial = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial), this);
			FlashMaterial->SetTextureParameterValue(TEXT("Blocks"), White);
		}
	}
	UMaterialInstanceDynamic* Material = bBlockFlash && FlashMaterial ? FlashMaterial.Get() : BlockMaterial.Get();
	Material->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
	Material->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	McPlayerModel::FMeshData Data;
	TArray<FVector2D> UV1;
	McEntityModel::BuildBlockMesh(*Model, State.GetValue(), Anim.BodyYaw, bBlockFlash && FlashMaterial, Data, UV1);
	TArray<FVector2D> UV2;
	UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
	if (Data.Positions.Num() == BlockVertices && BlockText == Text)
	{
		UpdateMeshSection(Section, Data.Positions, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>());
	}
	else
	{
		CreateMeshSection(Section, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
		BlockVertices = Data.Positions.Num();
		BlockText = Text;
	}
	SetMaterial(Section, Material);
}
