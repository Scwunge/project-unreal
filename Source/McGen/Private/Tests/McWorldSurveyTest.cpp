#include "McBiomeSource.h"
#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McDensityFunction.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McChunk.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McNoiseGenerator.h"
#include "McStructures.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Development aid, not a check: lists where the surface biomes are in a seeded world (nearest to the origin first)
// and how high the ground is there, so a camera for a side-by-side shot can be placed in each kind of terrain.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWorldSurveyTest, "McClient.McGen.WorldSurvey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWorldSurveyTest::RunTest(const FString& Parameters)
{
	FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	const FMcNoiseGenerator Generator(Settings, 4242424242ull, TEXT("minecraft:overworld"));
	if (!TestTrue(TEXT("generator is valid"), Generator.IsValid()))
	{
		return false;
	}
	struct FSpot { int32 X, Z, Y; int64 Distance; };
	TMap<FName, TArray<FSpot>> Spots;
	TMap<FName, int32> Counts;
	for (int32 Z = -2400; Z <= 2400; Z += 64)
	{
		for (int32 X = -2400; X <= 2400; X += 64)
		{
			const int32 Y = Generator.FindSurfaceY(X, Z);
			const FName Name = Registry.GetName(Generator.GetBiome(X, Y, Z));
			Counts.FindOrAdd(Name)++;
			TArray<FSpot>& List = Spots.FindOrAdd(Name);
			List.Add({ X, Z, Y, int64(X) * X + int64(Z) * Z });
		}
	}
	Counts.ValueSort([](int32 A, int32 B) { return A > B; });
	for (const TPair<FName, int32>& Entry : Counts)
	{
		TArray<FSpot>& List = Spots[Entry.Key];
		List.Sort([](const FSpot& A, const FSpot& B) { return A.Distance < B.Distance; });
		FString Line = FString::Printf(TEXT("%s x%d:"), *Entry.Key.ToString().RightChop(10), Entry.Value);
		for (int32 Index = 0; Index < FMath::Min(3, List.Num()); ++Index)
		{
			Line += FString::Printf(TEXT(" (%d,%d y%d)"), List[Index].X, List[Index].Z, List[Index].Y);
		}
		AddInfo(Line);
	}
	return true;
}

// The biome mix at y 64 over a 48 by 48 grid 256 apart (the same grid the vanilla reference run samples), summed over
// several seeds: one seed has too few climate features to say what a world's share of each biome is.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBiomeMixTest, "McClient.McGen.BiomeMix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBiomeMixTest::RunTest(const FString& Parameters)
{
	FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), Registry.EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	constexpr int32 Seeds = 8;
	TMap<FName, int32> Counts;
	int32 Total = 0;
	for (int32 SeedIndex = 0; SeedIndex < Seeds; ++SeedIndex)
	{
		const FMcNoiseGenerator Generator(Settings, SeedIndex == 0 ? 4242424242ull : 1000ull + SeedIndex * 7919ull, TEXT("minecraft:overworld"));
		for (int32 Z = 0; Z < 48; ++Z)
		{
			for (int32 X = 0; X < 48; ++X)
			{
				Counts.FindOrAdd(Registry.GetName(Generator.GetBiome(-6144 + X * 256, 64, -6144 + Z * 256)))++;
				++Total;
			}
		}
	}
	for (const int32 SampleY : { 0, -40 })
	{
		TMap<FName, int32> AtY;
		int32 AtTotal = 0;
		for (int32 SeedIndex = 0; SeedIndex < Seeds; ++SeedIndex)
		{
			const FMcNoiseGenerator Generator(Settings, SeedIndex == 0 ? 4242424242ull : 1000ull + SeedIndex * 7919ull, TEXT("minecraft:overworld"));
			for (int32 Z = 0; Z < 48; ++Z)
			{
				for (int32 X = 0; X < 48; ++X)
				{
					AtY.FindOrAdd(Registry.GetName(Generator.GetBiome(-6144 + X * 256, SampleY, -6144 + Z * 256)))++;
					++AtTotal;
				}
			}
		}
		AtY.ValueSort([](int32 A, int32 B) { return A > B; });
		FString Row;
		for (const TPair<FName, int32>& Entry : AtY)
		{
			Row += FString::Printf(TEXT("%s=%.2f "), *Entry.Key.ToString().RightChop(10), 100.0 * Entry.Value / AtTotal);
		}
		AddInfo(FString::Printf(TEXT("MIXY %d: %s"), SampleY, *Row));
	}
	Counts.ValueSort([](int32 A, int32 B) { return A > B; });
	FString Line;
	for (const TPair<FName, int32>& Entry : Counts)
	{
		Line += FString::Printf(TEXT("%s=%.2f "), *Entry.Key.ToString().RightChop(10), 100.0 * Entry.Value / Total);
	}
	AddInfo(FString::Printf(TEXT("MIX %d seeds: %s"), Seeds, *Line));
	// The six climate values (temperature, humidity, continentalness, erosion, depth, weirdness) over the same grid for
	// the first seed, as histograms in steps of 0.2 from -1.2 to 1.2 (the first and last bins take the tails).
	{
		const FMcNoiseGenerator Generator(Settings, 4242424242ull, TEXT("minecraft:overworld"));
		int32 Bins[6][12] = {};
		for (int32 Z = 0; Z < 48; ++Z)
		{
			for (int32 X = 0; X < 48; ++X)
			{
				const FMcClimate Climate = Generator.SampleClimate(-6144 + X * 256, 64, -6144 + Z * 256);
				for (int32 Index = 0; Index < 6; ++Index)
				{
					Bins[Index][FMath::Clamp(FMath::FloorToInt((Climate.Values[Index] + 1.2) / 0.2), 0, 11)]++;
				}
			}
		}
		for (int32 Index = 0; Index < 6; ++Index)
		{
			FString Row;
			for (int32 Bin = 0; Bin < 12; ++Bin)
			{
				Row += FString::Printf(TEXT("%d "), Bins[Index][Bin]);
			}
			AddInfo(FString::Printf(TEXT("CLIMATE %d: %s"), Index, *Row));
		}
	}
	// The spread of each climate value, averaged over 12 seeds (a world has few climate features, so one seed's is rough).
	{
		double SumDev[6] = {};
		double SumMean[6] = {};
		constexpr int32 DevSeeds = 12;
		for (int32 SeedIndex = 0; SeedIndex < DevSeeds; ++SeedIndex)
		{
			const FMcNoiseGenerator Generator(Settings, 555ull + SeedIndex * 104729ull, TEXT("minecraft:overworld"));
			double Sum[6] = {};
			double SumSq[6] = {};
			for (int32 Z = 0; Z < 48; ++Z)
			{
				for (int32 X = 0; X < 48; ++X)
				{
					const FMcClimate Climate = Generator.SampleClimate(-6144 + X * 256, 64, -6144 + Z * 256);
					for (int32 Index = 0; Index < 6; ++Index)
					{
						Sum[Index] += Climate.Values[Index];
						SumSq[Index] += Climate.Values[Index] * Climate.Values[Index];
					}
				}
			}
			for (int32 Index = 0; Index < 6; ++Index)
			{
				const double Mean = Sum[Index] / 2304.0;
				SumDev[Index] += FMath::Sqrt(SumSq[Index] / 2304.0 - Mean * Mean) / DevSeeds;
				SumMean[Index] += Mean / DevSeeds;
			}
		}
		AddInfo(FString::Printf(TEXT("CLIMATESD temperature %.3f humidity %.3f continentalness %.3f erosion %.3f depth %.3f weirdness %.3f"), SumDev[0], SumDev[1], SumDev[2], SumDev[3], SumDev[4], SumDev[5]));
	}
	return true;
}

