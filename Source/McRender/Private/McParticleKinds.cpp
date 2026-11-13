#include "McParticleKinds.h"

namespace
{
	using McParticleKinds::FKind;
	using McParticleKinds::EFrames;

	FKind Row(const TCHAR* Name, int32 LifeMin, int32 LifeMax, float Size)
	{
		FKind Kind;
		Kind.Name = FName(*(FString(TEXT("minecraft:")) + Name));
		Kind.LifeMin = LifeMin;
		Kind.LifeMax = LifeMax;
		Kind.Size = Size;
		return Kind;
	}

	TArray<FKind> BuildTable()
	{
		TArray<FKind> Table;
		// Fitted to the vanilla runs scripts/qb1 to qb4 (one particle spawned at rest, frozen, a shot per tick; PLAN.md "Particles"):
		// life is the ticks it showed for plus one; size the half width of its picture's visible box measured at a known depth; Rise the
		// start velocity and Friction and Gravity the least squares fit of its height over its life. Sizes differ about a third between
		// two particles of one kind in the runs (heart 0.30 and 0.20), so the mid value is used.
		const auto Add = [&Table](const TCHAR* Name, int32 LifeMin, int32 LifeMax, float Size, float Grow, float Rise, float Friction, float Gravity, EFrames Frames, bool bGlow = false)
		{
			FKind Kind = Row(Name, LifeMin, LifeMax, Size);
			Kind.Grow = Grow;
			Kind.Rise = Rise;
			Kind.Friction = Friction;
			Kind.Gravity = Gravity;
			Kind.Frames = Frames;
			Kind.bGlow = bGlow;
			Table.Add(Kind);
		};
		Add(TEXT("heart"), 18, 18, 0.25f, 0.0f, 0.12f, 0.84f, 0.0f, EFrames::First);
		Add(TEXT("happy_villager"), 25, 51, 0.125f, 0.0f, 0.01f, 0.55f, 0.0f, EFrames::First);
		Add(TEXT("angry_villager"), 18, 18, 0.22f, 0.0f, 0.115f, 0.86f, 0.0f, EFrames::First);
		Add(TEXT("crit"), 7, 7, 0.09f, 0.0f, 0.005f, 0.62f, 0.035f, EFrames::First);
		Add(TEXT("enchanted_hit"), 7, 7, 0.1f, 0.0f, 0.0f, 0.64f, 0.0325f, EFrames::First);
		Add(TEXT("damage_indicator"), 21, 21, 0.095f, 0.0f, 0.41f, 0.70f, 0.03f, EFrames::First);
		Add(TEXT("witch"), 10, 12, 0.09f, -0.55f, 0.005f, 0.94f, -0.005f, EFrames::ByAge);
		Add(TEXT("poof"), 39, 39, 0.11f, -0.75f, 0.045f, 0.84f, -0.0075f, EFrames::ByAge);
		Add(TEXT("smoke"), 25, 25, 0.12f, -0.88f, 0.015f, 1.0f, -0.0025f, EFrames::ByAge);
		Add(TEXT("large_smoke"), 42, 42, 0.09f, -0.7f, 0.02f, 0.9f, -0.0025f, EFrames::ByAge);
		Add(TEXT("white_smoke"), 17, 17, 0.13f, -0.87f, 0.01f, 0.94f, -0.005f, EFrames::ByAge);
		Add(TEXT("cloud"), 51, 51, 0.133f, -0.55f, 0.03f, 0.66f, -0.0025f, EFrames::ByAge);
		Add(TEXT("flame"), 27, 27, 0.05f, -0.5f, 0.04f, 0.52f, 0.0f, EFrames::First, true);
		Add(TEXT("small_flame"), 14, 14, 0.048f, -0.55f, 0.0f, 0.54f, 0.0f, EFrames::First, true);
		Add(TEXT("dripping_water"), 46, 46, 0.04f, 0.0f, 0.07f, 0.5f, 0.005f, EFrames::First);
		Add(TEXT("dripping_lava"), 46, 46, 0.035f, 0.0f, 0.07f, 0.5f, 0.005f, EFrames::First, true);
		Add(TEXT("soul"), 43, 43, 0.09f, -0.5f, 0.0f, 0.54f, -0.005f, EFrames::ByAge);
		Add(TEXT("sneeze"), 20, 20, 0.1f, 0.0f, 0.0f, 0.9f, 0.0f, EFrames::ByAge);
		Add(TEXT("glow"), 13, 13, 0.106f, 0.0f, 0.04f, 0.74f, -0.01f, EFrames::First, true);
		Add(TEXT("firework"), 20, 20, 0.11f, -0.85f, 0.03f, 0.54f, 0.02f, EFrames::ByAge, true);
		Add(TEXT("falling_water"), 5, 5, 0.04f, 0.0f, 0.01f, 0.9f, 0.075f, EFrames::First);
		// A dripstone tip's drop (run scripts/qd1, six tips, a shot a tick, 146.7 px a block at the drop's depth): it hangs 42 to 48 ticks
		// growing from 2 px to about 6 px wide, then falls 0.045 blocks a tick faster each tick (steps of 7, 14, 21, 27, 33, 40, 47, 52 px)
		// and goes where it lands.
		Add(TEXT("dripping_dripstone_water"), 46, 46, 0.006f, 2.6f, 0.0f, 1.0f, 0.0f, EFrames::First);
		Table.Last().Then = FName("minecraft:falling_dripstone_water");
		Add(TEXT("falling_dripstone_water"), 60, 60, 0.03f, 0.0f, 0.0f, 1.0f, 0.045f, EFrames::First);
		Table.Last().bPhysics = true;
		Table.Last().bDieOnLand = true;
		Add(TEXT("explosion"), 9, 9, 0.375f, -0.27f, 0.0f, 0.98f, 0.0f, EFrames::ByAge);
		// Smoke is a grey picture the game darkens: the runs showed it near black (mean 13 and 18 of 255 against a grey wall).
		for (FKind& Kind : Table)
		{
			if (Kind.Name == FName(TEXT("minecraft:smoke")))
			{
				Kind.Tint = FColor(24, 24, 24);
			}
			else if (Kind.Name == FName(TEXT("minecraft:large_smoke")))
			{
				Kind.Tint = FColor(36, 36, 36);
			}
		}
		return Table;
	}

	const TArray<FKind>& Table()
	{
		static const TArray<FKind> Rows = BuildTable();
		return Rows;
	}
}

const McParticleKinds::FKind& McParticleKinds::Find(FName Name)
{
	FString Id = Name.ToString();
	if (!Id.Contains(TEXT(":")))
	{
		Id = TEXT("minecraft:") + Id;
	}
	const FName Full(*Id);
	for (const FKind& Kind : Table())
	{
		if (Kind.Name == Full)
		{
			return Kind;
		}
	}
	static TMap<FName, TSharedPtr<FKind>> Defaults;
	TSharedPtr<FKind>& Found = Defaults.FindOrAdd(Full);
	if (!Found.IsValid())
	{
		Found = MakeShared<FKind>(Row(TEXT(""), 20, 20, 0.1f));
		Found->Name = Full;
	}
	return *Found;
}

const TArray<McParticleKinds::FKind>& McParticleKinds::GetAll()
{
	return Table();
}
