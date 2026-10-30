// Villagers (minecraft.wiki, "Villager"): a mob with a biome type, a profession taken from the job site block it
// claims, a level from novice to master that its trades earn, and a baby form that grows up in 20 minutes. What
// they do through the day is in McVillager.cpp's routine; what they sell is in McTrading (McGen).

#pragma once

#include "CoreMinimal.h"
#include "McMob.h"

namespace McVillager
{
	constexpr int32 MaxLevel = 5;
	// A baby takes this long to grow up.
	constexpr int32 BabyTicks = 24000;
	// How far a villager looks for a job site, and how far up and down.
	constexpr int32 SearchRadius = 16;
	constexpr int32 SearchHeight = 6;

	// The look of a villager by the biome it was born in: "plains", "desert" and so on, without the namespace.
	MCENTITY_API FName GetVariantOfBiome(FName Biome);
	MCENTITY_API const TArray<FName>& GetVariants();

	// The profession a job site block gives ("minecraft:composter" gives "minecraft:farmer"); none for any other block.
	MCENTITY_API FName GetProfessionOfJobSite(FName Block);
	// The block a profession works at; none for nitwit and the jobless.
	MCENTITY_API FName GetJobSite(FName Profession);
	MCENTITY_API const TArray<FName>& GetProfessions();

	// The levels run novice (1), apprentice, journeyman, expert and master (5); a villager reaches each at 0, 10, 70,
	// 150 and 250 experience.
	MCENTITY_API int32 GetXpForLevel(int32 Level);
	MCENTITY_API int32 GetLevelOfXp(int32 Xp);
	// "Novice", "Apprentice", "Journeyman", "Expert" or "Master".
	MCENTITY_API FString GetLevelName(int32 Level);

	// What a villager is doing by the hour (minecraft.wiki, "Villager#Daily routine"), the day starting at 0 with sunrise:
	// work at the job site from 2000 to 9000, meet at the bell until 11000, free time to 12000 and sleep in a bed after.
	// A sounding alarm sends every villager to the bell.
	enum class EActivity : uint8
	{
		Idle,
		Work,
		Meet,
		Rest,
		Alarm,
	};
	constexpr int32 WorkStart = 2000;
	constexpr int32 MeetStart = 9000;
	constexpr int32 FreeStart = 11000;
	constexpr int32 RestStart = 12000;
	// How far a villager looks for a bell, wider than for a job site or bed.
	constexpr int32 BellRadius = 48;
	// How long a bell rung for an alarm keeps the villagers at it.
	constexpr int32 AlarmTicks = 1200;
	MCENTITY_API EActivity GetActivityAt(int32 TimeOfDay, bool bAlarm);

	// Whether the block is a bed (either half) and whether it is a bell.
	MCENTITY_API bool IsBed(FName Block);
	MCENTITY_API bool IsBell(FName Block);

	// The nearest block within Radius across and Height up and down for which Is (given its name and place) gives true; false if none.
	MCENTITY_API bool FindNearestBlock(const FMcBlockWorld& World, const FIntVector& Origin, int32 Radius, int32 Height, TFunctionRef<bool(FName, const FIntVector&)> Is, FIntVector& Out);
}

class MCENTITY_API FMcVillager : public FMcMob
{
public:
	explicit FMcVillager(const FMcMobType& InType);
	static const FName& StaticType();

	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool Serialize(FArchive& Ar) override;

	// A baby is half the size, takes no profession and can't trade. Growing up is BabyTicks from birth.
	void SetBaby(bool bInBaby);
	bool IsBaby() const { return bBaby; }
	int32 GetGrowthTicksLeft() const { return GrowthTicks; }
	void SetGrowthTicksLeft(int32 Ticks) { GrowthTicks = Ticks; }

	// The profession, "minecraft:none" for a jobless villager and "minecraft:nitwit" for one that will never work.
	FName GetProfession() const { return Profession; }
	void SetProfession(FName InProfession);
	bool HasProfession() const { return Profession != NoProfession() && Profession != NitwitProfession(); }
	FName GetVariant() const { return Variant; }
	void SetVariant(FName InVariant) { Variant = InVariant; }
	int32 GetLevel() const { return Level; }
	int32 GetXp() const { return Xp; }
	void SetLevelAndXp(int32 InLevel, int32 InXp) { Level = FMath::Clamp(InLevel, 1, McVillager::MaxLevel); Xp = FMath::Max(0, InXp); }

	// The job site claimed, if any; none for the jobless.
	TOptional<FIntVector> GetJobSite() const { return bHasJobSite ? TOptional<FIntVector>(JobSite) : TOptional<FIntVector>(); }
	void ClaimJobSite(const FIntVector& Pos) { bHasJobSite = true; JobSite = Pos; }
	void ReleaseJobSite() { bHasJobSite = false; }

	// What it is doing now (decided about once a second), the bed it has claimed and whether it is asleep in it.
	McVillager::EActivity GetActivity() const { return Activity; }
	TOptional<FIntVector> GetBed() const { return bHasBed ? TOptional<FIntVector>(Bed) : TOptional<FIntVector>(); }
	void ClaimBed(const FIntVector& Pos) { bHasBed = true; Bed = Pos; }
	void ReleaseBed() { bHasBed = false; bSleeping = false; }
	bool IsSleeping() const { return bSleeping; }

	static FName NoProfession(){ return FName(TEXT("minecraft:none")); }
	static FName NitwitProfession() { return FName(TEXT("minecraft:nitwit")); }

private:
	// Once a second or so: a jobless adult looks for a job site nobody has claimed and takes its profession; one that
	// has lost its job site (and never traded) goes jobless again.
	void UpdateProfession(FMcEntityManager& Manager);
	// Whether another villager has the block as its job site.
	bool IsClaimedByOther(FMcEntityManager& Manager, const FIntVector& Pos) const;
	// Whether another villager has the bed (either half of it) as its own.
	bool IsBedClaimedByOther(FMcEntityManager& Manager, const FIntVector& Pos) const;
	// Once a second: picks the activity for the hour and walks to where it is done, or stays put when there.
	void UpdateRoutine(FMcEntityManager& Manager);
	// Heads for Place (block coordinates), and stays put once within two blocks. True when there.
	bool WalkTo(const FVector& Place);

	FName Variant;
	FName Profession;
	int32 Level = 1;
	int32 Xp = 0;
	bool bBaby = false;
	int32 GrowthTicks = 0;
	bool bHasJobSite = false;
	FIntVector JobSite = FIntVector::ZeroValue;
	McVillager::EActivity Activity = McVillager::EActivity::Idle;
	bool bHasBed = false;
	FIntVector Bed = FIntVector::ZeroValue;
	bool bSleeping = false;
	// The bell found for the meeting, looked for again when the activity changes.
	bool bHasBell = false;
	FIntVector Bell = FIntVector::ZeroValue;
};