// The ground height over the grid the vanilla reference's surface-height log uses (32 by 32 points 128 apart from
// -2048), over several seeds: quantiles and shares at sea level and in the mountains.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcHeightMixTest, "McClient.McGen.HeightMix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcHeightMixTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	TArray<int32> Heights;
	for (int32 SeedIndex = 0; SeedIndex < 4; ++SeedIndex)
	{
		const FMcNoiseGenerator Generator(Settings, 321ull + SeedIndex * 99991ull, TEXT("minecraft:overworld"));
		for (int32 Z = 0; Z < 32; ++Z)
		{
			for (int32 X = 0; X < 32; ++X)
			{
				Heights.Add(Generator.FindSurfaceY(-2048 + X * 128, -2048 + Z * 128));
			}
		}
	}
	Heights.Sort();
	const int32 Count = Heights.Num();
	auto Quantile = [&](double P) { return Heights[FMath::FloorToInt32(P * (Count - 1))]; };
	int32 AtSea = 0, High100 = 0, High150 = 0;
	for (const int32 Height : Heights)
	{
		AtSea += Height <= 63 ? 1 : 0;
		High100 += Height >= 100 ? 1 : 0;
		High150 += Height >= 150 ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("HEIGHTS %d points: q5 %d q25 %d q50 %d q75 %d q95 %d max %d, at or under 63: %.2f, 100 up: %.2f, 150 up: %.2f"),
		Count, Quantile(0.05), Quantile(0.25), Quantile(0.5), Quantile(0.75), Quantile(0.95), Heights.Last(),
		double(AtSea) / Count, double(High100) / Count, double(High150) / Count));
	return true;
}

// Where the variance of each climate function sits by wavelength, along the same lines the vanilla reference run
// logs ("log lines": 6 lines of 4096 points 8 apart, 5000 apart in z, y 64): a Hann-windowed periodogram summed in
// octave bands from 32768 down to 16 blocks, averaged over seeds. Printed as variance times 1000 per band.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcNoiseSpectrumTest, "McClient.McGen.NoiseSpectrum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// In-place radix-2 FFT.
	void Fft(TArray<TPair<double, double>>& Data)
	{
		const int32 Count = Data.Num();
		for (int32 I = 1, J = 0; I < Count; ++I)
		{
			int32 Bit = Count >> 1;
			for (; J & Bit; Bit >>= 1)
			{
				J ^= Bit;
			}
			J ^= Bit;
			if (I < J)
			{
				Swap(Data[I], Data[J]);
			}
		}
		for (int32 Length = 2; Length <= Count; Length <<= 1)
		{
			const double Angle = -2.0 * PI / Length;
			for (int32 Start = 0; Start < Count; Start += Length)
			{
				for (int32 K = 0; K < Length / 2; ++K)
				{
					const double Cos = FMath::Cos(Angle * K);
					const double Sin = FMath::Sin(Angle * K);
					const TPair<double, double> Even = Data[Start + K];
					const TPair<double, double> Odd = Data[Start + K + Length / 2];
					const double Re = Odd.Key * Cos - Odd.Value * Sin;
					const double Im = Odd.Key * Sin + Odd.Value * Cos;
					Data[Start + K] = { Even.Key + Re, Even.Value + Im };
					Data[Start + K + Length / 2] = { Even.Key - Re, Even.Value - Im };
				}
			}
		}
	}
}

bool FMcNoiseSpectrumTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	const TCHAR* Names[6] = { TEXT("temperature"), TEXT("vegetation"), TEXT("continents"), TEXT("erosion"), TEXT("depth"), TEXT("ridges") };
	constexpr int32 N = 4096;
	constexpr int32 Step = 8;
	constexpr int32 Seeds = 4;
	constexpr int32 Lines = 6;
	constexpr int32 Bands = 11;
	TArray<double> Window;
	double WindowSq = 0.0;
	for (int32 Index = 0; Index < N; ++Index)
	{
		Window.Add(0.5 - 0.5 * FMath::Cos(2.0 * PI * Index / (N - 1)));
		WindowSq += Window[Index] * Window[Index];
	}
	for (int32 Function = 0; Function < 6; ++Function)
	{
		double BandSum[Bands] = {};
		double VarianceSum = 0.0;
		for (int32 SeedIndex = 0; SeedIndex < Seeds; ++SeedIndex)
		{
			const FMcNoiseGenerator Generator(Settings, 777ull + SeedIndex * 15485863ull, TEXT("minecraft:overworld"));
			const int32 Node = Generator.GetRouterFunction(Names[Function] == FString(TEXT("temperature")) ? TEXT("temperature") : Names[Function]);
			if (Node == INDEX_NONE)
			{
				AddError(FString::Printf(TEXT("no router function %s"), Names[Function]));
				return false;
			}
			FMcDensityContext Context(Generator.GetGraph());
			for (int32 Line = 0; Line < Lines; ++Line)
			{
				TArray<double> Values;
				double Mean = 0.0;
				for (int32 Index = 0; Index < N; ++Index)
				{
					Context.SetPos(-16384 + Index * Step, 64, Line * 5000);
					Values.Add(Generator.GetGraph().Evaluate(Node, Context));
					Mean += Values.Last() / N;
				}
				double Variance = 0.0;
				TArray<TPair<double, double>> Data;
				for (int32 Index = 0; Index < N; ++Index)
				{
					Variance += FMath::Square(Values[Index] - Mean) / N;
					Data.Add({ (Values[Index] - Mean) * Window[Index], 0.0 });
				}
				VarianceSum += Variance / (Seeds * Lines);
				Fft(Data);
				for (int32 Bin = 1; Bin <= N / 2; ++Bin)
				{
					const double Frequency = double(Bin) / (N * Step);
					const double Power = 2.0 * (FMath::Square(Data[Bin].Key) + FMath::Square(Data[Bin].Value)) / (N * WindowSq);
					for (int32 Band = 0; Band < Bands; ++Band)
					{
						const double Low = (1.0 / 32768.0) * FMath::Pow(2.0, Band);
						if (Frequency >= Low && Frequency < Low * 2.0)
						{
							BandSum[Band] += Power / (Seeds * Lines);
						}
					}
				}
			}
		}
		FString Row;
		for (int32 Band = 0; Band < Bands; ++Band)
		{
			Row += FString::Printf(TEXT("%.1f "), BandSum[Band] * 1000.0);
		}
		AddInfo(FString::Printf(TEXT("SPECTRUM %s var %.3f sd %.3f bands: %s"), Names[Function], VarianceSum, FMath::Sqrt(VarianceSum), *Row));
	}
	return true;
}

