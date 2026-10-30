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
		for (int32 Z = -1; Z <= 1; ++Z) for (int32 X = -1; X <= 1; ++X) World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		FMcSimulation Sim(World);
		Sim.SetSunLight(15);
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
		for (int32 X = -16; X < 32; ++X) for (int32 Z = -16; Z < 32; ++Z) World.SetBlock(FIntVector(X, 0, Z), Parse(TEXT("stone")));
		UE_LOG(LogTemp, Display, TEXT("mcours script %s"), *File);
		bool bFrozen = false;
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
			else if (P.Num() >= 4 && P[0] == TEXT("cmd") && P[1] == TEXT("tick") && P[2] == TEXT("step"))
			{
				for (int32 Index = 0; Index < FCString::Atoi(*P[3]); ++Index) Sim.Tick();
			}
			else if (P.Num() == 8 && P[0] == TEXT("log") && P[1] == TEXT("states"))
			{
				FString Out;
				for (int32 Z = FCString::Atoi(*P[4]); Z <= FCString::Atoi(*P[7]); ++Z)
					for (int32 Y = FCString::Atoi(*P[3]); Y <= FCString::Atoi(*P[6]); ++Y)
						for (int32 X = FCString::Atoi(*P[2]); X <= FCString::Atoi(*P[5]); ++X)
						{
							const FMcStateId State = World.GetBlock(FIntVector(X, Y + Lift, Z));
							if (!Registry.IsAir(State)) Out += FString::Printf(TEXT("%d,%d,%d=%s;"), X, Y, Z, *Describe(State));
						}
				UE_LOG(LogTemp, Display, TEXT("mcours states: %s"), *Out);
			}
		}
	}
	return true;
}

#endif
