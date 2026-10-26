#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McChunk.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McNbt.h"
#include "McNoiseGenerator.h"
#include "McRandom.h"
#include "McStructureTemplate.h"
#include "McStructures.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString Describe(FMcStateId State)
	{
		return FMcBlockRegistry::Get().Describe(State);
	}

	FMcStateId Parse(const TCHAR* Text)
	{
		return FMcBlockRegistry::Get().ParseState(Text).Get(0);
	}

	// A 3x3 patch of chunks around chunk (1, 1), flat land or a sea over a sandy floor.
	struct FPatch
	{
		TArray<TUniquePtr<FMcChunk>> Chunks;
		FMcChunk* Around[9] = {};

		FPatch(const FMcNoiseSettings& Settings, bool bSea, FMcBiomeId Biome)
		{
			const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
			const FMcStateId Dirt = Registry.FindDefaultState(TEXT("minecraft:dirt")).Get(0);
			const FMcStateId Grass = Registry.FindDefaultState(TEXT("minecraft:grass_block")).Get(0);
			const FMcStateId Sand = Registry.FindDefaultState(TEXT("minecraft:sand")).Get(0);
			const FMcStateId Water = Registry.FindDefaultState(TEXT("minecraft:water")).Get(0);
			for (int32 Index = 0; Index < 9; ++Index)
			{
				Chunks.Add(MakeUnique<FMcChunk>(FIntPoint(Index % 3, Index / 3), Settings.MinY, Settings.Height / 16));
				FMcChunk& Chunk = *Chunks.Last();
				Chunk.FillBiome(Biome);
				for (int32 Z = 0; Z < 16; ++Z)
				{
					for (int32 X = 0; X < 16; ++X)
					{
						if (bSea)
						{
							for (int32 Y = 40; Y < 45; ++Y)
							{
								Chunk.SetBlock(X, Y, Z, Dirt);
							}
							Chunk.SetBlock(X, 45, Z, Sand);
							for (int32 Y = 46; Y < Settings.SeaLevel; ++Y)
							{
								Chunk.SetBlock(X, Y, Z, Water);
							}
						}
						else
						{
							Chunk.SetBlock(X, 62, Z, Dirt);
							Chunk.SetBlock(X, 63, Z, Grass);
						}
					}
				}
				Around[Index] = &Chunk;
			}
		}
	};

	// The block names a chunk has that a snapshot of it didn't, and how many blocks changed.
	struct FChange
	{
		int32 Changed = 0;
		TSet<FString> Names;
	};

	TArray<FMcStateId> Snapshot(const FMcChunk& Chunk)
	{
		TArray<FMcStateId> Blocks;
		Blocks.Reserve(16 * 16 * (Chunk.GetMaxY() - Chunk.GetMinY() + 1));
		for (int32 Y = Chunk.GetMinY(); Y <= Chunk.GetMaxY(); ++Y)
		{
			for (int32 Z = 0; Z < 16; ++Z)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					Blocks.Add(Chunk.GetBlock(X, Y, Z));
				}
			}
		}
		return Blocks;
	}

	void Diff(const FMcChunk& Chunk, const TArray<FMcStateId>& Before, FChange& Out)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		int32 Index = 0;
		for (int32 Y = Chunk.GetMinY(); Y <= Chunk.GetMaxY(); ++Y)
		{
			for (int32 Z = 0; Z < 16; ++Z)
			{
				for (int32 X = 0; X < 16; ++X, ++Index)
				{
					const FMcStateId Now = Chunk.GetBlock(X, Y, Z);
					if (Now != Before[Index])
					{
						++Out.Changed;
						Out.Names.Add(Registry.GetBlockOf(Now).Name.ToString());
					}
				}
			}
		}
	}

	// Finds a block of a given name in a template placed at Origin with these settings: where it ended up.
	TOptional<FIntVector> FindBlock(const FMcStructureTemplate& Template, const TCHAR* BlockName, const FIntVector& Origin, const FMcTemplatePlaceSettings& Settings, FMcStateId* OutState = nullptr)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		for (const FMcStructureTemplate::FBlock& Block : Template.Blocks)
		{
			const FMcStateId State = Template.Palettes[0][Block.Palette];
			if (State != McTemplate::SkipState && Registry.GetBlockOf(State).Name == FName(BlockName))
			{
				if (OutState)
				{
					*OutState = State;
				}
				return Template.ToWorld(Block.Pos, Origin, Settings);
			}
		}
		return NullOpt;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcStructureTest, "McClient.McGen.Structure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcStructureTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Blocks.EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}

	// ---- Every template in the jar loads ----
	TArray<FString> Ids;
	FMcStructureTemplate::ListIds(Ids);
	TestTrue(TEXT("the jar has its structure templates"), Ids.Num() > 1400);
	int32 Loaded = 0;
	int32 Jigsaws = 0;
	int32 JigsawsWithoutState = 0;
	for (const FString& Id : Ids)
	{
		const TSharedPtr<const FMcStructureTemplate> Template = FMcStructureTemplate::Find(Id);
		if (!Template.IsValid() || Template->Blocks.IsEmpty() || Template->Size.X <= 0 || Template->Size.Y <= 0 || Template->Size.Z <= 0)
		{
			AddError(FString::Printf(TEXT("template %s doesn't load"), *Id));
			continue;
		}
		++Loaded;
		for (const FMcStructureTemplate::FBlock& Block : Template->Blocks)
		{
			Jigsaws += Block.bJigsaw ? 1 : 0;
			JigsawsWithoutState += (Block.bJigsaw && Block.Final == McTemplate::SkipState) ? 1 : 0;
			if (!Template->Palettes[0].IsValidIndex(Block.Palette) || Block.Pos.X >= Template->Size.X || Block.Pos.Y >= Template->Size.Y || Block.Pos.Z >= Template->Size.Z)
			{
				AddError(FString::Printf(TEXT("template %s has a block out of its size"), *Id));
				break;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d of %d templates load, %d jigsaw blocks"), Loaded, Ids.Num(), Jigsaws));
	TestEqual(TEXT("every template loads"), Loaded, Ids.Num());
	TestEqual(TEXT("every jigsaw block has its final state"), JigsawsWithoutState, 0);

	// ---- Turning and mirroring ----
	const FIntVector Offset(2, 5, 3);
	auto TestVec = [this](const TCHAR* What, const FIntVector& Actual, const FIntVector& Expected)
	{
		TestTrue(FString::Printf(TEXT("%s (%s, expected %s)"), What, *Actual.ToString(), *Expected.ToString()), Actual == Expected);
	};
	TestVec(TEXT("no turn"), McTemplate::TransformPos(Offset, EMcRotation::None, EMcMirror::None), Offset);
	TestVec(TEXT("clockwise 90: east goes south, south goes west"), McTemplate::TransformPos(Offset, EMcRotation::Clockwise90, EMcMirror::None), FIntVector(-3, 5, 2));
	TestVec(TEXT("180"), McTemplate::TransformPos(Offset, EMcRotation::Clockwise180, EMcMirror::None), FIntVector(-2, 5, -3));
	TestVec(TEXT("counterclockwise 90"), McTemplate::TransformPos(Offset, EMcRotation::CounterClockwise90, EMcMirror::None), FIntVector(3, 5, -2));
	TestVec(TEXT("left-right flips z"), McTemplate::TransformPos(Offset, EMcRotation::None, EMcMirror::LeftRight), FIntVector(2, 5, -3));
	TestVec(TEXT("front-back flips x"), McTemplate::TransformPos(Offset, EMcRotation::None, EMcMirror::FrontBack), FIntVector(-2, 5, 3));
	TestVec(TEXT("mirror, then turn"), McTemplate::TransformPos(Offset, EMcRotation::Clockwise90, EMcMirror::FrontBack), FIntVector(-3, 5, -2));
	TestVec(TEXT("round a pivot"), McTemplate::TransformPos(FIntVector(5, 0, 5), EMcRotation::Clockwise90, EMcMirror::None, FIntVector(3, 0, 5)), FIntVector(3, 0, 7));

	auto Turned = [](const TCHAR* Text, EMcRotation Rotation, EMcMirror Mirror = EMcMirror::None)
	{
		return Describe(McTemplate::TransformState(Parse(Text), Rotation, Mirror));
	};
	TestEqual(TEXT("stairs face east after a quarter turn"), Turned(TEXT("minecraft:oak_stairs[facing=north]"), EMcRotation::Clockwise90), Describe(Parse(TEXT("minecraft:oak_stairs[facing=east]"))));
	TestEqual(TEXT("stairs face south after a half turn"), Turned(TEXT("minecraft:oak_stairs[facing=north]"), EMcRotation::Clockwise180), Describe(Parse(TEXT("minecraft:oak_stairs[facing=south]"))));
	TestEqual(TEXT("stairs face west after three"), Turned(TEXT("minecraft:oak_stairs[facing=north]"), EMcRotation::CounterClockwise90), Describe(Parse(TEXT("minecraft:oak_stairs[facing=west]"))));
	TestEqual(TEXT("up and down stay"), Turned(TEXT("minecraft:barrel[facing=up]"), EMcRotation::Clockwise90), Describe(Parse(TEXT("minecraft:barrel[facing=up]"))));
	TestEqual(TEXT("logs lie the other way"), Turned(TEXT("minecraft:oak_log[axis=x]"), EMcRotation::Clockwise90), Describe(Parse(TEXT("minecraft:oak_log[axis=z]"))));
	TestEqual(TEXT("and stand as they were"), Turned(TEXT("minecraft:oak_log[axis=y]"), EMcRotation::Clockwise90), Describe(Parse(TEXT("minecraft:oak_log[axis=y]"))));
	TestEqual(TEXT("a sign turns four steps a quarter"), Turned(TEXT("minecraft:oak_sign[rotation=2]"), EMcRotation::Clockwise90), Describe(Parse(TEXT("minecraft:oak_sign[rotation=6]"))));
	TestEqual(TEXT("a sign mirrored left-right"), Turned(TEXT("minecraft:oak_sign[rotation=2]"), EMcRotation::None, EMcMirror::LeftRight), Describe(Parse(TEXT("minecraft:oak_sign[rotation=6]"))));
	TestEqual(TEXT("a fence's arms turn"), Turned(TEXT("minecraft:oak_fence[north=true]"), EMcRotation::Clockwise90), Describe(Parse(TEXT("minecraft:oak_fence[east=true]"))));
	TestEqual(TEXT("rails turn"), Turned(TEXT("minecraft:rail[shape=north_south]"), EMcRotation::Clockwise90), Describe(Parse(TEXT("minecraft:rail[shape=east_west]"))));
	TestEqual(TEXT("sloping rails turn"), Turned(TEXT("minecraft:rail[shape=ascending_north]"), EMcRotation::Clockwise90), Describe(Parse(TEXT("minecraft:rail[shape=ascending_east]"))));
	TestEqual(TEXT("curved rails turn"), Turned(TEXT("minecraft:rail[shape=south_east]"), EMcRotation::Clockwise90), Describe(Parse(TEXT("minecraft:rail[shape=south_west]"))));
	TestEqual(TEXT("a mirror swaps a door's hinge"), Turned(TEXT("minecraft:oak_door[facing=north,hinge=left]"), EMcRotation::None, EMcMirror::FrontBack), Describe(Parse(TEXT("minecraft:oak_door[facing=north,hinge=right]"))));
	TestEqual(TEXT("and turns its facing"), Turned(TEXT("minecraft:oak_door[facing=east,hinge=left]"), EMcRotation::None, EMcMirror::FrontBack), Describe(Parse(TEXT("minecraft:oak_door[facing=west,hinge=right]"))));
	TestEqual(TEXT("a mirror swaps a stair's corner"), Turned(TEXT("minecraft:oak_stairs[facing=north,shape=inner_left]"), EMcRotation::None, EMcMirror::LeftRight), Describe(Parse(TEXT("minecraft:oak_stairs[facing=south,shape=inner_right]"))));
	TestEqual(TEXT("stone has no direction"), Turned(TEXT("minecraft:stone"), EMcRotation::Clockwise90), Describe(Parse(TEXT("minecraft:stone"))));

	// ---- A known template put down turned: the trapdoor of the igloo's top, five blocks south of its shaft ----
	const TSharedPtr<const FMcStructureTemplate> IglooTop = FMcStructureTemplate::Find(TEXT("minecraft:igloo/top"));
	if (TestTrue(TEXT("igloo top loads"), IglooTop.IsValid()))
	{
		FPatch Patch(Settings, false, FMcBiomeRegistry::Get().GetPlains());
		FMcGenRegion Region(FIntPoint(1, 1), Patch.Around);
		FMcRandom Random(3);
		FMcTemplatePlaceSettings PlaceSettings;
		const FIntVector Origin(24, 70, 24);
		FMcStateId Original;
		const TOptional<FIntVector> Untouched = FindBlock(*IglooTop, TEXT("minecraft:birch_trapdoor"), Origin, PlaceSettings, &Original);
		if (!Untouched)
		{
			// The igloo's hatch is an oak trapdoor in this jar if it isn't a birch one.
			AddInfo(TEXT("no birch trapdoor in the igloo"));
		}
		const TOptional<FIntVector> Door = FindBlock(*IglooTop, TEXT("minecraft:oak_trapdoor"), Origin, PlaceSettings, &Original);
		if (TestTrue(TEXT("the igloo has a trapdoor"), Door.IsSet() || Untouched.IsSet()))
		{
			const TCHAR* const Name = Door ? TEXT("minecraft:oak_trapdoor") : TEXT("minecraft:birch_trapdoor");
			for (const EMcRotation Rotation : { EMcRotation::None, EMcRotation::Clockwise90, EMcRotation::Clockwise180, EMcRotation::CounterClockwise90 })
			{
				FPatch Fresh(Settings, false, FMcBiomeRegistry::Get().GetPlains());
				FMcGenRegion FreshRegion(FIntPoint(1, 1), Fresh.Around);
				PlaceSettings.Rotation = Rotation;
				PlaceSettings.Pivot = FIntVector::ZeroValue;
				IglooTop->Place(FreshRegion, Origin, PlaceSettings, Random);
				const TOptional<FIntVector> Expected = FindBlock(*IglooTop, Name, Origin, PlaceSettings, &Original);
				const FMcStateId Placed = FreshRegion.GetBlock(*Expected);
				TestEqual(FString::Printf(TEXT("the trapdoor is where %s puts it"), McTemplate::Name(Rotation)), Describe(Placed), Describe(McTemplate::TransformState(Original, Rotation, EMcMirror::None)));
				// Turned round the origin, a block at local (x, z) lands at the turned offset.
				const FIntVector Local = Expected ? (*Expected - Origin) : FIntVector::ZeroValue;
				TestTrue(FString::Printf(TEXT("%s is inside the template's turned box"), McTemplate::Name(Rotation)), FMath::Abs(Local.X) < 9 && FMath::Abs(Local.Z) < 9);
			}
		}
	}

	// ---- The template and fossil features ----
	{
		FMcFeatureLibrary Library(Settings.SeaLevel, Settings.MinY, Settings.Height);
		TestTrue(TEXT("biome features load"), Library.LoadBiomes());
		TestFalse(TEXT("no feature errors"), Library.HasErrors());
		TestFalse(TEXT("template features are supported"), Library.GetUnsupported().Contains(TEXT("minecraft:template")));
		TestFalse(TEXT("fossil features are supported"), Library.GetUnsupported().Contains(TEXT("minecraft:fossil")));
		for (const TCHAR* Id : { TEXT("minecraft:desert_well"), TEXT("minecraft:sulfur_spring"), TEXT("minecraft:fossil_coal"), TEXT("minecraft:fossil_diamonds") })
		{
			const TSharedPtr<const FMcFeature> Feature = Library.ParseFeature(MakeShared<FJsonValueString>(Id));
			if (!TestTrue(FString::Printf(TEXT("%s parses"), Id), Feature.IsValid()))
			{
				continue;
			}
			// Stone with a pocket of air in it for the fossils; sand for the well.
			FPatch Patch(Settings, false, FMcBiomeRegistry::Get().GetPlains());
			const FMcStateId Stone = Parse(TEXT("minecraft:stone"));
			const FMcStateId Sand = Parse(TEXT("minecraft:sand"));
			const bool bFossil = FString(Id).Contains(TEXT("fossil"));
			for (const TUniquePtr<FMcChunk>& Chunk : Patch.Chunks)
			{
				for (int32 Z = 0; Z < 16; ++Z)
				{
					for (int32 X = 0; X < 16; ++X)
					{
						for (int32 Y = bFossil ? 0 : 60; Y < 64; ++Y)
						{
							Chunk->SetBlock(X, Y, Z, bFossil ? Stone : Sand);
						}
					}
				}
			}
			FMcGenRegion Region(FIntPoint(1, 1), Patch.Around);
			FMcRandom Random(5);
			FMcFeatureContext Context{ Region, Random, Library, Settings.SeaLevel };
			const TArray<FMcStateId> Before = Snapshot(*Patch.Chunks[4]);
			bool bPlaced = false;
			for (int32 Try = 0; Try < 20 && !bPlaced; ++Try)
			{
				bPlaced = Feature->Place(Context, FIntVector(24, bFossil ? 20 : 64, 24));
			}
			FChange Change;
			Diff(*Patch.Chunks[4], Before, Change);
			TestTrue(FString::Printf(TEXT("%s places"), Id), bPlaced && Change.Changed > 0);
			AddInfo(FString::Printf(TEXT("%s: %d blocks"), Id, Change.Changed));
			if (FString(Id).Contains(TEXT("fossil_coal")))
			{
				TestTrue(TEXT("a coal fossil has bone blocks"), Change.Names.Contains(TEXT("minecraft:bone_block")));
			}
		}
	}

	// ---- The structure sets ----
	FMcStructureLibrary Structures(Settings.SeaLevel, Settings.MinY);
	if (!TestTrue(TEXT("structure sets load"), Structures.Load()))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("%d structure sets, %d structures"), Structures.NumSets(), Structures.NumStructures()));
	TMap<FName, int32> Counts;
	for (int32 Z = -100; Z < 100; ++Z)
	{
		for (int32 X = -100; X < 100; ++X)
		{
			TArray<FName> Here;
			Structures.GetCandidates(12345, FIntPoint(X, Z), Here);
			for (const FName Name : Here)
			{
				++Counts.FindOrAdd(Name);
			}
		}
	}
	// 40000 chunks: one igloo cell per 1024, one buried treasure per hundred chunks.
	int32 Igloos = Counts.FindRef(TEXT("minecraft:igloo"));
	TestTrue(TEXT("about 39 igloo starts in 200 x 200 chunks"), Igloos > 20 && Igloos < 60);
	const int32 Treasures = Counts.FindRef(TEXT("minecraft:buried_treasure"));
	TestTrue(TEXT("about 400 buried treasure starts"), Treasures > 250 && Treasures < 560);
	// The same chunk always gives the same answer.
	TArray<FName> First;
	TArray<FName> Second;
	for (int32 Index = 0; Index < 4000; ++Index)
	{
		Structures.GetCandidates(9, FIntPoint(Index % 64, Index / 64), First);
		Structures.GetCandidates(9, FIntPoint(Index % 64, Index / 64), Second);
	}
	TestTrue(TEXT("starts don't change"), First == Second);
	// Starts keep their distance: no two of one set closer than the separation, none further than the spacing allows.
	for (int32 Z = -60; Z < 60; ++Z)
	{
		for (int32 X = -60; X < 60; ++X)
		{
			TArray<FName> Here;
			Structures.GetCandidates(1, FIntPoint(X, Z), Here);
			if (Here.Contains(TEXT("minecraft:igloo")))
			{
				TArray<FName> Beside;
				for (int32 DZ = -7; DZ <= 7; ++DZ)
				{
					for (int32 DX = -7; DX <= 7; ++DX)
					{
						if (DX != 0 || DZ != 0)
						{
							Structures.GetCandidates(1, FIntPoint(X + DX, Z + DZ), Beside);
						}
					}
				}
				TestFalse(TEXT("igloos are at least 8 chunks apart"), Beside.Contains(TEXT("minecraft:igloo")));
			}
		}
	}

	// ---- Each structure built on flat ground, in the centre chunk ----
	struct FCase
	{
		const TCHAR* Id;
		bool bSea;
		int32 MinChanged;
		const TCHAR* Marker;
	};
	const FCase Cases[] = {
		{ TEXT("minecraft:igloo"), false, 30, TEXT("minecraft:snow_block") },
		{ TEXT("minecraft:desert_pyramid"), false, 800, TEXT("minecraft:chiseled_sandstone") },
		{ TEXT("minecraft:jungle_pyramid"), false, 800, TEXT("minecraft:mossy_cobblestone") },
		{ TEXT("minecraft:swamp_hut"), false, 60, TEXT("minecraft:spruce_planks") },
		{ TEXT("minecraft:buried_treasure"), false, 0, TEXT("minecraft:chest") },
		{ TEXT("minecraft:ruined_portal"), false, 20, TEXT("minecraft:obsidian") },
		{ TEXT("minecraft:shipwreck"), true, 30, nullptr },
		{ TEXT("minecraft:shipwreck_beached"), false, 30, nullptr },
		{ TEXT("minecraft:ocean_ruin_cold"), true, 5, nullptr },
		{ TEXT("minecraft:ocean_ruin_warm"), true, 5, nullptr },
	};
	for (const FCase& Case : Cases)
	{
		int32 BestChanged = 0;
		bool bMarker = Case.Marker == nullptr;
		for (const int32 Seed : { 1, 2, 3, 4, 5, 6 })
		{
			FPatch Patch(Settings, Case.bSea, FMcBiomeRegistry::Get().GetPlains());
			FMcGenRegion Region(FIntPoint(1, 1), Patch.Around);
			const TArray<FMcStateId> Before = Snapshot(*Patch.Chunks[4]);
			if (!TestTrue(FString::Printf(TEXT("%s is built"), Case.Id), Structures.PlaceStructure(Region, Case.Id, Seed, FIntPoint(1, 1))))
			{
				break;
			}
			FChange Change;
			Diff(*Patch.Chunks[4], Before, Change);
			BestChanged = FMath::Max(BestChanged, Change.Changed);
			bMarker = bMarker || Change.Names.Contains(Case.Marker);
		}
		TestTrue(FString::Printf(TEXT("%s changes blocks (best %d)"), Case.Id, BestChanged), BestChanged >= FMath::Max(1, Case.MinChanged) || Case.MinChanged == 0);
		TestTrue(FString::Printf(TEXT("%s puts down its marker block"), Case.Id), bMarker);
		AddInfo(FString::Printf(TEXT("%s: %d blocks in the centre chunk"), Case.Id, BestChanged));
	}

	// ---- Structures in generated worlds ----
	// For a few seeds, find chunks where a set starts a structure whose biome fits (checked on the biome source
	// alone), generate a patch round it and decorate: blocks must appear in the centre chunk's neighbourhood.
	struct FWanted
	{
		const TCHAR* Prefix;
		bool bRequired;
		bool bFound = false;
	};
	FWanted Wanted[] = { { TEXT("minecraft:igloo"), true }, { TEXT("minecraft:desert_pyramid"), true }, { TEXT("minecraft:ruined_portal"), true },
		{ TEXT("minecraft:swamp_hut"), false }, { TEXT("minecraft:jungle_pyramid"), false }, { TEXT("minecraft:shipwreck"), false },
		{ TEXT("minecraft:ocean_ruin"), false }, { TEXT("minecraft:buried_treasure"), false } };
	for (const int32 Seed : { 11, 22, 33, 44, 55, 66 })
	{
		const FMcNoiseGenerator Generator(Settings, Seed, TEXT("minecraft:overworld"));
		for (FWanted& Want : Wanted)
		{
			if (Want.bFound)
			{
				continue;
			}
			// Candidates near the origin first.
			TArray<TPair<int64, FIntPoint>> Found;
			for (int32 Z = -200; Z <= 200; ++Z)
			{
				for (int32 X = -200; X <= 200; ++X)
				{
					TArray<FName> Here;
					Structures.GetCandidates(Seed, FIntPoint(X, Z), Here);
					for (const FName Name : Here)
					{
						if (Name.ToString().StartsWith(Want.Prefix)
							&& Structures.BiomeAllows(Name, Generator.GetBiome(X * 16 + 8, Settings.SeaLevel + 4, Z * 16 + 8)))
						{
							Found.Emplace(static_cast<int64>(X) * X + static_cast<int64>(Z) * Z, FIntPoint(X, Z));
						}
					}
				}
			}
			Found.Sort([](const TPair<int64, FIntPoint>& A, const TPair<int64, FIntPoint>& B) { return A.Key < B.Key; });
			for (int32 Try = 0; Try < FMath::Min(Found.Num(), Want.bRequired ? 4 : 2) && !Want.bFound; ++Try)
			{
				const FIntPoint Start = Found[Try].Value;
				constexpr int32 Size = 5;
				const FIntPoint Corner = Start - FIntPoint(2, 2);
				TArray<TUniquePtr<FMcChunk>> Chunks;
				for (int32 Index = 0; Index < Size * Size; ++Index)
				{
					Chunks.Add(MakeUnique<FMcChunk>(Corner + FIntPoint(Index % Size, Index / Size), Settings.MinY, Settings.Height / 16));
					Generator.Generate(*Chunks.Last());
				}
				TArray<TArray<FMcStateId>> Before;
				for (const int32 Index : { 6, 7, 8, 11, 12, 13, 16, 17, 18 })
				{
					Before.Add(Snapshot(*Chunks[Index]));
				}
				int32 Slot = 0;
				FChange Change;
				for (const int32 Index : { 6, 7, 8, 11, 12, 13, 16, 17, 18 })
				{
					const int32 CX = Index % Size;
					const int32 CZ = Index / Size;
					FMcChunk* Around[9];
					for (int32 DZ = -1; DZ <= 1; ++DZ)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							Around[(DZ + 1) * 3 + DX + 1] = Chunks[(CZ + DZ) * Size + CX + DX].Get();
						}
					}
					FMcGenRegion Region(Corner + FIntPoint(CX, CZ), Around);
					Structures.Place(Region, Seed);
					Diff(*Chunks[Index], Before[Slot++], Change);
				}
				if (Change.Changed > 0)
				{
					Want.bFound = true;
					FMcChunk* Middle[9];
					for (int32 Slot9 = 0; Slot9 < 9; ++Slot9)
					{
						Middle[Slot9] = Chunks[(1 + Slot9 / 3) * Size + 1 + Slot9 % 3].Get();
					}
					FMcGenRegion MiddleRegion(Start, Middle);
					AddInfo(FString::Printf(TEXT("seed %d: %s starts in chunk (%d, %d): %d blocks, ground at y %d (x %d, z %d)"), Seed, Want.Prefix, Start.X, Start.Y, Change.Changed,
						MiddleRegion.GetHeight(EMcHeightmap::OceanFloor, Start.X * 16 + 8, Start.Y * 16 + 8), Start.X * 16 + 8, Start.Y * 16 + 8));
				}
			}
		}
	}
	for (const FWanted& Want : Wanted)
	{
		if (Want.bRequired)
		{
			TestTrue(FString::Printf(TEXT("%s turns up in a generated world"), Want.Prefix), Want.bFound);
		}
	}
	return true;
}

#endif