// Pointwise check against the reference client: the six router climate functions along the first logged line
// (x -16384 + 8 i, z 0, y 64, 1/10000 resolution), for each seed the reference saves use. An exact construction
// agrees to the log's rounding; the best seed's errors are printed per function.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcClimatePointTest, "McClient.McGen.ClimatePoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcClimatePointTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	FString Text;
	if (!TestTrue(TEXT("reference line"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Tests/vanilla_line0.txt")))))
	{
		return false;
	}
	TArray<TArray<double>> Reference;
	TArray<FString> Rows;
	Text.ParseIntoArrayLines(Rows);
	for (const FString& Row : Rows)
	{
		TArray<FString> Parts;
		Row.ParseIntoArrayWS(Parts);
		TArray<double>& Values = Reference.AddDefaulted_GetRef();
		for (int32 Index = 1; Index < Parts.Num(); ++Index)
		{
			Values.Add(FCString::Atod(*Parts[Index]) / 10000.0);
		}
	}
	const TCHAR* Names[6] = { TEXT("temperature"), TEXT("vegetation"), TEXT("continents"), TEXT("erosion"), TEXT("depth"), TEXT("ridges") };
	const int64 Seeds[] = { 4242424242ll, 1001ll, -77777ll, 909090909ll, 0x75088ff07ll, -8467505393108131185ll };
	for (const int64 Seed : Seeds)
	{
		const FMcNoiseGenerator Generator(Settings, static_cast<uint64>(Seed), TEXT("minecraft:overworld"));
		FMcDensityContext Context(Generator.GetGraph());
		FString Line = FString::Printf(TEXT("POINTS seed %lld max/mean:"), Seed);
		for (int32 Function = 0; Function < 6 && Function < Reference.Num(); ++Function)
		{
			const int32 Node = Generator.GetRouterFunction(Names[Function]);
			double Max = 0.0;
			double Sum = 0.0;
			const int32 Count = Reference[Function].Num();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Context.SetPos(-16384 + Index * 8, 64, 0);
				const double Error = FMath::Abs(Generator.GetGraph().Evaluate(Node, Context) - Reference[Function][Index]);
				Max = FMath::Max(Max, Error);
				Sum += Error / Count;
			}
			Line += FString::Printf(TEXT(" %.4f/%.4f"), Max, Sum);
		}
		AddInfo(Line);
	}
	return true;
}

// Block statistics over 5 by 5 patches of 4 by 4 chunks (patch corners 32 chunks apart from chunk -64, -64), fully
// generated and decorated, in the form the reference client's "log stats" verb prints, for comparing worlds that
// are not seed-matched:
//   STATA <seed> <block> <16-block band counts from the world bottom, 24 of them>
//   STATG <seed> <biome> <block> <columns>   the ground (top block that blocks motion, without leaves and logs)
//   STATV <seed> <biome> <block> <count>     blocks above the ground
//   STATC <seed> <biome> <columns>
namespace
{
	bool RunWorldStats(FAutomationTestBase& Test, int32 SeedIndex, const TArray<FIntPoint>* Listed = nullptr)
	{
		FMcNoiseSettings Settings;
		if (!Test.TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
		{
			return false;
		}
		const uint64 Seeds[] = { 4242424242ull, 1001ull, static_cast<uint64>(-77777ll), 909090909ull, 31415926535ull };
		const uint64 Seed = Seeds[SeedIndex];
		// Content/Tests/aqcfg.txt, if present: lines "<console variable> <value>" for sweeping the aquifer over these statistics.
		FString ConfigText;
		if (FFileHelper::LoadFileToString(ConfigText, *(FPaths::ProjectContentDir() / TEXT("Tests/aqcfg.txt"))))
		{
			TArray<FString> ConfigLines;
			ConfigText.ParseIntoArrayLines(ConfigLines);
			for (const FString& ConfigLine : ConfigLines)
			{
				FString Name, Value;
				if (ConfigLine.Split(TEXT(" "), &Name, &Value))
				{
					if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name))
					{
						Variable->Set(*Value);
					}
				}
			}
		}
		const TSharedRef<FMcNoiseGenerator> Generator = MakeShared<FMcNoiseGenerator>(Settings, Seed, TEXT("minecraft:overworld"));
		FMcFeatureLibrary Features(Settings.SeaLevel, Settings.MinY, Settings.Height);
		Features.LoadBiomes();
		bMcFeatureStatsOn = true;
		Features.SetHeightSource([Generator](int32 X, int32 Z) { return Generator->FindSurfaceY(X, Z) + 1; });
		Features.SetBiomeSource([Generator](int32 X, int32 Y, int32 Z) { return Generator->GetBiome(X, Y, Z); });
		const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
		TMap<FString, TArray<int64>> Bands;
		TMap<FString, TArray<int64>> PerY;
		TMap<FString, int64> Ground, Above, Columns;
		// Per biome and log block: trunk bases (a log with no log below it) and logs.
		TMap<FString, TPair<int64, int64>> Trees;
		TMap<FString, TPair<int64, int64>> Depth;  // biome and water depth bucket: columns, columns with kelp
		TMap<FString, int64> PrelDiff;
		TMap<FString, TTuple<int64, int64, int64>> OpenCells;  // sea or land column and 8-block band: air, water, lava below the ground
		auto NameOf = [&Blocks](FMcStateId State) { return Blocks.GetBlockOf(State).Name.ToString(); };
		TArray<FIntPoint> CornerList;
		if (Listed)
		{
			CornerList = *Listed;
		}
		else
		{
			for (int32 PatchZ = 0; PatchZ < 5; ++PatchZ)
			{
				for (int32 PatchX = 0; PatchX < 5; ++PatchX)
				{
					CornerList.Add(FIntPoint(-64 + PatchX * 32, -64 + PatchZ * 32));
				}
			}
		}
		for (int32 PatchIndex = 0; PatchIndex < CornerList.Num(); ++PatchIndex)
		{
			{
				const FIntPoint Corner = CornerList[PatchIndex];
				constexpr int32 Side = 6;
				TMap<FString, FString> MangroveBlocks;
				TArray<TUniquePtr<FMcChunk>> Chunks;
				for (int32 Index = 0; Index < Side * Side; ++Index)
				{
					Chunks.Add(MakeUnique<FMcChunk>(FIntPoint(Corner.X - 1 + Index % Side, Corner.Y - 1 + Index / Side), Settings.MinY, Settings.Height / 16));
					Generator->Generate(*Chunks.Last());
				}
				auto Around = [&](int32 CX, int32 CZ, FMcChunk** Out)
				{
					for (int32 Index = 0; Index < 9; ++Index)
					{
						Out[Index] = Chunks[(CZ - 1 + Index / 3) * Side + (CX - 1 + Index % 3)].Get();
					}
				};
				for (int32 CZ = 1; CZ < Side - 1; ++CZ)
				{
					for (int32 CX = 1; CX < Side - 1; ++CX)
					{
						FMcChunk* Nine[9];
						Around(CX, CZ, Nine);
						FMcGenRegion Region(FIntPoint(Corner.X - 1 + CX, Corner.Y - 1 + CZ), Nine);
						Features.Decorate(Region, Seed);
					}
				}
				for (int32 CZ = 1; CZ < Side - 1; ++CZ)
				{
					for (int32 CX = 1; CX < Side - 1; ++CX)
					{
						FMcChunk* Nine[9];
						Around(CX, CZ, Nine);
						FMcGenRegion Region(FIntPoint(Corner.X - 1 + CX, Corner.Y - 1 + CZ), Nine);
						const FMcChunk& Chunk = *Chunks[CZ * Side + CX];
						for (int32 LZ = 0; LZ < 16; ++LZ)
						{
							for (int32 LX = 0; LX < 16; ++LX)
							{
								const int32 X = Chunk.GetPos().X * 16 + LX;
								const int32 Z = Chunk.GetPos().Y * 16 + LZ;
								int32 GroundY = Region.GetHeight(EMcHeightmap::OceanFloor, X, Z) - 1;
								while (GroundY > Settings.MinY)
								{
									const FString Name = NameOf(Chunk.GetBlock(LX, GroundY, LZ));
									if (Name.EndsWith(TEXT("leaves")) || Name.EndsWith(TEXT("_log")) || Name.EndsWith(TEXT("_wood")))
									{
										--GroundY;
									}
									else
									{
										break;
									}
								}
								const FString Biome = FMcBiomeRegistry::Get().GetName(Chunk.GetBiome(LX, GroundY, LZ)).ToString();
								Columns.FindOrAdd(Biome)++;
								bool bKelpColumn = false;
								bool bLogBelow = false;
								for (int32 Y = Settings.MinY; Y < Settings.MinY + Settings.Height; ++Y)
								{
									const FString Name = NameOf(Chunk.GetBlock(LX, Y, LZ));
									const bool bLog = Name.EndsWith(TEXT("_log"));
									if (bLog)
									{
										TPair<int64, int64>& Tree = Trees.FindOrAdd(FMcBiomeRegistry::Get().GetName(Chunk.GetBiome(LX, Y, LZ)).ToString() + TEXT(" ") + Name, TPair<int64, int64>(0, 0));
										Tree.Key += bLogBelow ? 0 : 1;
										Tree.Value++;
									}
									bLogBelow = bLog;
									if (Listed && Name.Contains(TEXT("mangrove")))
									{
										MangroveBlocks.FindOrAdd(Name) += FString::Printf(TEXT(" %d,%d,%d"), X, Y, Z);
									}
									Bands.FindOrAdd(Name, TArray<int64>()).SetNumZeroed(24);
									Bands[Name][(Y - Settings.MinY) >> 4]++;
									if (Y < 64 && (Name.Contains(TEXT("copper_ore")) || Name.Contains(TEXT("iron_ore")) || Name.Contains(TEXT("raw_copper_block")) || Name.Contains(TEXT("raw_iron_block")) || Name == TEXT("minecraft:granite") || Name == TEXT("minecraft:tuff")))
									{
										PerY.FindOrAdd(Name, TArray<int64>()).SetNumZeroed(128);
										PerY[Name][Y - Settings.MinY]++;
									}
									if (Y > GroundY && (Name == TEXT("minecraft:kelp") || Name == TEXT("minecraft:kelp_plant")))
									{
										bKelpColumn = true;
									}
									if (Y == GroundY)
									{
										Ground.FindOrAdd(Biome + TEXT(" ") + Name)++;
									}
									else if (Y > GroundY && Name != TEXT("minecraft:air") && Name != TEXT("minecraft:cave_air"))
									{
										Above.FindOrAdd(Biome + TEXT(" ") + Name)++;
									}
								}
								if (GroundY >= 62)
								{
									// The aquifer's preliminary surface (chunk corner) against the ground of a land column: STATP <seed> <bucket> <columns>.
									const int32 Diff = GroundY - FMath::FloorToInt32(Generator->GetPreliminarySurface(Chunk.GetPos().X * 16, Chunk.GetPos().Y * 16));
									PrelDiff.FindOrAdd(FString::Printf(TEXT("%d"), FMath::Clamp(FMath::FloorToInt32(Diff / 8.0), -8, 8) * 8), 0)++;
								}
								// Open cells below the ground, by whether the column's ground is under the sea (the reference's stat W).
								for (int32 Y = Settings.MinY; Y < FMath::Min(GroundY, 64); ++Y)
								{
									const FMcStateId Cell = Chunk.GetBlock(LX, Y, LZ);
									const bool bAir = Region.IsAir(Cell);
									const EMcFluid Fluid = FMcBlockShapes::Get().GetFluid(Cell);
									if (bAir || Fluid != EMcFluid::None)
									{
										TTuple<int64, int64, int64>& Open = OpenCells.FindOrAdd(FString::Printf(TEXT("%s %d"), GroundY < 62 ? TEXT("sea") : TEXT("land"), (Y - Settings.MinY) >> 3), MakeTuple<int64, int64, int64>(0, 0, 0));
										(bAir ? Open.Get<0>() : Fluid == EMcFluid::Water ? Open.Get<1>() : Open.Get<2>())++;
										if (Fluid == EMcFluid::Water && Y + 1 < GroundY && Region.IsAir(Chunk.GetBlock(LX, Y + 1, LZ)))
										{
											OpenCells.FindOrAdd(FString::Printf(TEXT("%sS %d"), GroundY < 62 ? TEXT("sea") : TEXT("land"), Y), MakeTuple<int64, int64, int64>(0, 0, 0)).Get<1>()++;
										}
										if (Y >= -8 && Y < 48)
										{
											TTuple<int64, int64, int64>& Fine = OpenCells.FindOrAdd(FString::Printf(TEXT("%sY %d"), GroundY < 62 ? TEXT("sea") : TEXT("land"), Y), MakeTuple<int64, int64, int64>(0, 0, 0));
											(bAir ? Fine.Get<0>() : Fluid == EMcFluid::Water ? Fine.Get<1>() : Fine.Get<2>())++;
										}
									}
								}
								if (GroundY < Settings.SeaLevel - 1 && Biome.Contains(TEXT("ocean")))
								{
									TPair<int64, int64>& Bucket = Depth.FindOrAdd(FString::Printf(TEXT("%s %d"), *Biome, (Settings.SeaLevel - GroundY) / 4 * 4), TPair<int64, int64>(0, 0));
									Bucket.Key++;
									Bucket.Value += bKelpColumn ? 1 : 0;
								}
							}
						}
					}
				}
				// Mangrove blocks of the patch, for fitting single trees: MTREE <seed> <patch corner chunk> <block> <x,y,z ...>
				for (const TPair<FString, FString>& Pair : MangroveBlocks)
				{
					Test.AddInfo(FString::Printf(TEXT("MTREE %d %d %d %s%s"), SeedIndex, Corner.X, Corner.Y, *Pair.Key, *Pair.Value));
				}
			}
		}
		for (const TPair<FString, TArray<int64>>& Pair : Bands)
		{
			FString Row;
			for (const int64 Count : Pair.Value)
			{
				Row += FString::Printf(TEXT(" %lld"), Count);
			}
			Test.AddInfo(FString::Printf(TEXT("STATA %d %s%s"), SeedIndex, *Pair.Key, *Row));
		}
		for (const TPair<FString, TArray<int64>>& Pair : PerY)
		{
			FString Row;
			for (const int64 Count : Pair.Value)
			{
				Row += FString::Printf(TEXT(" %lld"), Count);
			}
			Test.AddInfo(FString::Printf(TEXT("STATY %d %s%s"), SeedIndex, *Pair.Key, *Row));
		}
		for (const TPair<FString, int64>& Pair : PrelDiff) { Test.AddInfo(FString::Printf(TEXT("STATP %d %s %lld"), SeedIndex, *Pair.Key, Pair.Value)); }
		for (const TPair<FString, TTuple<int64, int64, int64>>& Pair : OpenCells) { Test.AddInfo(FString::Printf(TEXT("STATW %d %s %lld %lld %lld"), SeedIndex, *Pair.Key, Pair.Value.Get<0>(), Pair.Value.Get<1>(), Pair.Value.Get<2>())); }
		for (const TPair<FString, TPair<int64, int64>>& Pair : Depth) { Test.AddInfo(FString::Printf(TEXT("STATD %d %s %lld %lld"), SeedIndex, *Pair.Key, Pair.Value.Key, Pair.Value.Value)); }
		for (const TPair<FString, int64>& Pair : Ground) { Test.AddInfo(FString::Printf(TEXT("STATG %d %s %lld"), SeedIndex, *Pair.Key, Pair.Value)); }
		for (const TPair<FString, int64>& Pair : Above) { Test.AddInfo(FString::Printf(TEXT("STATV %d %s %lld"), SeedIndex, *Pair.Key, Pair.Value)); }
		for (const TPair<FString, TPair<int64, int64>>& Pair : Trees) { Test.AddInfo(FString::Printf(TEXT("STATT %d %s %lld %lld"), SeedIndex, *Pair.Key, Pair.Value.Key, Pair.Value.Value)); }
		for (const TPair<FString, TPair<int64, int64>>& Pair : McFeatureStats) { Test.AddInfo(FString::Printf(TEXT("STATF %d %s %lld %lld"), SeedIndex, *Pair.Key, Pair.Value.Key, Pair.Value.Value)); }
		for (const TPair<FString, int64>& Pair : Columns) { Test.AddInfo(FString::Printf(TEXT("STATC %d %s %lld"), SeedIndex, *Pair.Key, Pair.Value)); }
		return true;
	}
}

