#include "McEntityModel.h"
#include "McRenderMode.h"
#include "McCache.h"
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
		// LeftU, if it is not negative, is where the left legs' own picture starts (the ravager has one for each side).
		void AddLegs(FModel& Model, int32 U, int32 V, float Width, float Height, float X, float Front, float Back, float Top, int32 LeftU = -1)
		{
			const FBox Leg = Box(U, V, -Width * 0.5f, -Height, -Width * 0.5f, static_cast<int32>(Width), static_cast<int32>(Height), static_cast<int32>(Width));
			const FBox LeftLeg = LeftU < 0 ? Leg : Box(LeftU, V, -Width * 0.5f, -Height, -Width * 0.5f, static_cast<int32>(Width), static_cast<int32>(Height), static_cast<int32>(Width));
			Model.Parts.Add(Part(ERole::LegFrontRight, FVector(-X, Top, Front), { Leg }));
			Model.Parts.Add(Part(ERole::LegFrontLeft, FVector(X, Top, Front), { LeftLeg }));
			Model.Parts.Add(Part(ERole::LegBackRight, FVector(-X, Top, Back), { Leg }));
			Model.Parts.Add(Part(ERole::LegBackLeft, FVector(X, Top, Back), { LeftLeg }));
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

		// A villager's body (minecraft.wiki, "Villager"): a head with a long nose, a hat, a robe, arms folded across the chest
		// and two legs. The biome type, profession and level pictures are laid over the base one into a single picture (the
		// game draws each over the whole body in turn, which comes to the same), so the hat and robe are cut from it as well.
		// A zombie villager holds its arms out in front like the zombie: two arms of its own, cut from the same place of the
		// picture the crossed ones are, instead of the folded pair.
		void AddVillager(FModel& Model, bool bArmsOut = false)
		{
			Model.TextureSize = FIntPoint(64, 64);
			Model.BaseWidth = 0.6f;
			Model.bComposite = true;
			Model.Parts.Add(Part(ERole::Head, FVector(0, 24, 0), { Box(0, 0, -4, 0, -4, 8, 10, 8), Box(24, 0, -1, -1, 4, 2, 4, 2), Box(32, 0, -4, 0, -4, 8, 10, 8, 0.51f) }));
			Model.Parts.Add(Part(ERole::Static, FVector(0, 24, 0), { Box(16, 20, -4, -12, -3, 8, 12, 6), Box(0, 38, -4, -18, -3, 8, 18, 6, 0.5f) }));
			if (bArmsOut)
			{
				Model.bArmsForward = true;
				Model.Parts.Add(Part(ERole::ArmRight, FVector(-5, 22, 0), { Box(44, 22, -3, -10, -2, 4, 12, 4) }));
				Model.Parts.Add(Part(ERole::ArmLeft, FVector(5, 22, 0), { Box(44, 22, -1, -10, -2, 4, 12, 4, 0.0f, true) }));
			}
			else
			{
				Model.Parts.Add(Part(ERole::Static, FVector(0, 21, 1), { Box(44, 22, -8, -6, -2, 4, 8, 4), Box(44, 22, 4, -6, -2, 4, 8, 4, 0.0f, true), Box(40, 38, -4, -6, -2, 8, 4, 4) }, -0.75f));
			}
			Model.Parts.Add(Part(ERole::LegRight, FVector(-2, 12, 0), { Box(0, 22, -2, -12, -2, 4, 12, 4) }));
			Model.Parts.Add(Part(ERole::LegLeft, FVector(2, 12, 0), { Box(0, 22, -2, -12, -2, 4, 12, 4, 0.0f, true) }));
		}

		// An illager (minecraft.wiki, "Illager"): the villager's shape without its robe's colour, on its own picture. Idle it
		// folds its arms like the villager; armed it has two arms of its own, held out with the weapon of its Style (1 a crossbow,
		// 2 an axe, 3 none: it casts), and Held is the item picture drawn in the right hand.
		void AddIllager(FModel& Model, int32 Style, const TCHAR* Held)
		{
			AddVillager(Model, false);
			Model.bComposite = false;
			Model.IllagerStyle = Style;
			for (FPart& Each : Model.Parts)
			{
				if (Each.Role == ERole::Static && Each.Boxes.Num() == 3)
				{
					// The pillager never folds its arms: its own two hang at its sides.
					Each.ShowPoses = Style == 1 ? 0u : 1u;
				}
			}
			const uint32 Armed = Style == 1 ? ~0u : ~1u;
			Model.Parts.Add(Part(ERole::ArmRight, FVector(-5, 22, 0), { Box(40, 46, -3, -10, -2, 4, 12, 4) }));
			Model.Parts.Add(Part(ERole::ArmLeft, FVector(5, 22, 0), { Box(40, 46, -1, -10, -2, 4, 12, 4, 0.0f, true) }));
			Model.Parts.Last(1).ShowPoses = Armed;
			Model.Parts.Last(0).ShowPoses = Armed;
			if (Held)
			{
				Model.Textures.Add(FString(TEXT("../item/")) + Held + TEXT(".png"));
				FBox Picture = Style == 1 ? Box(0, 0, -1, -16, 0, 12, 12, 0) : Box(0, 0, -1, -22, -1, 0, 12, 12);
				Picture.bFlat = true;
				// The crossbow lies flat along the arms, its barrel forward (the item model's display turns it down by 90 degrees).
				Picture.bFlatTop = Style == 1;
				FPart Hand = Part(ERole::ArmRight, FVector(-5, 22, 0), { Picture }, 0.0f, 0, 0, 0, 1);
				Hand.ShowPoses = Armed;
				Model.Parts.Add(Hand);
			}
		}

		TMap<FName, FModel> BuildModels()
		{
			TMap<FName, FModel> Models;
			constexpr float Flat = HALF_PI;

			{
				FModel Pig;
				Pig.Kind = TEXT("minecraft:pig");
				Pig.TextureSize = FIntPoint(64, 64);
				Pig.Textures = { TEXT("pig/pig_temperate.png") };
				Pig.Variants = { { TEXT("pig/pig_cold.png") }, { TEXT("pig/pig_temperate.png") }, { TEXT("pig/pig_warm.png") } };
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
				Cow.Variants = { { TEXT("cow/cow_cold.png") }, { TEXT("cow/cow_temperate.png") }, { TEXT("cow/cow_warm.png") } };
				Cow.Parts.Add(Part(ERole::Static, FVector(0, 17, 0), { Box(18, 4, -6, -9, -5, 12, 18, 10), Box(52, 0, -2, -9, 5, 4, 6, 1) }, Flat));
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
				Chicken.Variants = { { TEXT("chicken/chicken_cold.png") }, { TEXT("chicken/chicken_temperate.png") }, { TEXT("chicken/chicken_warm.png") } };
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
				struct FIllager { const TCHAR* Name; int32 Style; const TCHAR* Held; };
				for (const FIllager& Each : { FIllager{ TEXT("pillager"), 1, TEXT("crossbow_standby") }, FIllager{ TEXT("vindicator"), 2, TEXT("iron_axe") }, FIllager{ TEXT("evoker"), 3, nullptr } })
				{
					FModel Illager;
					Illager.Kind = FName(*(FString(TEXT("minecraft:")) + Each.Name));
					Illager.Textures = { FString(TEXT("illager/")) + Each.Name + TEXT(".png") };
					AddIllager(Illager, Each.Style, Each.Held);
					Models.Add(Illager.Kind, Illager);
				}
			}
			{
				// The witch: a villager under a tall pointed hat of four stacked pieces.
				FModel Witch;
				Witch.Kind = TEXT("minecraft:witch");
				Witch.TextureSize = FIntPoint(64, 128);
				Witch.Textures = { TEXT("witch/witch.png") };
				AddVillager(Witch);
				Witch.TextureSize = FIntPoint(64, 128);
				Witch.bComposite = false;
				for (FPart& Part : Witch.Parts)
				{
					if (Part.Role == ERole::Head)
					{
						Part.Boxes.Reset();
						Part.Boxes.Add(Box(0, 0, -4, 0, -4, 8, 10, 8));
						Part.Boxes.Add(Box(24, 0, -1, -1, 4, 2, 4, 2));
						Part.Boxes.Add(Box(0, 64, -5, 10, -5, 10, 2, 10));
						Part.Boxes.Add(Box(0, 76, -3.5f, 12, -3.5f, 7, 4, 7));
						Part.Boxes.Add(Box(0, 87, -2, 16, -2, 4, 4, 4));
						Part.Boxes.Add(Box(0, 95, -0.5f, 20, -0.5f, 1, 2, 1));
					}
					else if (Part.Role == ERole::Static && Part.Boxes.Num() == 2)
					{
						// Its robe is part of the body picture.
						Part.Boxes.RemoveAt(1);
					}
				}
				// The potion it holds up to drink, in front of its face.
				Witch.Textures.Add(TEXT("../item/potion.png"));
				FBox Bottle = Box(0, 0, -1, 0, 3, 0, 10, 10);
				Bottle.bFlat = true;
				Bottle.bMirror = true;
				FPart Drinking = Part(ERole::Static, FVector(0, 24, 0), { Bottle }, 0, 0, 0, 0, 1);
				Drinking.ShowPoses = 1u << 4;
				Witch.Parts.Add(Drinking);
				Models.Add(Witch.Kind, Witch);
			}
			{
				// The vex: a small head and body with arms, floating.
				FModel Vex;
				Vex.Kind = TEXT("minecraft:vex");
				Vex.TextureSize = FIntPoint(32, 32);
				Vex.Textures = { TEXT("illager/vex.png") };
				Vex.Parts.Add(Part(ERole::Head, FVector(0, 7, 0), { Box(0, 0, -2.5f, 0, -2.5f, 5, 5, 5) }));
				Vex.Parts.Add(Part(ERole::Static, FVector(0, 7, 0), { Box(0, 10, -1.5f, -4, -1, 3, 4, 2) }));
				Vex.Parts.Add(Part(ERole::ArmRight, FVector(-2, 6.5f, 0), { Box(23, 0, -1, -3, -1, 2, 4, 2) }));
				Vex.Parts.Add(Part(ERole::ArmLeft, FVector(2, 6.5f, 0), { Box(23, 6, -1, -3, -1, 2, 4, 2) }));
				// Two wings at its back, beating all the time.
				Vex.bFlutter = true;
				Vex.Parts.Add(Part(ERole::WingRight, FVector(-1.5f, 6.5f, -1), { Box(16, 22, 0, 0, -8, 0, 5, 8) }));
				Vex.Parts.Add(Part(ERole::WingLeft, FVector(1.5f, 6.5f, -1), { Box(16, 22, 0, 0, -8, 0, 5, 8, 0.0f, true) }));
				Models.Add(Vex.Kind, Vex);
			}
			{
				// The ravager (minecraft.wiki, "Ravager"): a long torso between four tall legs and a big head
				// carried low in front with a jaw under it and two horns at its sides.
				FModel Ravager;
				Ravager.Kind = TEXT("minecraft:ravager");
				Ravager.TextureSize = FIntPoint(128, 128);
				Ravager.Textures = { TEXT("illager/ravager.png") };
				// The torso is two blocks end to end: the front 14x16x20 (its saddle patch on top) and behind it the 12x13x18
				// hindquarters, so it runs about 29 long and 20 high between four whole 8x37x8 legs that stand a little above
				// its back. The head hangs low in front, its bottom near the knees, the neck hidden inside it.
				Ravager.Parts.Add(Part(ERole::Static, FVector(0, 26, 0), { Box(0, 55, -7, -8, -10, 14, 16, 20), Box(0, 91, -6, -21, -8, 12, 13, 18) }, Flat));
				Ravager.Parts.Add(Part(ERole::Static, FVector(0, 27, 0), { Box(68, 73, -5, -5, -3, 10, 10, 15) }));
				// The head stands level with the top of the legs, its back against the front legs' fronts, so no neck shows; the nose hangs from the face.
				Ravager.Parts.Add(Part(ERole::Head, FVector(0, 25, 9), { Box(0, 0, -8, -10, 0, 16, 20, 16), Box(0, 36, -8, -13, 0, 16, 3, 16), Box(0, 0, -2, -12, 16, 4, 8, 4) }));
				// The horns stand on the head's top corners and lean well forward (vanilla's 63 degrees).
				Ravager.Parts.Add(Part(ERole::Head, FVector(-8.5f, 30, 19), { Box(74, 55, -1, 0, -2, 2, 14, 4) }, 1.1f, 0, 0));
				Ravager.Parts.Add(Part(ERole::Head, FVector(8.5f, 30, 19), { Box(74, 55, -1, 0, -2, 2, 14, 4, 0.0f, true) }, 1.1f, 0, 0));
				AddLegs(Ravager, 96, 0, 8, 37, 8, 5, -18, 37, 64);
				Models.Add(Ravager.Kind, Ravager);
			}
			{
				// The evoker fangs: a base with a pair of jaws.
				FModel Fangs;
				Fangs.Kind = TEXT("minecraft:evoker_fangs");
				Fangs.TextureSize = FIntPoint(64, 32);
				Fangs.Textures = { TEXT("illager/evoker_fangs.png") };
				Fangs.Parts.Add(Part(ERole::Static, FVector(0, 0, 0), { Box(0, 0, -5, 0, -5, 10, 12, 10) }));
				Fangs.Parts.Add(Part(ERole::Static, FVector(0, 12, 0), { Box(40, 0, -3, 0, -2, 4, 14, 8), Box(40, 0, -1, 0, -2, 4, 14, 8, 0.0f, true) }));
				Models.Add(Fangs.Kind, Fangs);
			}
			{
				// The iron golem (minecraft.wiki, "Iron Golem"): a 2.7 block tall slab of a body on short legs with arms to its knees.
				FModel Golem;
				Golem.Kind = TEXT("minecraft:iron_golem");
				Golem.TextureSize = FIntPoint(128, 128);
				Golem.Textures = { TEXT("iron_golem/iron_golem.png") };
				Golem.Parts.Add(Part(ERole::Head, FVector(0, 33, 0), { Box(0, 0, -4, 0, -2.5f, 8, 10, 8), Box(24, 0, -1, -1, 5.5f, 2, 4, 2) }));
				Golem.Parts.Add(Part(ERole::Static, FVector(0, 21, 0), { Box(0, 40, -9, 0, -5, 18, 12, 11), Box(0, 70, -4.5f, -5, -3, 9, 5, 6, 0.5f) }));
				Golem.Parts.Add(Part(ERole::ArmRight, FVector(0, 33.5f, 0), { Box(60, 21, -13, -30, -3, 4, 30, 6) }));
				Golem.Parts.Add(Part(ERole::ArmLeft, FVector(0, 33.5f, 0), { Box(60, 58, 9, -30, -3, 4, 30, 6) }));
				Golem.Parts.Add(Part(ERole::LegRight, FVector(-4, 13, 0), { Box(37, 0, -3.5f, -13, -2, 6, 16, 5) }));
				Golem.Parts.Add(Part(ERole::LegLeft, FVector(5, 13, 0), { Box(60, 0, -3.5f, -13, -2, 6, 16, 5) }));
				Golem.MaxLimbSwing = 0.5f;
				Models.Add(Golem.Kind, Golem);
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
				// The pictures are chosen per villager (GetVillagerTextures); these are the plains novice farmer's.
				FModel Villager;
				Villager.Kind = TEXT("minecraft:villager");
				Villager.Textures = { TEXT("villager/villager.png"), TEXT("villager/type/plains.png"), TEXT("villager/profession/farmer.png"), TEXT("villager/profession_level/stone.png") };
				AddVillager(Villager);
				Models.Add(Villager.Kind, Villager);
			}
			{
				// A zombie villager is the villager's shape in the zombie's greens; the pictures are chosen per one.
				FModel Zombie;
				Zombie.Kind = TEXT("minecraft:zombie_villager");
				Zombie.Textures = { TEXT("zombie_villager/zombie_villager.png"), TEXT("zombie_villager/type/plains.png") };
				AddVillager(Zombie, true);
				Models.Add(Zombie.Kind, Zombie);
			}
			{
				// The wandering trader wears one picture, laid out as the villager's.
				FModel Trader;
				Trader.Kind = TEXT("minecraft:wandering_trader");
				Trader.Textures = { TEXT("wandering_trader/wandering_trader.png") };
				AddVillager(Trader);
				Models.Add(Trader.Kind, Trader);
			}
			{
				// Pieces are cut from the textures' own nets (box sizes read off the picture), placed from minecraft.wiki's models:
				// the body is a long box along z, the wolf's and the others' laid down as the pig's is.
				FModel Horse;
				Horse.Kind = TEXT("minecraft:horse");
				Horse.TextureSize = FIntPoint(64, 64);
				Horse.Textures = { TEXT("horse/horse_brown.png") };
				// The coat with its markings laid over it.
				Horse.bComposite = true;
				for (const TCHAR* Coat : { TEXT("white"), TEXT("creamy"), TEXT("chestnut"), TEXT("brown"), TEXT("black"), TEXT("gray"), TEXT("darkbrown") })
				{
					for (const TCHAR* Marking : { TEXT(""), TEXT("white"), TEXT("whitefield"), TEXT("whitedots"), TEXT("blackdots") })
					{
						Horse.Variants.Add({ FString::Printf(TEXT("horse/horse_%s.png"), Coat), FCString::Strlen(Marking) ? FString::Printf(TEXT("horse/horse_markings_%s.png"), Marking) : FString() });
					}
				}
				Horse.Parts.Add(Part(ERole::Static, FVector(0, 0, 0), { Box(0, 32, -5, 11, -14, 10, 10, 24) }));
				// The tail hangs from the rump, leaning back.
				Horse.Parts.Add(Part(ERole::Static, FVector(0, 21, -14), { Box(42, 36, -1.5f, -14, -4, 3, 14, 4) }, 0.52f));
				// The neck, head, mane, mouth and ears all lean forward from the withers together.
				Horse.Parts.Add(Part(ERole::Head, FVector(0, 20, 10),
					{ Box(0, 35, -2.05f, -6, -5, 4, 12, 7), Box(0, 0, -3, 6, -5, 6, 5, 7), Box(0, 25, -2, 6, 2, 4, 5, 5), Box(56, 36, -1, -5, -7, 2, 16, 2),
					  Box(19, 16, -2.55f, 10, -5, 2, 3, 1), Box(19, 16, 0.55f, 10, -5, 2, 3, 1) }, 0.52f));
				AddLegs(Horse, 48, 21, 4, 11, 3, 7, -11, 11);
				Models.Add(Horse.Kind, Horse);
			}
			{
				FModel Wolf;
				Wolf.Kind = TEXT("minecraft:wolf");
				Wolf.Textures = { TEXT("wolf/wolf.png"), TEXT("wolf/wolf_collar.png") };
				Wolf.TintedLayers = 2u;
				// In the order of data/minecraft/wolf_variant/ (McMobLook): the files by name.
				for (const TCHAR* Breed : { TEXT("wolf_ashen"), TEXT("wolf_black"), TEXT("wolf_chestnut"), TEXT("wolf"), TEXT("wolf_rusty"), TEXT("wolf_snowy"), TEXT("wolf_spotted"), TEXT("wolf_striped"), TEXT("wolf_woods") })
				{
					Wolf.Variants.Add({ FString::Printf(TEXT("wolf/%s.png"), Breed), TEXT("wolf/wolf_collar.png") });
				}
				// The poses are bits: 1 sitting, 2 angry, 4 tame. Standing is where bit 1 is clear; the collar is drawn for tame ones.
				// The boxes are cut from the picture; where they sit (pivots, tilts, the tail's angle by mood) was fitted to the
				// silhouettes of side-on shots of the real thing, at the same camera and a steady field of view.
				constexpr uint32 Standing = 0x55u;
				constexpr uint32 Sitting = 0xAAu;
				constexpr uint32 Wild = 0x03u;
				constexpr uint32 Angry = 0x0Cu;
				constexpr uint32 Tame = 0xF0u;
				const FBox Rear = Box(18, 14, -3, -4.5f, -3, 6, 9, 6);
				const FBox Chest = Box(21, 0, -4, -3, -3.5f, 8, 6, 7);
				const FBox ChestCollar = Box(21, 0, -4, -3, -3.5f, 8, 6, 7, 0.03f);
				const FBox Tail = Box(9, 18, -1, -8, -1, 2, 8, 2);
				const FBox Leg = Box(0, 18, -1, -8, -1, 2, 8, 2);
				const auto Add = [&Wolf](FPart Each, uint32 Poses)
				{
					Each.ShowPoses = Poses;
					Wolf.Parts.Add(Each);
				};
				const std::initializer_list<FBox> HeadBoxes = { Box(0, 0, -3, -3, -2, 6, 6, 4), Box(16, 14, -3, 3, -1, 2, 2, 1), Box(16, 14, 1, 3, -1, 2, 2, 1), Box(0, 10, -1.5f, -3, 1, 3, 3, 4) };
				// Standing.
				Add(Part(ERole::Head, FVector(0, 9.9f, 5.65f), HeadBoxes), Standing);
				Add(Part(ERole::Static, FVector(0, 8.34f, -4.1f), { Rear }, Flat), Standing);
				Add(Part(ERole::Static, FVector(0, 8.55f, 3.52f), { Chest }, Flat), Standing);
				Add(Part(ERole::Static, FVector(0, 10.64f, -7.38f), { Tail }, 0.56f), Standing & Wild);
				Add(Part(ERole::Static, FVector(0, 10.64f, -7.38f), { Tail }, 1.45f), Standing & Angry);
				Add(Part(ERole::Static, FVector(0, 10.64f, -7.38f), { Tail }, 1.65f), Standing & Tame);
				Add(Part(ERole::LegFrontRight, FVector(-1.25f, 8, 5.05f), { Leg }), Standing);
				Add(Part(ERole::LegFrontLeft, FVector(1.25f, 8, 5.05f), { Leg }), Standing);
				Add(Part(ERole::LegBackRight, FVector(-1.25f, 8, -5.96f), { Leg }), Standing);
				Add(Part(ERole::LegBackLeft, FVector(1.25f, 8, -5.96f), { Leg }), Standing);
				// Sitting: the rear on its haunches, the chest raised, the hind legs folded forward and the front ones straight down.
				Add(Part(ERole::Head, FVector(0, 10.12f, 6.26f), HeadBoxes), Sitting);
				Add(Part(ERole::Static, FVector(0, 2.28f, -1.18f), { Rear }, 0.83f), Sitting);
				Add(Part(ERole::Static, FVector(0, 6.77f, 3.12f), { Chest }, 1.27f), Sitting);
				Add(Part(ERole::Static, FVector(0, 1.85f, -4.95f), { Tail }, 0.82f), Sitting & Wild);
				Add(Part(ERole::Static, FVector(0, 1.85f, -4.95f), { Tail }, 1.5f), Sitting & Angry);
				Add(Part(ERole::Static, FVector(0, 1.85f, -4.95f), { Tail }, 1.64f), Sitting & Tame);
				Add(Part(ERole::Static, FVector(-1.25f, -0.95f, -2.93f), { Leg }, 4.65f), Sitting);
				Add(Part(ERole::Static, FVector(1.25f, -0.95f, -2.93f), { Leg }, 4.65f), Sitting);
				Add(Part(ERole::Static, FVector(-1.25f, 6.73f, 4.4f), { Leg }, 5.83f), Sitting);
				Add(Part(ERole::Static, FVector(1.25f, 6.73f, 4.4f), { Leg }, 5.83f), Sitting);
				// The collar: the chest drawn again, a hair larger, in the collar's own picture.
				Add(Part(ERole::Static, FVector(0, 8.55f, 3.52f), { ChestCollar }, Flat, 0.0f, 0.0f, 0, 1), Standing & Tame);
				Add(Part(ERole::Static, FVector(0, 6.77f, 3.12f), { ChestCollar }, 1.27f, 0.0f, 0.0f, 0, 1), Sitting & Tame);
				Models.Add(Wolf.Kind, Wolf);
			}
			{
				FModel Goat;
				Goat.Kind = TEXT("minecraft:goat");
				Goat.TextureSize = FIntPoint(64, 64);
				Goat.Textures = { TEXT("goat/goat.png") };
				Goat.Parts.Add(Part(ERole::Static, FVector(0, 0, 0), { Box(1, 1, -4.5f, 6, -9, 9, 11, 16), Box(0, 28, -5.5f, 4, -3, 11, 14, 11) }));
				Goat.Parts.Add(Part(ERole::Head, FVector(0, 15, 8),
					{ Box(34, 46, -2.5f, -4, 0, 5, 7, 10), Box(12, 55, -3, 3, 2, 2, 7, 2), Box(12, 55, 1, 3, 2, 2, 7, 2, 0.0f, true) }));
				const FBox FrontLeg = Box(49, 2, -1.5f, -10, -1.5f, 3, 10, 3);
				const FBox BackLeg = Box(36, 29, -1.5f, -6, -1.5f, 3, 6, 3);
				Goat.Parts.Add(Part(ERole::LegFrontRight, FVector(-3, 10, 5), { FrontLeg }));
				Goat.Parts.Add(Part(ERole::LegFrontLeft, FVector(3, 10, 5), { FrontLeg }));
				Goat.Parts.Add(Part(ERole::LegBackRight, FVector(-3, 6, -6), { BackLeg }));
				Goat.Parts.Add(Part(ERole::LegBackLeft, FVector(3, 6, -6), { BackLeg }));
				Models.Add(Goat.Kind, Goat);
			}
			{
				FModel Fox;
				Fox.Kind = TEXT("minecraft:fox");
				Fox.TextureSize = FIntPoint(48, 32);
				Fox.Textures = { TEXT("fox/fox.png") };
				Fox.Variants = { { TEXT("fox/fox.png") }, { TEXT("fox/fox_snow.png") } };
				Fox.Parts.Add(Part(ERole::Static, FVector(0, 9, 0.5f), { Box(24, 15, -3, -5.5f, -3, 6, 11, 6) }, Flat));
				Fox.Parts.Add(Part(ERole::Head, FVector(0, 10.5f, 5.5f),
					{ Box(1, 5, -4, -3, 0, 8, 6, 6), Box(8, 1, -3.5f, 3, 1, 2, 2, 1), Box(15, 1, 1.5f, 3, 1, 2, 2, 1), Box(6, 18, -2, -2.5f, 6, 4, 2, 3) }));
				// The tail trails behind, a little drooped.
				Fox.Parts.Add(Part(ERole::Static, FVector(0, 10, -5), { Box(30, 0, -2, 0, -2.5f, 4, 9, 5) }, -1.7f));
				AddLegs(Fox, 4, 24, 2, 6, 2, 4, -3.5f, 6);
				Models.Add(Fox.Kind, Fox);
			}
			for (const TCHAR* LlamaKind : { TEXT("minecraft:llama"), TEXT("minecraft:trader_llama") })
			{
				FModel Llama;
				Llama.Kind = LlamaKind;
				Llama.TextureSize = FIntPoint(128, 64);
				Llama.Textures = { TEXT("llama/llama_brown.png") };
				Llama.Variants = { { TEXT("llama/llama_creamy.png") }, { TEXT("llama/llama_white.png") }, { TEXT("llama/llama_brown.png") }, { TEXT("llama/llama_gray.png") } };
				Llama.Parts.Add(Part(ERole::Static, FVector(0, 19, 0), { Box(29, 0, -6, -9, -5, 12, 18, 10) }, Flat));
				Llama.Parts.Add(Part(ERole::Static, FVector(0, 14, 8), { Box(0, 14, -4, 0, -3, 8, 18, 6) }));
				Llama.Parts.Add(Part(ERole::Head, FVector(0, 28, 9), { Box(0, 0, -2, 0, -1, 4, 4, 9), Box(17, 0, -3, 4, 3, 3, 3, 1), Box(17, 0, 0, 4, 3, 3, 3, 1) }));
				AddLegs(Llama, 29, 29, 4, 14, 4, 6.5f, -6.5f, 14);
				Models.Add(Llama.Kind, Llama);
			}
			{
				FModel Bee;
				Bee.Kind = TEXT("minecraft:bee");
				Bee.TextureSize = FIntPoint(64, 64);
				Bee.Textures = { TEXT("bee/bee.png") };
				Bee.Parts.Add(Part(ERole::Static, FVector(0, 6, 0), { Box(0, 0, -3.5f, -3.5f, -5, 7, 7, 10), Box(26, 7, 0, -0.5f, -7, 0, 1, 2) }));
				// The antennae, and three legs that are flat planes (a box with no depth, drawn both sides).
				Bee.Parts.Add(Part(ERole::Static, FVector(0, 6, 0), { Box(2, 0, 1.5f, 2, 5, 1, 2, 3), Box(2, 3, -2.5f, 2, 5, 1, 2, 3) }));
				Bee.Parts.Add(Part(ERole::Static, FVector(0, 6, 0), { Box(26, 1, -3.5f, -5.5f, 3, 7, 2, 0), Box(26, 3, -3.5f, -5.5f, 0, 7, 2, 0), Box(26, 5, -3.5f, -5.5f, -3, 7, 2, 0) }));
				// Two flat wings that beat.
				Bee.Parts.Add(Part(ERole::WingRight, FVector(-1.5f, 10, 3), { Box(0, 18, -9, 0, -6, 9, 0, 6) }, 0.0f, 0.0f, -0.6f));
				Bee.Parts.Add(Part(ERole::WingLeft, FVector(1.5f, 10, 3), { Box(0, 18, 0, 0, -6, 9, 0, 6, 0.0f, true) }, 0.0f, 0.0f, 0.6f));
				Models.Add(Bee.Kind, Bee);
			}
			{
				// The nets of the rabbit's picture (read off its pixels): body 8x6x10, head 5x5x5, ears 2x5x1, haunches 4x4x4, feet 2x1x6.
				FModel Rabbit;
				Rabbit.Kind = TEXT("minecraft:rabbit");
				Rabbit.TextureSize = FIntPoint(64, 64);
				Rabbit.Textures = { TEXT("rabbit/rabbit_brown.png") };
				for (const TCHAR* Coat : { TEXT("brown"), TEXT("white"), TEXT("black"), TEXT("gold"), TEXT("salt"), TEXT("white_splotched"), TEXT("toast") })
				{
					Rabbit.Variants.Add({ FString::Printf(TEXT("rabbit/rabbit_%s.png"), Coat) });
				}
				Rabbit.Parts.Add(Part(ERole::Static, FVector(0, 5, 0), { Box(0, 0, -4, -3, -5, 8, 6, 10) }));
				Rabbit.Parts.Add(Part(ERole::Head, FVector(0, 7, 5), { Box(0, 16, -2.5f, 0, 0, 5, 5, 5), Box(26, 0, -2.5f, 5, 0, 2, 5, 1), Box(32, 0, 0.5f, 5, 0, 2, 5, 1) }));
				const FBox Haunch = Box(20, 16, -2, -4, -2, 4, 4, 4);
				const FBox Foot = Box(20, 24, -1, -4, -3, 2, 1, 6);
				Rabbit.Parts.Add(Part(ERole::LegBackRight, FVector(-4, 4, -3), { Haunch, Foot }));
				Rabbit.Parts.Add(Part(ERole::LegBackLeft, FVector(4, 4, -3), { Haunch, Foot }));
				Rabbit.Parts.Add(Part(ERole::Static, FVector(0, 6, -5), { Box(24, 16, -1.5f, -1.5f, -1, 3, 3, 1) }));
				Models.Add(Rabbit.Kind, Rabbit);
			}
			{
				// The sea turtle's shell, belly and head, and its four flippers (nets read off the picture).
				FModel Turtle;
				Turtle.Kind = TEXT("minecraft:turtle");
				Turtle.TextureSize = FIntPoint(128, 64);
				Turtle.Textures = { TEXT("turtle/turtle.png") };
				Turtle.Parts.Add(Part(ERole::Static, FVector(0, 6, -1), { Box(7, 37, -9.5f, -10, -3, 19, 20, 6) }, -Flat));
				Turtle.Parts.Add(Part(ERole::Static, FVector(0, 3, -1), { Box(31, 1, -5.5f, -9, -1.5f, 11, 18, 3) }, Flat));
				Turtle.Parts.Add(Part(ERole::Head, FVector(0, 5, 9), { Box(3, 0, -3, -2, 0, 6, 5, 6) }));
				Turtle.Parts.Add(Part(ERole::LegBackRight, FVector(-3.5f, 2, -9), { Box(1, 23, -2, -1, -10, 4, 1, 10) }));
				Turtle.Parts.Add(Part(ERole::LegBackLeft, FVector(3.5f, 2, -9), { Box(1, 23, -2, -1, -10, 4, 1, 10, 0.0f, true) }));
				Turtle.Parts.Add(Part(ERole::LegFrontRight, FVector(-5, 2, 4), { Box(27, 30, -13, -1, -2, 13, 1, 5) }));
				Turtle.Parts.Add(Part(ERole::LegFrontLeft, FVector(5, 2, 4), { Box(27, 30, 0, -1, -2, 13, 1, 5, 0.0f, true) }));
				Models.Add(Turtle.Kind, Turtle);
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

		// A held picture: two faces back to back, the whole of the picture on each.
		void AddFlat(const FBox& Box, const FQuat& PartRotation, const FVector& Pivot, float BodyYaw, float Hurt, float WholeScale, McPlayerModel::FMeshData& Out)
		{
			const FQuat ModelRotation(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw));
			if (Box.bFlatTop)
			{
				// Corners of the picture (u right, v up) laid in the x-y plane with its diagonal along -y.
				const FVector Centre(Box.Min.X + Box.Size.X * 0.5f, Box.Min.Y + Box.Size.Y * 0.5f, Box.Min.Z);
				const FVector U(0.7071, -0.7071, 0.0);
				const FVector V(-0.7071, -0.7071, 0.0);
				const FVector2D Uv[4] = { FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) };
				for (const bool bBack : { false, true })
				{
					const FVector Normal = ModelRotation.RotateVector(PartRotation.RotateVector(FVector(0.0, 0.0, bBack ? -1.0 : 1.0)));
					const FColor Color = Shade(Normal, Hurt);
					const int32 First = Out.Positions.Num();
					for (const FVector2D& Corner : Uv)
					{
						const FVector Local = Centre + Box.Size.Y * ((Corner.X - 0.5) * U + (Corner.Y - 0.5) * V);
						const FVector Model = ModelRotation.RotateVector(Pivot + PartRotation.RotateVector(Local)) * WholeScale;
						Out.Positions.Add(FVector(Model.X, Model.Z, Model.Y) * (McCoords::UnitsPerBlock / 16.0));
						Out.Normals.Add(FVector(Normal.X, Normal.Z, Normal.Y));
						Out.UV0.Add(FVector2D(Corner.X, 1.0 - Corner.Y));
						Out.Colors.Add(Color);
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
				return;
			}
			const float Y0 = Box.Min.Y;
			const float Y1 = Box.Min.Y + Box.Size.Y;
			const float Z0 = Box.Min.Z;
			const float Z1 = Box.Min.Z + Box.Size.Z;
			const FVector Corners[4] = { FVector(Box.Min.X, Y1, Z0), FVector(Box.Min.X, Y1, Z1), FVector(Box.Min.X, Y0, Z1), FVector(Box.Min.X, Y0, Z0) };
			// Hanging down from a shoulder the top of the picture is towards -y; mirrored, it stands up with its top towards +y.
			const FVector2D UVs[4] = { FVector2D(0, Box.bMirror ? 0 : 1), FVector2D(1, Box.bMirror ? 0 : 1), FVector2D(1, Box.bMirror ? 1 : 0), FVector2D(0, Box.bMirror ? 1 : 0) };
			for (const bool bBack : { false, true })
			{
				const FVector Normal = ModelRotation.RotateVector(PartRotation.RotateVector(FVector(bBack ? -1.0 : 1.0, 0.0, 0.0)));
				const FColor Color = Shade(Normal, Hurt);
				const int32 First = Out.Positions.Num();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector Model = ModelRotation.RotateVector(Pivot + PartRotation.RotateVector(Corners[Corner])) * WholeScale;
					Out.Positions.Add(FVector(Model.X, Model.Z, Model.Y) * (McCoords::UnitsPerBlock / 16.0));
					Out.Normals.Add(FVector(Normal.X, Normal.Z, Normal.Y));
					Out.UV0.Add(UVs[Corner]);
					Out.Colors.Add(Color);
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

		void AddBox(const FBox& Box, const FPart& Part, const FQuat& PartRotation, const FVector& Pivot, float BodyYaw, FIntPoint TextureSize, float Hurt, bool bGlow, float LocalScale, float WholeScale, McPlayerModel::FMeshData& Out)
		{
			const FVector Min = (Box.Min - FVector(Box.Inflate)) * LocalScale;
			const FVector Max = (Box.Min + FVector(Box.Size.X, Box.Size.Y, Box.Size.Z) + FVector(Box.Inflate)) * LocalScale;
			const FQuat ModelRotation(FVector::YAxisVector, -FMath::DegreesToRadians(BodyYaw));
			const auto Place = [&](const FVector& Local)
			{
				return ModelRotation.RotateVector(Pivot + PartRotation.RotateVector(Local)) * WholeScale;
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

	TArray<FString> GetVillagerTextures(bool bZombie, FName Variant, FName Profession, int32 Level)
	{
		const FString Folder = bZombie ? TEXT("zombie_villager/") : TEXT("villager/");
		const FString Type = Variant.IsNone() ? FString(TEXT("plains")) : Variant.ToString();
		const FString Job = Profession.IsNone() ? FString() : McCache::PathOf(Profession.ToString());
		// A jobless villager has no clothes picture, and a nitwit has clothes but no badge.
		const bool bClothes = !Job.IsEmpty() && Job != TEXT("none");
		static const TCHAR* Badges[] = { TEXT("stone"), TEXT("iron"), TEXT("gold"), TEXT("emerald"), TEXT("diamond") };
		TArray<FString> Textures;
		Textures.Add(Folder + (bZombie ? TEXT("zombie_villager.png") : TEXT("villager.png")));
		Textures.Add(Folder + TEXT("type/") + Type + TEXT(".png"));
		Textures.Add(bClothes ? Folder + TEXT("profession/") + Job + TEXT(".png") : FString());
		Textures.Add(bClothes && Job != TEXT("nitwit") ? Folder + TEXT("profession_level/") + Badges[FMath::Clamp(Level, 1, 5) - 1] + TEXT(".png") : FString());
		return Textures;
	}

	FColor GetDyeColor(int32 Index)
	{
		static const FColor Colors[16] = {
			FColor(249, 255, 254), FColor(249, 128, 29), FColor(199, 78, 189), FColor(58, 179, 218), FColor(254, 216, 61), FColor(128, 199, 31),
			FColor(243, 139, 170), FColor(71, 79, 82), FColor(157, 157, 151), FColor(22, 156, 156), FColor(137, 50, 184), FColor(60, 68, 170),
			FColor(131, 84, 50), FColor(94, 124, 22), FColor(176, 46, 38), FColor(29, 29, 33) };
		return Colors[FMath::Clamp(Index, 0, 15)];
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
			if (Part.Layer != Layer || ((Part.ShowPoses >> FMath::Clamp(Anim.Pose, 0, 31)) & 1u) == 0)
			{
				continue;
			}
			// A young one is half the size, with its head only a third smaller than a grown one's, so the head is big for it.
			const bool bHeadBig = Anim.bBaby && Part.Role == ERole::Head;
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
				if (Model.IllagerStyle != 0 && Anim.Pose != 0)
				{
					// Armed, the arms leave the folded pair: forward on a crossbow (one drawn back as it charges), up with an axe,
					// up and out as a spell is cast.
					const float Wave = FMath::Sin(Anim.Age * 9.0f);
					if (Anim.Pose == 3)
					{
						// Both arms out and up to the sides, mirror images, swinging together (vanilla's 135 degrees of roll, its
						// sign flipped in this model space).
						Pitch += FMath::Cos(Anim.Age * 13.0f * 0.6662f) * 0.25f;
						Roll += (bRight ? -2.3562f : 2.3562f);
					}
					else if (Model.IllagerStyle == 1)
					{
						// Both straight forward at the target, the right a little inward and the left more, as the crossbow is held.
						Pitch += (bRight ? -1.5708f : (Anim.Pose == 2 ? -0.9f : -1.5708f));
						Yaw += (bRight ? 0.3f : -0.6f);
					}
					else
					{
						Pitch += (bRight ? -2.3f : -1.0f) + Wave * 0.35f * Amount + Wave * 0.15f;
					}
					break;
				}
				if (Anim.bOffering && bRight)
				{
					// Held out in front, a little up.
					Pitch += -HALF_PI * 0.9f + FMath::Sin(Anim.Age * 2.0f) * 0.05f;
					break;
				}
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
				Roll += Side * (Model.bFlutter ? 0.7f + FMath::Sin(Anim.Age * 40.0f) * 0.5f : 0.08f + (Amount > 0.4f ? FMath::Abs(FMath::Sin(Anim.Age * 25.0f)) * 0.9f : 0.0f));
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
				if (Box.bFlat)
				{
					AddFlat(Box, Rotation, Part.Pivot, Anim.BodyYaw, Anim.Hurt, Anim.bBaby ? BabyScale : 1.0f, Out);
					continue;
				}
				AddBox(Box, Part, Rotation, Part.Pivot, Anim.BodyYaw, Model.TextureSize, Anim.Hurt, bGlow, bHeadBig ? BabyHeadScale : 1.0f, Anim.bBaby ? BabyScale : 1.0f, Out);
			}
		}
		if (((Model.TintedLayers >> Layer) & 1u) != 0)
		{
			for (FColor& Color : Out.Colors)
			{
				Color.R = static_cast<uint8>(Color.R * Anim.Tint.R / 255);
				Color.G = static_cast<uint8>(Color.G * Anim.Tint.G / 255);
				Color.B = static_cast<uint8>(Color.B * Anim.Tint.B / 255);
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

	// The pictures laid over each other, kept like LoadCached's (a villager's look is the same for every villager that has it).
	UTexture2DArray* LoadCompositeCached(const TArray<FString>& Paths, FIntPoint& OutSize)
	{
		const FString Key = FString::Join(Paths, TEXT("|")) + TEXT("#composite");
		if (const TStrongObjectPtr<UTexture2DArray>* Found = TextureCache().Find(Key))
		{
			OutSize = SizeCache().FindRef(Key);
			return Found->Get();
		}
		int32 Width = 0, Height = 0;
		UTexture2DArray* Texture = McRenderTextures::LoadPngLayersAsArray(GetTransientPackage(), Paths, Width, Height);
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
	TextureOverrides.Reset();
	LookKey = NAME_None;
	VariantSeed = INT32_MIN;
	WolfTextureState = 0;
	bBabyLook = false;
	bIsItem = Kind == TEXT("minecraft:item");
	bIsArrow = Kind == TEXT("minecraft:arrow");
	Model = bIsItem || bIsArrow ? nullptr : McEntityModel::Find(Kind);
	Materials.Reset();
	ClearAllMeshSections();
	NumSections = 0;
	bItemFailed = false;
	bFlashing = false;
	bDirty = true;
}

void UMcEntityModelComponent::SetVillagerLook(FName InVariant, FName InProfession, int32 InLevel, bool bInBaby)
{
	const bool bVillager = Kind == TEXT("minecraft:villager");
	const bool bZombie = Kind == TEXT("minecraft:zombie_villager");
	if (!bVillager && !bZombie)
	{
		return;
	}
	const FName Key(*FString::Printf(TEXT("%s|%s|%d"), *InVariant.ToString(), *InProfession.ToString(), InLevel));
	if (Key == LookKey)
	{
		return;
	}
	LookKey = Key;
	TextureOverrides = McEntityModel::GetVillagerTextures(bZombie, InVariant, InProfession, InLevel);
	// The layers are made again from the new pictures.
	Materials.Reset();
	ClearAllMeshSections();
	NumSections = 0;
	bFlashing = false;
}

void UMcEntityModelComponent::SetVariantSeed(int32 InSeed)
{
	if (!Model || Model->Variants.IsEmpty() || InSeed == VariantSeed)
	{
		return;
	}
	VariantSeed = InSeed;
	ApplyVariant();
}

void UMcEntityModelComponent::ApplyVariant()
{
	TextureOverrides = Model->Variants[FMath::Abs(VariantSeed) % Model->Variants.Num()];
	if (Kind == TEXT("minecraft:wolf") && WolfTextureState != 0 && !TextureOverrides.IsEmpty())
	{
		// wolf_ashen.png -> wolf_ashen_tame.png, wolf_ashen_angry.png
		FString& Base = TextureOverrides[0];
		Base = Base.LeftChop(4) + (WolfTextureState == 2 ? TEXT("_tame.png") : TEXT("_angry.png"));
	}
	Materials.Reset();
	ClearAllMeshSections();
	NumSections = 0;
	bFlashing = false;
}

void UMcEntityModelComponent::SetPose(int32 InPose)
{
	PoseLook = InPose;
	if (Kind == TEXT("minecraft:wolf"))
	{
		// Tame wins over angry (a tamed wolf is never drawn angry); the picture changes only when that does.
		const int32 State = (InPose & 4) ? 2 : (InPose & 2) ? 1 : 0;
		if (State != WolfTextureState)
		{
			WolfTextureState = State;
			if (Model && !Model->Variants.IsEmpty() && VariantSeed != INT32_MIN)
			{
				ApplyVariant();
			}
		}
	}
}

bool UMcEntityModelComponent::HasModel() const
{
	return bIsItem || bIsArrow || Model != nullptr;
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
	if (TextureOverrides.IsValidIndex(Layer))
	{
		return TextureOverrides[Layer].IsEmpty() ? FString() : McCache::Asset(TEXT("textures/entity/") + TextureOverrides[Layer]);
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
	UTexture2DArray* Texture = nullptr;
	if (Model && Model->bComposite)
	{
		TArray<FString> Paths;
		for (int32 Index = 0; Index < FMath::Max(Model->Textures.Num(), TextureOverrides.Num()); ++Index)
		{
			Paths.Add(GetTexturePath(Index));
		}
		Texture = LoadCompositeCached(Paths, Size);
	}
	else
	{
		Texture = LoadCached(Path, Size);
	}
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
	const int32 NumLayers = Model->bComposite ? 1 : Model->Textures.Num();
	// Every other tenth of the fuse the creeper goes white.
	const bool bFlash = Swelling > 0.0f && McEntityModel::IsSwellFlash(Swelling);
	Anim.bFlash = bFlash;
	Anim.bBaby = bBabyLook;
	Anim.bOffering = bOfferingLook;
	Anim.Pose = PoseLook & 31;
	if (Model->TintedLayers != 0)
	{
		Anim.Tint = McEntityModel::GetDyeColor((PoseLook >> 8) & 15);
	}
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
