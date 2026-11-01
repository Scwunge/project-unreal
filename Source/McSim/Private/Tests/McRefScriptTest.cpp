#include "HAL/FileManager.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Replays the vanilla reference timing scripts (tools/refscripts/rs_*.txt, the files E:\mc-reference runs) in our
// simulation: setblock, fill and "tick step" lines are carried out, and each "log states" line prints the box's
// states as an "mcours states:" line. tools/refscripts/diff.py compares that with the reference's "mcref states:".
// The scripts' vanilla ground block is at y -61; ours is at y 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcRefScriptTest, "McClient.McSim.RefScripts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcRefScriptTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("tools/refscripts"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("rs_*.txt")), true, false);
	TestTrue(TEXT("scripts found"), Files.Num() > 0);
	constexpr int32 Lift = 61;
	for (const FString& File : Files)
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Dir, File));
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 Z = -1; Z <= 1; ++Z) for (int32 X = -1; X <= 9; ++X) World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		FMcSimulation Sim(World);
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
				for (int32 Index = 0; Index < Block.Properties.Num(); ++Index) Parts.Add(Block.Properties[Index].Name.ToString() + TEXT("=") + Block.Properties[Index].Values[Info.Values[Index]].ToString());
				Parts.Sort();
				Out += TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
			}
			return Out;
		};
		const auto Fill = [&](const TArray<FString>& P, int32 First, const FString& Block)
		{
			const FMcStateId State = Parse(Block);
			const FIntVector A(FCString::Atoi(*P[First]), FCString::Atoi(*P[First + 1]) + Lift, FCString::Atoi(*P[First + 2]));
			const FIntVector B(FCString::Atoi(*P[First + 3]), FCString::Atoi(*P[First + 4]) + Lift, FCString::Atoi(*P[First + 5]));
			for (int32 X = FMath::Min(A.X, B.X); X <= FMath::Max(A.X, B.X); ++X)
				for (int32 Y = FMath::Min(A.Y, B.Y); Y <= FMath::Max(A.Y, B.Y); ++Y)
					for (int32 Z = FMath::Min(A.Z, B.Z); Z <= FMath::Max(A.Z, B.Z); ++Z)
						World.SetBlock(FIntVector(X, Y, Z), State);
		};
		for (int32 X = -16; X < 144; ++X) for (int32 Z = -16; Z < 32; ++Z) World.SetBlock(FIntVector(X, 0, Z), Parse(TEXT("stone")));
		UE_LOG(LogTemp, Display, TEXT("mcours script %s"), *File);
		// The changes between one "log states" and the next, as "sample|x,y,z|from|to": what the vanilla run's
		// expect/ file holds, and what ours must equal.
		TMap<FString, FString> Previous;
		TArray<FString> Changes;
		int32 Sample = -1;
		// Things that run when the game time divides by 20 (daylight detectors) need the same game time modulo 20 at the
		// first sample as the vanilla run had; its expect file says so in a "#vt N" line.
		TArray<FString> Expected;
		const bool bHasExpected = FFileHelper::LoadFileToStringArray(Expected, *FPaths::Combine(Dir, TEXT("expect"), File));
		for (const FString& Line : Expected)
		{
			if (Line.StartsWith(TEXT("#vt ")))
			{
				int32 Before = 0;
				for (const FString& Raw : Lines)
				{
					TArray<FString> Q;
					Raw.TrimStartAndEnd().ParseIntoArray(Q, TEXT(" "));
					if (Q.Num() >= 2 && Q[0] == TEXT("log") && Q[1] == TEXT("states")) break;
					if (Q.Num() >= 4 && Q[0] == TEXT("cmd") && Q[1] == TEXT("tick") && Q[2] == TEXT("step")) Before += FCString::Atoi(*Q[3]);
				}
				const int32 Pre = ((FCString::Atoi(*Line.Mid(4)) - Before) % 20 + 20) % 20;
				for (int32 Index = 0; Index < Pre; ++Index) Sim.Tick();
			}
		}
		FString Out;
		TMap<FString, FString> Current;
		bool bOpen = false;
		TSet<FString> GroupBoxes;
		const auto Finish = [&]()
		{
			if (!bOpen) return;
			bOpen = false;
			GroupBoxes.Reset();
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
			Current.Reset();
			Out.Reset();
		};
		for (const FString& Raw : Lines)
		{
			TArray<FString> P;
			Raw.TrimStartAndEnd().ParseIntoArray(P, TEXT(" "));
			if (P.Num() >= 6 && P[0] == TEXT("cmd") && P[1] == TEXT("setblock"))
			{
				World.SetBlock(FIntVector(FCString::Atoi(*P[2]), FCString::Atoi(*P[3]) + Lift, FCString::Atoi(*P[4])), Parse(P[5]));
			}
			else if (P.Num() >= 9 && P[0] == TEXT("cmd") && P[1] == TEXT("fill"))
			{
				Fill(P, 2, P[8]);
			}
			else if (P.Num() == 4 && P[0] == TEXT("press"))
			{
				Sim.UseBlock(FIntVector(FCString::Atoi(*P[1]), FCString::Atoi(*P[2]) + Lift, FCString::Atoi(*P[3])), 0.0f);
			}
			else if (P.Num() >= 3 && P[0] == TEXT("cmd") && P[1] == TEXT("weather"))
			{
				Sim.SetWeather(P[2] == TEXT("thunder") ? EMcWeather::Thunder : P[2] == TEXT("rain") ? EMcWeather::Rain : EMcWeather::Clear, P.Num() > 3 ? FCString::Atoi(*P[3]) : 0);
			}
			else if (P.Num() == 4 && P[0] == TEXT("cmd") && P[1] == TEXT("time") && P[2] == TEXT("set"))
			{
				Sim.SetDayTime(FCString::Atoi64(*P[3]));
			}
			else if (P.Num() >= 4 && P[0] == TEXT("cmd") && P[1] == TEXT("tick") && P[2] == TEXT("step"))
			{
				Finish();
				for (int32 Index = 0; Index < FCString::Atoi(*P[3]); ++Index) Sim.Tick();
			}
			else if (P.Num() == 8 && P[0] == TEXT("log") && P[1] == TEXT("states"))
			{
				// Several different boxes logged without a tick between them are one sample; the same box twice is two.
				const FString Box = FString::Join(P, TEXT(" "));
				if (GroupBoxes.Contains(Box)) Finish();
				GroupBoxes.Add(Box);
				bOpen = true;
				for (int32 Z = FCString::Atoi(*P[4]); Z <= FCString::Atoi(*P[7]); ++Z)
					for (int32 Y = FCString::Atoi(*P[3]); Y <= FCString::Atoi(*P[6]); ++Y)
						for (int32 X = FCString::Atoi(*P[2]); X <= FCString::Atoi(*P[5]); ++X)
						{
							const FMcStateId State = World.GetBlock(FIntVector(X, Y + Lift, Z));
							if (!Registry.IsAir(State)) { Out += FString::Printf(TEXT("%d,%d,%d=%s;"), X, Y, Z, *Describe(State)); Current.Add(FString::Printf(TEXT("%d,%d,%d"), X, Y, Z), Describe(State)); }
						}
			}
		}
		Finish();
		// Held to the vanilla run, when one is stored.
		if (bHasExpected)
		{
			Expected.RemoveAll([](const FString& Line) { return Line.IsEmpty() || Line.StartsWith(TEXT("#")); });
			Expected.Sort();
			Changes.Sort();
			TestEqual(*FString::Printf(TEXT("%s: number of changes"), *File), Changes.Num(), Expected.Num());
			int32 Shown = 0;
			for (const FString& Line : Expected) if (!Changes.Contains(Line) && Shown++ < 8) AddError(FString::Printf(TEXT("%s: vanilla has %s"), *File, *Line));
			for (const FString& Line : Changes) if (!Expected.Contains(Line) && Shown++ < 16) AddError(FString::Printf(TEXT("%s: ours has %s"), *File, *Line));
		}
	}
	return true;
}

#endif