#define MC_WORLD_STATS_TEST(Index) 	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWorldStats##Index##Test, "McClient.McGen.WorldStats" #Index, 		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter) 	bool FMcWorldStats##Index##Test::RunTest(const FString& Parameters) { return RunWorldStats(*this, Index); }
MC_WORLD_STATS_TEST(0)
MC_WORLD_STATS_TEST(1)
MC_WORLD_STATS_TEST(2)
MC_WORLD_STATS_TEST(3)
MC_WORLD_STATS_TEST(4)

#endif

// Air, water and lava as a share of each 8-block band, over 25 patches of 4 by 4 undecorated chunks, with the carvers on
// and off, to see how much of the caves comes from the carvers. "CAVEBANDS <carvers> <seed> <band start y> air water lava".
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCaveBandsTest, "McClient.McGen.CaveBands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcCaveBandsTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	const uint64 Seeds[] = { 4242424242ull, 1001ull, static_cast<uint64>(-77777ll) };
	const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	for (const bool bOff : { false, true })
	{
		bMcCarversOff = bOff;
		TArray<int64> Air, Water, Lava, Total;
		Air.SetNumZeroed(48);
		Water.SetNumZeroed(48);
		Lava.SetNumZeroed(48);
		Total.SetNumZeroed(48);
		for (const uint64 Seed : Seeds)
		{
			const FMcNoiseGenerator Generator(Settings, Seed, TEXT("minecraft:overworld"));
			for (int32 Patch = 0; Patch < 25; ++Patch)
			{
				const FIntPoint Corner(-64 + (Patch % 5) * 32, -64 + (Patch / 5) * 32);
				for (int32 Index = 0; Index < 16; ++Index)
				{
					FMcChunk Chunk(FIntPoint(Corner.X + Index % 4, Corner.Y + Index / 4), Settings.MinY, Settings.Height / 16);
					Generator.Generate(Chunk);
					for (int32 Y = Settings.MinY; Y < Settings.MinY + 128 + 64; ++Y)
					{
						const int32 Band = (Y - Settings.MinY) >> 3;
						for (int32 Z = 0; Z < 16; ++Z)
						{
							for (int32 X = 0; X < 16; ++X)
							{
								const FString Name = Blocks.GetBlockOf(Chunk.GetBlock(X, Y, Z)).Name.ToString();
								Total[Band]++;
								if (Name == TEXT("minecraft:air") || Name == TEXT("minecraft:cave_air")) Air[Band]++;
								else if (Name == TEXT("minecraft:water")) Water[Band]++;
								else if (Name == TEXT("minecraft:lava")) Lava[Band]++;
							}
						}
					}
				}
			}
		}
		for (int32 Band = 0; Band < 24; ++Band)
		{
			AddInfo(FString::Printf(TEXT("CAVEBANDS %d %d air %.2f water %.2f lava %.3f"), bOff ? 0 : 1, Settings.MinY + Band * 8,
				100.0 * Air[Band] / Total[Band], 100.0 * Water[Band] / Total[Band], 100.0 * Lava[Band] / Total[Band]));
		}
	}
	bMcCarversOff = false;
	return true;
}

