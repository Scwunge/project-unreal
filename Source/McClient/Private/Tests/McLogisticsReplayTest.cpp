#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockUse.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McBrewingStand.h"
#include "McEntityHost.h"
#include "McFurnace.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "McWorldActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Replays tools/refscripts/lg_*.txt (hoppers and droppers, whose contents live in the block-use subsystem) in a world actor, one
// game tick per "cmd tick step 1": the simulation, then the hoppers. Holds the block states each tick to expect/lg_*.txt, the
// changes the reference client showed (same format as McSim.RefScripts). The y of the script is lifted by 61.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLogisticsReplayTest, "McClient.McSim.RefScriptsLogistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcLogisticsReplayTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded()) || !GEngine)
	{
		return false;
	}
	const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("tools/refscripts"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("lg_*.txt")), true, false);
	constexpr int32 Lift = 61;
	for (const FString& File : Files)
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Dir, File));
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		if (!TestNotNull(TEXT("a world"), World))
		{
			return false;
		}
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		AMcWorldActor* Actor = World->SpawnActor<AMcWorldActor>();
		if (Actor && !Actor->HasActorBegunPlay())
		{
			Actor->DispatchBeginPlay();
		}
		UMcBlockUseSubsystem* Use = World->GetSubsystem<UMcBlockUseSubsystem>();
		const auto Finish = [&]()
		{
			if (Actor)
			{
				Actor->Destroy();
			}
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		};
		if (!TestNotNull(TEXT("a world actor"), Actor) || !TestNotNull(TEXT("the block use subsystem"), Use) || !Actor->GetBlockWorld() || !Actor->GetSimulation())
		{
			Finish();
			return false;
		}
		FMcBlockWorld& Blocks = *Actor->GetBlockWorld();
		FMcSimulation& Sim = *Actor->GetSimulation();
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 X = -1; X <= 5; ++X)
			{
				if (!Blocks.HasChunk(FIntPoint(X, Z)))
				{
					Blocks.AddChunk(Blocks.MakeChunk(FIntPoint(X, Z)));
				}
			}
		}
		Use->Tick(0.0f);
		const FMcStateId Air = Registry.ParseState(TEXT("minecraft:air")).Get(0);
		const auto Parse = [&](const FString& Text) { return Registry.ParseState(*(TEXT("minecraft:") + Text)).Get(Air); };
		const auto Describe = [&](FMcStateId State)
		{
			const FMcBlock& Block = Registry.GetBlockOf(State);
			const FMcBlockState& Info = Registry.GetState(State);
			FString Out = Block.Name.ToString().Replace(TEXT("minecraft:"), TEXT(""));
			if (Block.Properties.Num() > 0)
			{
				TArray<FString> Parts;
				for (int32 Index = 0; Index < Block.Properties.Num(); ++Index)
				{
					Parts.Add(Block.Properties[Index].Name.ToString() + TEXT("=") + Block.Properties[Index].Values[Info.Values[Index]].ToString());
				}
				Parts.Sort();
				Out += TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
			}
			return Out;
		};
		// "block[props]{Items:[{Slot:0,id:"minecraft:x",count:3},...]}"
		const auto Place = [&](const FIntVector& Pos, const FString& Text)
		{
			FString Block = Text, Nbt;
			int32 Brace = INDEX_NONE;
			if (Text.FindChar('{', Brace))
			{
				Block = Text.Left(Brace);
				Nbt = Text.Mid(Brace);
			}
			Blocks.SetBlock(Pos, Parse(Block));
			if (Nbt.IsEmpty())
			{
				return;
			}
			int32 Bracket = INDEX_NONE;
			const FString Name = Block.FindChar('[', Bracket) ? Block.Left(Bracket) : Block;
			const bool bFurnace = Name == TEXT("furnace") || Name == TEXT("blast_furnace") || Name == TEXT("smoker");
			const bool bStand = Name == TEXT("brewing_stand");
			TSharedPtr<FMcMachine> Machine;
			if (bFurnace)
			{
				Machine = MakeShared<FMcFurnace>(Name == TEXT("furnace") ? EMcFurnaceKind::Furnace : Name == TEXT("smoker") ? EMcFurnaceKind::Smoker : EMcFurnaceKind::BlastFurnace);
			}
			else if (bStand)
			{
				Machine = MakeShared<FMcBrewingStand>();
			}
			const int32 NumSlots = Machine ? Machine->GetItems()->GetAll().Num() : Name == TEXT("hopper") ? 5 : Name == TEXT("chest") ? 27 : 9;
			FMcBlockEntity Entity = FMcBlockEntity::MakeContainer(NumSlots);
			int32 From = 0;
			while ((From = Nbt.Find(TEXT("Slot:"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From)) != INDEX_NONE)
			{
				const int32 Slot = FCString::Atoi(*Nbt.Mid(From + 5));
				const int32 IdAt = Nbt.Find(TEXT("id:\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, From) + 4;
				const FString Id = Nbt.Mid(IdAt, Nbt.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, IdAt) - IdAt);
				const int32 CountAt = Nbt.Find(TEXT("count:"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From) + 6;
				if (Slot >= 0 && Slot < NumSlots)
				{
					if (Machine)
					{
						Machine->GetItems()->Set(Slot, FMcItemStack(*Id, FCString::Atoi(*Nbt.Mid(CountAt))));
					}
					else
					{
						Entity.Slots[Slot] = FMcItemStack(*Id, FCString::Atoi(*Nbt.Mid(CountAt)));
					}
				}
				From = CountAt;
			}
			if (Machine)
			{
				Entity = FMcBlockEntity();
				Entity.Kind = bFurnace ? EMcBlockEntityKind::Furnace : EMcBlockEntityKind::BrewingStand;
				FMemoryWriter Writer(Entity.Data);
				Machine->Serialize(Writer);
			}
			if (FMcChunk* Chunk = Blocks.FindChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)))
			{
				Chunk->SetBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15, Entity);
			}
		};
		TMap<FString, FString> Previous, Current, PreviousItems;
		TArray<FString> Changes;
		int32 Sample = -1;
		TArray<FString> Wanted;
		const bool bHasExpected = FFileHelper::LoadFileToStringArray(Wanted, *FPaths::Combine(Dir, TEXT("expect"), File));
		UE_LOG(LogTemp, Display, TEXT("mcours script %s"), *File);
		for (const FString& Raw : Lines)
		{
			TArray<FString> P;
			Raw.TrimStartAndEnd().ParseIntoArray(P, TEXT(" "));
			if (P.Num() >= 6 && P[0] == TEXT("cmd") && P[1] == TEXT("setblock"))
			{
				Place(FIntVector(FCString::Atoi(*P[2]), FCString::Atoi(*P[3]) + Lift, FCString::Atoi(*P[4])), P[5]);
			}
			else if (P.Num() >= 9 && P[0] == TEXT("cmd") && P[1] == TEXT("fill"))
			{
				const FMcStateId State = Parse(P[8]);
				const FIntVector A(FCString::Atoi(*P[2]), FCString::Atoi(*P[3]) + Lift, FCString::Atoi(*P[4]));
				const FIntVector B(FCString::Atoi(*P[5]), FCString::Atoi(*P[6]) + Lift, FCString::Atoi(*P[7]));
				for (int32 X = FMath::Min(A.X, B.X); X <= FMath::Max(A.X, B.X); ++X)
				{
					for (int32 Y = FMath::Min(A.Y, B.Y); Y <= FMath::Max(A.Y, B.Y); ++Y)
					{
						for (int32 Z = FMath::Min(A.Z, B.Z); Z <= FMath::Max(A.Z, B.Z); ++Z)
						{
							Blocks.SetBlock(FIntVector(X, Y, Z), State);
						}
					}
				}
			}
			else if (P.Num() >= 4 && P[0] == TEXT("cmd") && P[1] == TEXT("tick") && P[2] == TEXT("step"))
			{
				for (int32 Index = 0; Index < FCString::Atoi(*P[3]); ++Index)
				{
					Sim.Tick();
					Use->Tick(0.05f + 1e-4f);
				}
			}
			else if (P.Num() == 8 && P[0] == TEXT("log") && P[1] == TEXT("states"))
			{
				Current.Reset();
				FString Out;
				for (int32 Z = FCString::Atoi(*P[4]); Z <= FCString::Atoi(*P[7]); ++Z)
				{
					for (int32 Y = FCString::Atoi(*P[3]); Y <= FCString::Atoi(*P[6]); ++Y)
					{
						for (int32 X = FCString::Atoi(*P[2]); X <= FCString::Atoi(*P[5]); ++X)
						{
							const FMcStateId State = Blocks.GetBlock(FIntVector(X, Y + Lift, Z));
							if (!Registry.IsAir(State))
							{
								Out += FString::Printf(TEXT("%d,%d,%d=%s;"), X, Y, Z, *Describe(State));
								Current.Add(FString::Printf(TEXT("%d,%d,%d"), X, Y, Z), Describe(State));
							}
						}
					}
				}
				if (++Sample > 0)
				{
					TSet<FString> Keys;
					for (const TPair<FString, FString>& Pair : Previous) Keys.Add(Pair.Key);
					for (const TPair<FString, FString>& Pair : Current) Keys.Add(Pair.Key);
					for (const FString& Key : Keys)
					{
						const FString* Old = Previous.Find(Key);
						const FString* New = Current.Find(Key);
						if ((Old ? *Old : FString()) != (New ? *New : FString()))
						{
							Changes.Add(FString::Printf(TEXT("%d|%s|%s|%s"), Sample, *Key, Old ? **Old : TEXT("-"), New ? **New : TEXT("-")));
						}
					}
				}
				Previous = Current;
				UE_LOG(LogTemp, Display, TEXT("mcours states: %s"), *Out);
			}
			else if (P.Num() == 8 && P[0] == TEXT("log") && P[1] == TEXT("items"))
			{
				// Container contents per cell ("cobblestone=1,stick=2", sorted), diffed like the states; keys start "i:".
				TMap<FString, FString> Items;
				for (int32 Z = FCString::Atoi(*P[4]); Z <= FCString::Atoi(*P[7]); ++Z)
				{
					for (int32 Y = FCString::Atoi(*P[3]); Y <= FCString::Atoi(*P[6]); ++Y)
					{
						for (int32 X = FCString::Atoi(*P[2]); X <= FCString::Atoi(*P[5]); ++X)
						{
							const FMcChunk* Chunk = Blocks.FindChunk(FIntPoint(X >> 4, Z >> 4));
							const FMcBlockEntity* Entity = Chunk ? Chunk->FindBlockEntity(X & 15, Y + Lift, Z & 15) : nullptr;
							if (!Entity)
							{
								continue;
							}
							TMap<FString, int32> Counts;
							for (const FMcItemStack& Stack : Entity->Slots)
							{
								if (Stack.Count > 0)
								{
									Counts.FindOrAdd(Stack.Item.ToString().Replace(TEXT("minecraft:"), TEXT(""))) += Stack.Count;
								}
							}
							Counts.KeySort([](const FString& A, const FString& B) { return A < B; });
							TArray<FString> Parts;
							for (const TPair<FString, int32>& Pair : Counts) Parts.Add(FString::Printf(TEXT("%s=%d"), *Pair.Key, Pair.Value));
							if (Parts.Num() > 0)
							{
								Items.Add(FString::Printf(TEXT("i:%d,%d,%d"), X, Y, Z), FString::Join(Parts, TEXT(",")));
							}
						}
					}
				}
				TSet<FString> Keys;
				for (const TPair<FString, FString>& Pair : PreviousItems) Keys.Add(Pair.Key);
				for (const TPair<FString, FString>& Pair : Items) Keys.Add(Pair.Key);
				for (const FString& Key : Keys)
				{
					const FString* Old = PreviousItems.Find(Key);
					const FString* New = Items.Find(Key);
					if (Sample > 0 && (Old ? *Old : FString()) != (New ? *New : FString()))
					{
						Changes.Add(FString::Printf(TEXT("%d|%s|%s|%s"), Sample, *Key, Old ? **Old : TEXT("-"), New ? **New : TEXT("-")));
					}
				}
				PreviousItems = Items;
				UE_LOG(LogTemp, Display, TEXT("mcours items: %d"), Items.Num());
			}
		}
		Finish();
		for (const FString& Line : Changes) UE_LOG(LogTemp, Display, TEXT("mcours change %s"), *Line);
		if (bHasExpected)
		{
			Wanted.RemoveAll([](const FString& Line) { return Line.IsEmpty() || Line.StartsWith(TEXT("#")); });
			Wanted.Sort();
			Changes.Sort();
			TestEqual(*FString::Printf(TEXT("%s: number of changes"), *File), Changes.Num(), Wanted.Num());
			int32 Shown = 0;
			for (const FString& Line : Wanted) if (!Changes.Contains(Line) && Shown++ < 8) AddError(FString::Printf(TEXT("%s: vanilla has %s"), *File, *Line));
			for (const FString& Line : Changes) if (!Wanted.Contains(Line) && Shown++ < 16) AddError(FString::Printf(TEXT("%s: ours has %s"), *File, *Line));
		}
	}
	return true;
}

#endif