// Sweeps aquifer parameters (console variables) and prints the bands y -8 to 47 for each: 1 seed, 25 patches.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcAquiferSweepTest, "McClient.McGen.AquiferSweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcAquiferSweepTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	// The sea-floor clause's three numbers; each line is the water share of the open cells below the ground, by 8-block
	// band from y -8 to 39, for columns whose ground is under the sea and for land columns, over 5 seeds.
	struct FConfig { double Full0; double Partial0; double Depth; int32 Base; double Flood; };
	const FConfig Configs[] = { { -0.2, -0.7, 44, 20, 1.0 }, { -0.2, -0.7, 44, 20, 0.75 }, { -0.2, -0.7, 32, 20, 1.0 } };
	const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	const uint64 Seeds[] = { 4242424242ull, 1001ull, 909090909ull };
	for (const FConfig& Config : Configs)
	{
		IConsoleManager::Get().FindConsoleVariable(TEXT("mc.aq.seafull0"))->Set(static_cast<float>(Config.Full0));
		IConsoleManager::Get().FindConsoleVariable(TEXT("mc.aq.seapartial0"))->Set(static_cast<float>(Config.Partial0));
		IConsoleManager::Get().FindConsoleVariable(TEXT("mc.aq.seadepth"))->Set(static_cast<float>(Config.Depth));
		IConsoleManager::Get().FindConsoleVariable(TEXT("mc.aq.levelbase"))->Set(Config.Base);
		IConsoleManager::Get().FindConsoleVariable(TEXT("mc.aq.floodscale"))->Set(static_cast<float>(Config.Flood));
		int64 Open[2][6] = {}, Water[2][6] = {};
		for (const uint64 Seed : Seeds)
		{
			const FMcNoiseGenerator Generator(Settings, Seed, TEXT("minecraft:overworld"));
			for (int32 Patch = 0; Patch < 25; ++Patch)
			{
				const FIntPoint Corner(-64 + (Patch % 5) * 32, -64 + (Patch / 5) * 32);
				for (int32 Index = 0; Index < 4; ++Index)
				{
					FMcChunk Chunk(FIntPoint(Corner.X + Index % 2, Corner.Y + Index / 2), Settings.MinY, Settings.Height / 16);
					Generator.Generate(Chunk);
					for (int32 Z = 0; Z < 16; ++Z)
					{
						for (int32 X = 0; X < 16; ++X)
						{
							// The ground: the highest block that is neither air nor water.
							int32 Ground = 100;
							while (Ground > Settings.MinY)
							{
								const FString Top = Blocks.GetBlockOf(Chunk.GetBlock(X, Ground, Z)).Name.ToString();
								if (Top != TEXT("minecraft:air") && Top != TEXT("minecraft:cave_air") && Top != TEXT("minecraft:water"))
								{
									break;
								}
								--Ground;
							}
							const int32 Class = Ground < 62 ? 1 : 0;
							for (int32 Y = -8; Y < FMath::Min(Ground, 40); ++Y)
							{
								const FMcStateId Cell = Chunk.GetBlock(X, Y, Z);
								const FString Name = Blocks.GetBlockOf(Cell).Name.ToString();
								const bool bWater = Name == TEXT("minecraft:water");
								if (bWater || Name == TEXT("minecraft:air") || Name == TEXT("minecraft:cave_air"))
								{
									const int32 Band = (Y + 8) >> 3;
									Open[Class][Band]++;
									Water[Class][Band] += bWater ? 1 : 0;
								}
							}
						}
					}
				}
			}
		}
		for (int32 Class = 0; Class < 2; ++Class)
		{
			FString Line = FString::Printf(TEXT("AQSWEEP full0 %.2f partial0 %.2f depth %.0f base %d flood %.2f %s |"), Config.Full0, Config.Partial0, Config.Depth, Config.Base, Config.Flood, Class ? TEXT("sea") : TEXT("land"));
			for (int32 Band = 0; Band < 6; ++Band)
			{
				Line += FString::Printf(TEXT(" y%d %.1f(%lld)"), Band * 8 - 8, 100.0 * Water[Class][Band] / FMath::Max<int64>(Open[Class][Band], 1), Open[Class][Band]);
			}
			AddInfo(Line);
		}
	}
	return true;
}

// The top solid block over the grid of the reference's "log floors" (32 by 32 points 128 apart from -2048), per seed, one
// "FLOORS <seed index> <values...>" line each, for comparing the sea-floor depth with the vanilla ocean-floor heightmap.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFloorMixTest, "McClient.McGen.FloorMix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFloorMixTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	const uint64 Seeds[] = { 4242424242ull, 1001ull, static_cast<uint64>(-77777ll), 909090909ull, 31415926535ull };
	for (int32 SeedIndex = 0; SeedIndex < 5; ++SeedIndex)
	{
		const FMcNoiseGenerator Generator(Settings, Seeds[SeedIndex], TEXT("minecraft:overworld"));
		FString Line;
		for (int32 Z = 0; Z < 32; ++Z)
		{
			for (int32 X = 0; X < 32; ++X)
			{
				Line += FString::Printf(TEXT("%d "), Generator.FindSolidTopY(-2048 + X * 128, -2048 + Z * 128) + 1);
			}
		}
		AddInfo(FString::Printf(TEXT("FLOORS %d %s"), SeedIndex, *Line));
	}
	return true;
}

// Structure starts over 256 x 256 chunks from (-128, -128), per seed: a chunk the structure set picks whose biome (at sea
// level + 4, in the chunk's middle) the structure allows. "STARTS <seed> <structure> <count>"; the reference client's
// "log starts" gives the same over the same chunks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcStructureStartsTest, "McClient.McGen.StructureStarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcStructureStartsTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	FMcStructureLibrary Structures(Settings.SeaLevel, Settings.MinY);
	if (!TestTrue(TEXT("structure sets load"), Structures.Load()))
	{
		return false;
	}
	const uint64 Seeds[] = { 4242424242ull, 1001ull, static_cast<uint64>(-77777ll), 909090909ull, 31415926535ull };
	for (int32 SeedIndex = 0; SeedIndex < 5; ++SeedIndex)
	{
		const uint64 Seed = Seeds[SeedIndex];
		const FMcNoiseGenerator Generator(Settings, Seed, TEXT("minecraft:overworld"));
		// The real start path: the biome one block under the surface, the ground found by the generator.
		Structures.SetHeightSource([&Generator](int32 X, int32 Z) { return Generator.FindSurfaceY(X, Z) + 1; });
		Structures.SetBiomeSource([&Generator](int32 X, int32 Y, int32 Z) { return Generator.GetBiome(X, Y, Z); });
		TMap<FName, int64> Counts;
		TMap<FName, int64> Pre;
		TMap<FName, int64> AllCandidates;
		for (int32 Z = -128; Z < 128; ++Z)
		{
			for (int32 X = -128; X < 128; ++X)
			{
				TArray<FName> Here;
				Structures.GetCandidates(Seed, FIntPoint(X, Z), Here);
				if (Here.IsEmpty())
				{
					continue;
				}
				for (const FName Name : Here)
				{
					if (Name == FName(TEXT("minecraft:ancient_city")))
					{
						AddInfo(FString::Printf(TEXT("ANC %d %d %d %s %s"), SeedIndex, X, Z, *FMcBiomeRegistry::Get().GetName(Generator.GetBiome(X * 16, -27, Z * 16)).ToString(), *FMcBiomeRegistry::Get().GetName(Generator.GetBiome(X * 16 + 8, -27, Z * 16 + 8)).ToString()));
					}
				}
				const FMcBiomeId Biome = Generator.GetBiome(X * 16 + 8, Generator.FindSurfaceY(X * 16 + 8, Z * 16 + 8), Z * 16 + 8);
				for (const FName Name : Here)
				{
					AllCandidates.FindOrAdd(Name)++;
					if (Name.ToString().Contains(TEXT("monument")) || Name.ToString().Contains(TEXT("village_plains")))
					{
						AddInfo(FString::Printf(TEXT("CANDCELL %d %s %d %d %s"), SeedIndex, *Name.ToString(), X, Z, *FMcBiomeRegistry::Get().GetName(Biome).ToString()));
					}
				}
				Here.Reset();
				Structures.GetCandidates(Seed, FIntPoint(X, Z), Biome, Here);
				for (const FName Name : Here)
				{
					Pre.FindOrAdd(Name)++;
					if (!Structures.SurroundAllows(Name, FIntPoint(X, Z), [&](int32 BX, int32 BZ) { return Generator.GetBiome(BX, Generator.FindSurfaceY(BX, BZ), BZ); }))
					{
						continue;
					}
					Counts.FindOrAdd(Name)++;
				}
			}
		}
		for (const TPair<FName, int64>& Pair : AllCandidates)
		{
			AddInfo(FString::Printf(TEXT("STARTSALL %d %s %lld"), SeedIndex, *Pair.Key.ToString(), Pair.Value));
		}
		for (const TPair<FName, int64>& Pair : Counts)
		{
			AddInfo(FString::Printf(TEXT("STARTS %d %s %lld"), SeedIndex, *Pair.Key.ToString(), Pair.Value));
			AddInfo(FString::Printf(TEXT("STARTSPRE %d %s %lld"), SeedIndex, *Pair.Key.ToString(), Pre.FindRef(Pair.Key)));
		}
	}
	return true;
}

// The climate and biome stages against the reference client's `log cbiomes` grid (x and z from -8192, 256 apart, 64 by 64,
// y 64; Content/Tests/vanilla_cb_<n>.txt, "x z t h c e d w biome" in 1/10000). Two checks: our biome lookup fed the
// reference's own climate values (no seed involved, so every mismatch is the lookup), and our noise's climate values and
// biomes on the same grid for the five seeds (CB lines, for comparing the value distributions).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcClimateGridTest, "McClient.McGen.ClimateGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcClimateGridTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	const FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	FMcBiomeSource Source;
	if (!TestTrue(TEXT("biome source"), Source.Load(TEXT("minecraft:overworld"))))
	{
		return false;
	}
	for (int32 World = 1; World <= 5; ++World)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / FString::Printf(TEXT("Tests/vanilla_cb_%d.txt"), World))))
		{
			continue;
		}
		TArray<FString> Rows;
		Text.ParseIntoArrayLines(Rows);
		int32 Total = 0;
		int32 Same = 0;
		TMap<FString, int32> Swaps;
		for (const FString& Row : Rows)
		{
			TArray<FString> Parts;
			Row.ParseIntoArrayWS(Parts);
			if (Parts.Num() < 9)
			{
				continue;
			}
			FMcClimate Climate;
			for (int32 Index = 0; Index < 6; ++Index)
			{
				Climate.Values[Index] = FCString::Atod(*Parts[2 + Index]) / 10000.0;
			}
			FString Vanilla = Parts[8];
			Vanilla.RemoveFromStart(TEXT("ResourceKey[minecraft:worldgen/biome / "));
			Vanilla.RemoveFromEnd(TEXT("]"));
			const FString Ours = Registry.GetName(Source.FindSlow(Climate)).ToString();
			++Total;
			if (Ours == Vanilla)
			{
				++Same;
			}
			else
			{
				Swaps.FindOrAdd(Vanilla + TEXT(" -> ") + Ours)++;
			}
		}
		AddInfo(FString::Printf(TEXT("LOOKUP world %d: %d of %d points the same"), World, Same, Total));
		Swaps.ValueSort([](int32 A, int32 B) { return A > B; });
		int32 Shown = 0;
		for (const TPair<FString, int32>& Pair : Swaps)
		{
			AddInfo(FString::Printf(TEXT("LOOKUPSWAP %d %s %d"), World, *Pair.Key, Pair.Value));
			if (++Shown >= 12)
			{
				break;
			}
		}
	}
	const uint64 Seeds[] = { 4242424242ull, 1001ull, static_cast<uint64>(-77777ll), 909090909ull, 31415926535ull };
	TMap<int32, int32> DeepDark;
	for (int32 SeedIndex = 0; SeedIndex < 5; ++SeedIndex)
	{
		const FMcNoiseGenerator Generator(Settings, Seeds[SeedIndex], TEXT("minecraft:overworld"));
		for (int32 J = 0; J < 64; ++J)
		{
			for (int32 I = 0; I < 64; ++I)
			{
				const int32 X = -8192 + I * 256;
				const int32 Z = -8192 + J * 256;
				const FMcClimate Climate = Generator.SampleClimate(X, 64, Z);
				int32 Hint = INDEX_NONE;
				{
					// The same grid at y -27 (ancient cities start there): the biome only; and the deep_dark count at other heights.
					const FMcClimate Deep = Generator.SampleClimate(X, -27, Z);
					int32 DeepHint = INDEX_NONE;
					AddInfo(FString::Printf(TEXT("CBD %d %d %d %s"), SeedIndex, X, Z, *Registry.GetName(Source.Find(Deep, DeepHint)).ToString()));
					for (int32 Slot = 0; Slot < 7; ++Slot)
					{
						const int32 DeepY = -8 - Slot * 8;
						const FMcClimate Level = Generator.SampleClimate(X, DeepY, Z);
						int32 LevelHint = INDEX_NONE;
						if (Registry.GetName(Source.Find(Level, LevelHint)) == FName(TEXT("minecraft:deep_dark")))
						{
							++DeepDark.FindOrAdd(SeedIndex * 100 + Slot);
						}
					}
				}
				AddInfo(FString::Printf(TEXT("CB %d %d %d %lld %lld %lld %lld %lld %lld %s"), SeedIndex, X, Z,
					FMath::RoundToInt64(Climate.Values[0] * 10000), FMath::RoundToInt64(Climate.Values[1] * 10000), FMath::RoundToInt64(Climate.Values[2] * 10000),
					FMath::RoundToInt64(Climate.Values[3] * 10000), FMath::RoundToInt64(Climate.Values[4] * 10000), FMath::RoundToInt64(Climate.Values[5] * 10000),
					*Registry.GetName(Source.Find(Climate, Hint)).ToString()));
			}
		}
	}
	for (const TPair<int32, int32>& Pair : DeepDark)
	{
		AddInfo(FString::Printf(TEXT("CBDY %d %d %d"), Pair.Key / 100, -8 - (Pair.Key % 100) * 8, Pair.Value));
	}
	// The Nether's biomes on the same grid (CBN lines), against `log cbiomes ... 64 nether` on the reference.
	{
		FMcNoiseSettings NetherSettings;
		FMcBiomeSource NetherSource;
		if (TestTrue(TEXT("nether setup"), FMcNoiseSettings::Load(TEXT("minecraft:nether"), NetherSettings) && NetherSource.Load(TEXT("minecraft:nether"))))
		{
			for (int32 SeedIndex = 0; SeedIndex < 5; ++SeedIndex)
			{
				const FMcNoiseGenerator NetherGenerator(NetherSettings, Seeds[SeedIndex], TEXT("minecraft:nether"));
				for (int32 J = 0; J < 64; ++J)
				{
					for (int32 I = 0; I < 64; ++I)
					{
						const int32 X = -8192 + I * 256;
						const int32 Z = -8192 + J * 256;
						const FMcClimate Climate = NetherGenerator.SampleClimate(X, 64, Z);
						int32 Hint = INDEX_NONE;
						AddInfo(FString::Printf(TEXT("CBN %d %d %d %s"), SeedIndex, X, Z, *Registry.GetName(NetherSource.Find(Climate, Hint)).ToString()));
					}
				}
			}
		}
	}
	// Is the middle 4 km (where the structure starts are counted) as oceanic as the rest? Many seeds, deep ocean shares.
	TArray<double> Shares;
	for (int32 K = 0; K < 60; ++K)
	{
		const FMcNoiseGenerator Generator(Settings, 7919ull * K + 13ull, TEXT("minecraft:overworld"));
		int32 Deep = 0;
		for (int32 J = 0; J < 16; ++J)
		{
			for (int32 I = 0; I < 16; ++I)
			{
				int32 Hint = INDEX_NONE;
				const FString Name = Registry.GetName(Source.Find(Generator.SampleClimate(-2048 + I * 256, 64, -2048 + J * 256), Hint)).ToString();
				Deep += Name.StartsWith(TEXT("deep_")) || Name.Contains(TEXT("deep_")) ? 1 : 0;
			}
		}
		Shares.Add(Deep / 256.0 * 100.0);
	}
	Shares.Sort();
	AddInfo(FString::Printf(TEXT("CENTRE deep ocean share over 60 seeds: min %.1f p10 %.1f p25 %.1f median %.1f p75 %.1f p90 %.1f max %.1f"),
		Shares[0], Shares[6], Shares[15], Shares[30], Shares[45], Shares[54], Shares[59]));
	return true;
}

// The same statistics for the patches listed in Content/Tests/statsat_ours.txt ("seed first-chunk-x first-chunk-z" per
// line): patches picked where this world has mangrove swamp or frozen river, to compare with the reference's `log statslist`.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWorldStatsAtTest, "McClient.McGen.WorldStatsAt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWorldStatsAtTest::RunTest(const FString& Parameters)
{
	FString Text;
	if (!TestTrue(TEXT("patch list"), FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Tests/statsat_ours.txt")))))
	{
		return false;
	}
	TArray<FString> Rows;
	Text.ParseIntoArrayLines(Rows);
	for (int32 SeedIndex = 0; SeedIndex < 5; ++SeedIndex)
	{
		TArray<FIntPoint> Corners;
		for (const FString& Row : Rows)
		{
			TArray<FString> Parts;
			Row.ParseIntoArrayWS(Parts);
			if (Parts.Num() >= 3 && FCString::Atoi(*Parts[0]) == SeedIndex)
			{
				Corners.Add(FIntPoint(FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2])));
			}
		}
		if (!Corners.IsEmpty() && !RunWorldStats(*this, SeedIndex, &Corners))
		{
			return false;
		}
	}
	return true;
}

// Nether block counts on the patches `log nstats -256 -256 64 8` reads on the reference (8 * 8 patches of 4 * 4 chunks, 64
// chunks apart), per 8-block band and per biome at y 64: NSTAT A / B / C lines in the reference's format.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcNetherStatsTest, "McClient.McGen.NetherStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

static bool RunNetherStats(FAutomationTestBase& Test, bool bNoDecor)
{
	FMcNoiseSettings Settings;
	if (!Test.TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:nether"), Settings)))
	{
		return false;
	}
	const uint64 Seeds[] = { 4242424242ull, 1001ull, static_cast<uint64>(-77777ll), 909090909ull, 31415926535ull };
	const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	FString SeedFilter;
	FParse::Value(FCommandLine::Get(), TEXT("McNetherSeed="), SeedFilter);
	for (int32 SeedIndex = 0; SeedIndex < 5; ++SeedIndex)
	{
		if (!SeedFilter.IsEmpty() && FCString::Atoi(*SeedFilter) != SeedIndex)
		{
			continue;
		}
		const TSharedRef<FMcNoiseGenerator> Generator = MakeShared<FMcNoiseGenerator>(Settings, Seeds[SeedIndex], TEXT("minecraft:nether"));
		FMcFeatureLibrary Features(Settings.SeaLevel, Settings.MinY, Settings.Height);
		Features.LoadBiomes();
		Features.SetHeightSource([Generator](int32 X, int32 Z) { return Generator->FindSurfaceY(X, Z) + 1; });
		Features.SetBiomeSource([Generator](int32 X, int32 Y, int32 Z) { return Generator->GetBiome(X, Y, Z); });
		TMap<FString, TArray<int64>> Bands;
		TMap<FString, int64> PerBiome, Columns, LavaAbove;
		bMcFeatureStatsOn = true;
		McFeatureStats.Reset();
		for (int32 PatchZ = 0; PatchZ < 8; ++PatchZ)
		{
			for (int32 PatchX = 0; PatchX < 8; ++PatchX)
			{
				const FIntPoint Corner(-256 + PatchX * 64, -256 + PatchZ * 64);
				constexpr int32 Side = 6;
				TArray<TUniquePtr<FMcChunk>> Chunks;
				for (int32 Index = 0; Index < Side * Side; ++Index)
				{
					Chunks.Add(MakeUnique<FMcChunk>(FIntPoint(Corner.X - 1 + Index % Side, Corner.Y - 1 + Index / Side), Settings.MinY, Settings.Height / 16));
					Generator->Generate(*Chunks.Last());
				}
				auto Around = [&](int32 CX, int32 CZ, FMcChunk** Out)
				{
					for (int32 Index = 0; Index < 9; ++Index)
					{
						Out[Index] = Chunks[(CZ - 1 + Index / 3) * Side + (CX - 1 + Index % 3)].Get();
					}
				};
				for (int32 CZ = 1; CZ < Side - 1; ++CZ)
				{
					for (int32 CX = 1; CX < Side - 1; ++CX)
					{
						FMcChunk* Nine[9];
						Around(CX, CZ, Nine);
						FMcGenRegion Region(FIntPoint(Corner.X - 1 + CX, Corner.Y - 1 + CZ), Nine);
						if (!bNoDecor)
						{
							Features.Decorate(Region, Seeds[SeedIndex]);
						}
					}
				}
				for (int32 CZ = 1; CZ < Side - 1; ++CZ)
				{
					for (int32 CX = 1; CX < Side - 1; ++CX)
					{
						const FMcChunk& Chunk = *Chunks[CZ * Side + CX];
						for (int32 LZ = 0; LZ < 16; ++LZ)
						{
							for (int32 LX = 0; LX < 16; ++LX)
							{
								const FString Biome = FMcBiomeRegistry::Get().GetName(Generator->GetBiome(Chunk.GetPos().X * 16 + LX, 64, Chunk.GetPos().Y * 16 + LZ)).ToString();
								++Columns.FindOrAdd(Biome);
								for (int32 Y = 0; Y < 128; ++Y)
								{
									const FString Name = Blocks.GetBlockOf(Chunk.GetBlock(LX, Y, LZ)).Name.ToString();
									TArray<int64>& Band = Bands.FindOrAdd(Name);
									Band.SetNumZeroed(16);
									++Band[Y >> 3];
									++PerBiome.FindOrAdd(Biome + TEXT(" ") + Name);
									if (Y >= 32 && Name == TEXT("minecraft:lava"))
									{
										++LavaAbove.FindOrAdd(Biome);
									}
								}
							}
						}
					}
				}
			}
		}
		for (const TPair<FString, TArray<int64>>& Pair : Bands)
		{
			for (int32 Band = 0; Band < 16; ++Band)
			{
				if (Pair.Value[Band] > 0)
				{
					Test.AddInfo(FString::Printf(TEXT("NSTAT %d A %d %s %lld"), SeedIndex, Band, *Pair.Key, Pair.Value[Band]));
				}
			}
		}
		for (const TPair<FString, int64>& Pair : PerBiome)
		{
			Test.AddInfo(FString::Printf(TEXT("NSTAT %d B %s %lld"), SeedIndex, *Pair.Key, Pair.Value));
		}
		for (const TPair<FString, TPair<int64, int64>>& Pair : McFeatureStats)
		{
			Test.AddInfo(FString::Printf(TEXT("NSTAT %d F %s %lld %lld"), SeedIndex, *Pair.Key, Pair.Value.Key, Pair.Value.Value));
		}
		bMcFeatureStatsOn = false;
		for (const TPair<FString, int64>& Pair : LavaAbove)
		{
			Test.AddInfo(FString::Printf(TEXT("NSTAT %d D %s %lld"), SeedIndex, *Pair.Key, Pair.Value));
		}
		for (const TPair<FString, int64>& Pair : Columns)
		{
			Test.AddInfo(FString::Printf(TEXT("NSTAT %d C %s %lld"), SeedIndex, *Pair.Key, Pair.Value));
		}
	}
	return true;
}

bool FMcNetherStatsTest::RunTest(const FString& Parameters)
{
	return RunNetherStats(*this, false);
}

// The same counts with terrain and carvers only (no decoration), to see which blocks the features add.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcNetherStatsTerrainTest, "McClient.McGen.NetherStatsTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcNetherStatsTerrainTest::RunTest(const FString& Parameters)
{
	return RunNetherStats(*this, true);
}

// Structure starts in the Nether on the 256 x 256 chunks from (-128, -128), the same cells as `log starts -128 -128 256 nether`
// on the reference: STARTSN <seed index> <structure> <count>. The biome is taken at y 64.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcNetherStartsTest, "McClient.McGen.NetherStarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcNetherStartsTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:nether"), Settings)))
	{
		return false;
	}
	FMcStructureLibrary Structures(Settings.SeaLevel, Settings.MinY);
	if (!TestTrue(TEXT("structure sets load"), Structures.Load()))
	{
		return false;
	}
	const uint64 Seeds[] = { 4242424242ull, 1001ull, static_cast<uint64>(-77777ll), 909090909ull, 31415926535ull };
	for (int32 SeedIndex = 0; SeedIndex < 5; ++SeedIndex)
	{
		const uint64 Seed = Seeds[SeedIndex];
		const FMcNoiseGenerator Generator(Settings, Seed, TEXT("minecraft:nether"));
		Structures.SetHeightSource([&Generator](int32 X, int32 Z) { return 64; });
		Structures.SetBiomeSource([&Generator](int32 X, int32 Y, int32 Z) { return Generator.GetBiome(X, Y, Z); });
		TMap<FName, int64> Counts, Candidates;
		for (int32 Z = -128; Z < 128; ++Z)
		{
			for (int32 X = -128; X < 128; ++X)
			{
				TArray<FName> Here;
				Structures.GetCandidates(Seed, FIntPoint(X, Z), Here);
				if (Here.IsEmpty())
				{
					continue;
				}
				for (const FName Name : Here)
				{
					Candidates.FindOrAdd(Name)++;
				}
				const FMcBiomeId Biome = Generator.GetBiome(X * 16 + 8, 64, Z * 16 + 8);
				Here.Reset();
				Structures.GetCandidates(Seed, FIntPoint(X, Z), Biome, Here);
				for (const FName Name : Here)
				{
					Counts.FindOrAdd(Name)++;
				}
			}
		}
		for (const TPair<FName, int64>& Pair : Candidates)
		{
			AddInfo(FString::Printf(TEXT("STARTSNALL %d %s %lld"), SeedIndex, *Pair.Key.ToString(), Pair.Value));
		}
		for (const TPair<FName, int64>& Pair : Counts)
		{
			AddInfo(FString::Printf(TEXT("STARTSN %d %s %lld"), SeedIndex, *Pair.Key.ToString(), Pair.Value));
		}
	}
	return true;
}

// The ore vein density functions on the grid `log veinnoise -4096 -4096 64 64 30` reads on the reference (seed 4242424242):
// VN <seed index> <x> <z> toggle mask gap copper iron richness, each times 10000. Compared by distribution, not point by point.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcVeinNoiseTest, "McClient.McGen.VeinNoise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcVeinNoiseTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	const uint64 Seeds[] = { 4242424242ull, 1001ull, static_cast<uint64>(-77777ll), 909090909ull, 31415926535ull };
	for (int32 SeedIndex = 0; SeedIndex < 5; ++SeedIndex)
	{
		FMcDensityGraph Graph(Seeds[SeedIndex]);
		const TCHAR* Names[] = { TEXT("toggle"), TEXT("mask"), TEXT("gap"), TEXT("copper_density"), TEXT("iron_density"), TEXT("richness") };
		int32 Nodes[6];
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Nodes[Index] = Graph.Parse(MakeShared<FJsonValueString>(FString(TEXT("minecraft:overworld/ore_vein/")) + Names[Index]));
			if (!TestTrue(TEXT("vein function parses"), Nodes[Index] != INDEX_NONE))
			{
				return false;
			}
		}
		FMcDensityContext Context(Graph);
		for (int32 J = 0; J < 64; ++J)
		{
			for (int32 I = 0; I < 64; ++I)
			{
				const int32 X = -4096 + I * 64;
				const int32 Z = -4096 + J * 64;
				Context.SetPos(X, 30, Z);
				FString Row;
				for (int32 Index = 0; Index < 6; ++Index)
				{
					Row += FString::Printf(TEXT(" %lld"), FMath::RoundToInt64(Graph.Evaluate(Nodes[Index], Context) * 10000.0));
				}
				AddInfo(FString::Printf(TEXT("VN %d %d %d%s"), SeedIndex, X, Z, *Row));
			}
		}
	}
		// Per-height summaries: VNY <seed> <y> <cells> <copper>0> <iron>0> <gap<0> <sum richness x10000>
		for (int32 SeedIndex = 0; SeedIndex < 5; ++SeedIndex)
		{
			FMcDensityGraph Graph(Seeds[SeedIndex]);
			const TCHAR* Names[] = { TEXT("gap"), TEXT("copper_density"), TEXT("iron_density"), TEXT("richness") };
			int32 Nodes[4];
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Nodes[Index] = Graph.Parse(MakeShared<FJsonValueString>(FString(TEXT("minecraft:overworld/ore_vein/")) + Names[Index]));
			}
			FMcDensityContext Context(Graph);
			for (const int32 Y : { -50, -30, -10, 5, 15, 30, 45 })
			{
				int32 Copper = 0;
				int32 Iron = 0;
				int32 Gap = 0;
				double Richness = 0.0;
				for (int32 J = 0; J < 64; ++J)
				{
					for (int32 I = 0; I < 64; ++I)
					{
						Context.SetPos(-4096 + I * 64, Y, -4096 + J * 64);
						Gap += Graph.Evaluate(Nodes[0], Context) < 0.0 ? 1 : 0;
						Copper += Graph.Evaluate(Nodes[1], Context) > 0.0 ? 1 : 0;
						Iron += Graph.Evaluate(Nodes[2], Context) > 0.0 ? 1 : 0;
						Richness += Graph.Evaluate(Nodes[3], Context);
					}
				}
				AddInfo(FString::Printf(TEXT("VNY %d %d 4096 %d %d %d %.0f"), SeedIndex, Y, Copper, Iron, Gap, Richness * 10000.0));
			}
		}
	return true;
}

// Deep dark counts at y -27 on a wide grid (-24576 + 384 * i, 128 * 128 points) for the five survey seeds and 25 more, to compare
// with `log cbiomes -24576 -24576 384 128 -27` on the five reference worlds: DDW <seed index> <deep_dark points> <points>.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDeepDarkWideTest, "McClient.McGen.DeepDarkWide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDeepDarkWideTest::RunTest(const FString& Parameters)
{
	FMcNoiseSettings Settings;
	FMcBiomeSource Source;
	if (!TestTrue(TEXT("setup"), FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded() && FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings) && Source.Load(TEXT("minecraft:overworld"))))
	{
		return false;
	}
	const FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	const uint64 Seeds[] = { 4242424242ull, 1001ull, static_cast<uint64>(-77777ll), 909090909ull, 31415926535ull };
	for (int32 SeedIndex = 0; SeedIndex < 30; ++SeedIndex)
	{
		const uint64 Seed = SeedIndex < 5 ? Seeds[SeedIndex] : 7919ull * SeedIndex + 13ull;
		const FMcNoiseGenerator Generator(Settings, Seed, TEXT("minecraft:overworld"));
		int32 Deep = 0;
		for (int32 J = 0; J < 128; ++J)
		{
			for (int32 I = 0; I < 128; ++I)
			{
				int32 Hint = INDEX_NONE;
				Deep += Registry.GetName(Source.Find(Generator.SampleClimate(-24576 + I * 384, -27, -24576 + J * 384), Hint)) == FName(TEXT("minecraft:deep_dark")) ? 1 : 0;
			}
		}
		AddInfo(FString::Printf(TEXT("DDW %d %d %d"), SeedIndex, Deep, 128 * 128));
	}
	return true;
}
