#include "McBiomeSource.h"
#include "Dom/JsonObject.h"
#include "McCache.h"
#include "McGen.h"

namespace
{
	// The report's parameter names, in FMcClimate's order.
	const TCHAR* const ParameterNames[] = {
		TEXT("temperature"), TEXT("humidity"), TEXT("continentalness"), TEXT("erosion"), TEXT("depth"), TEXT("weirdness"),
	};

	constexpr int32 MaxLeafSize = 8;
}

double FMcBiomeSource::FBox::Distance(const double Point[NumAxes]) const
{
	double Sum = 0.0;
	for (int32 Axis = 0; Axis < NumAxes; ++Axis)
	{
		const double Below = Min[Axis] - Point[Axis];
		const double Above = Point[Axis] - Max[Axis];
		const double Outside = Below > 0.0 ? Below : (Above > 0.0 ? Above : 0.0);
		Sum += Outside * Outside;
	}
	return Sum;
}

bool FMcBiomeSource::LoadEnd()
{
	const TCHAR* const Names[5] = { TEXT("minecraft:the_end"), TEXT("minecraft:end_highlands"), TEXT("minecraft:end_midlands"),
		TEXT("minecraft:small_end_islands"), TEXT("minecraft:end_barrens") };
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const TOptional<FMcBiomeId> Id = FMcBiomeRegistry::Get().Find(FName(Names[Index]));
		if (!Id.IsSet())
		{
			UE_LOG(LogMcGen, Error, TEXT("The End's biome %s isn't in the registry"), Names[Index]);
			return false;
		}
		EndBiomes[Index] = Id.GetValue();
	}
	bEnd = true;
	return true;
}

FMcBiomeId FMcBiomeSource::FindEnd(double IslandHeight, int32 BlockX, int32 BlockZ) const
{
	const int64 ChunkX = BlockX >> 4;
	const int64 ChunkZ = BlockZ >> 4;
	if (ChunkX * ChunkX + ChunkZ * ChunkZ <= 4096)
	{
		return EndBiomes[0];
	}
	return IslandHeight > 0.25 ? EndBiomes[1] : IslandHeight >= -0.0625 ? EndBiomes[2] : IslandHeight >= -0.21875 ? EndBiomes[3] : EndBiomes[4];
}

bool FMcBiomeSource::Load(const FString& Preset)
{
	const FString Id = McCache::Namespaced(Preset);
	const FString Path = McCache::Report(TEXT("biome_parameters/") + Id.Replace(TEXT(":"), TEXT("/")) + TEXT(".json"));
	const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(Path);
	const TArray<TSharedPtr<FJsonValue>>* List;
	if (!Json.IsValid() || !Json->TryGetArrayField(TEXT("biomes"), List))
	{
		UE_LOG(LogMcGen, Error, TEXT("Biome parameters %s not found"), *Path);
		return false;
	}

	const FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	Entries.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		const TSharedPtr<FJsonObject> Object = Value->AsObject();
		const TSharedPtr<FJsonObject>* Parameters;
		if (!Object.IsValid() || !Object->TryGetObjectField(TEXT("parameters"), Parameters))
		{
			continue;
		}
		const TOptional<FMcBiomeId> Biome = Registry.Find(FName(*Object->GetStringField(TEXT("biome"))));
		if (!Biome.IsSet())
		{
			UE_LOG(LogMcGen, Warning, TEXT("Biome parameters name an unknown biome %s"), *Object->GetStringField(TEXT("biome")));
			continue;
		}
		FEntry Entry;
		Entry.Biome = Biome.GetValue();
		for (int32 Axis = 0; Axis < 6; ++Axis)
		{
			// A range [min, max] or a single number.
			const TSharedPtr<FJsonValue> Range = (*Parameters)->TryGetField(ParameterNames[Axis]);
			if (Range.IsValid() && Range->Type == EJson::Array && Range->AsArray().Num() == 2)
			{
				Entry.Box.Min[Axis] = static_cast<float>(Range->AsArray()[0]->AsNumber());
				Entry.Box.Max[Axis] = static_cast<float>(Range->AsArray()[1]->AsNumber());
			}
			else
			{
				const float Number = Range.IsValid() ? static_cast<float>(Range->AsNumber()) : 0.0f;
				Entry.Box.Min[Axis] = Number;
				Entry.Box.Max[Axis] = Number;
			}
		}
		// The offset is a distance every lookup pays: a point at 0 against a range at the offset.
		const float Offset = static_cast<float>((*Parameters)->GetNumberField(TEXT("offset")));
		Entry.Box.Min[6] = Offset;
		Entry.Box.Max[6] = Offset;
		Entries.Add(Entry);
	}
	if (Entries.IsEmpty())
	{
		UE_LOG(LogMcGen, Error, TEXT("Biome parameters %s list no biomes"), *Path);
		return false;
	}

	Nodes.Reset();
	Root = Build(0, Entries.Num());
	return true;
}

int32 FMcBiomeSource::Build(int32 First, int32 Count)
{
	FNode Node;
	for (int32 Axis = 0; Axis < NumAxes; ++Axis)
	{
		Node.Bounds.Min[Axis] = TNumericLimits<float>::Max();
		Node.Bounds.Max[Axis] = TNumericLimits<float>::Lowest();
	}
	for (int32 Index = First; Index < First + Count; ++Index)
	{
		for (int32 Axis = 0; Axis < NumAxes; ++Axis)
		{
			Node.Bounds.Min[Axis] = FMath::Min(Node.Bounds.Min[Axis], Entries[Index].Box.Min[Axis]);
			Node.Bounds.Max[Axis] = FMath::Max(Node.Bounds.Max[Axis], Entries[Index].Box.Max[Axis]);
		}
	}
	if (Count <= MaxLeafSize)
	{
		Node.bLeaf = true;
		Node.First = First;
		Node.Count = Count;
		return Nodes.Add(Node);
	}

	// Split in half along the axis where the entries' middles spread furthest.
	int32 SplitAxis = 0;
	float BestSpread = -1.0f;
	for (int32 Axis = 0; Axis < NumAxes; ++Axis)
	{
		float Low = TNumericLimits<float>::Max();
		float High = TNumericLimits<float>::Lowest();
		for (int32 Index = First; Index < First + Count; ++Index)
		{
			const float Middle = (Entries[Index].Box.Min[Axis] + Entries[Index].Box.Max[Axis]) * 0.5f;
			Low = FMath::Min(Low, Middle);
			High = FMath::Max(High, Middle);
		}
		if (High - Low > BestSpread)
		{
			BestSpread = High - Low;
			SplitAxis = Axis;
		}
	}
	TArrayView<FEntry>(Entries.GetData() + First, Count).StableSort([SplitAxis](const FEntry& A, const FEntry& B)
	{
		return A.Box.Min[SplitAxis] + A.Box.Max[SplitAxis] < B.Box.Min[SplitAxis] + B.Box.Max[SplitAxis];
	});

	const int32 Half = Count / 2;
	const int32 Left = Build(First, Half);
	const int32 Right = Build(First + Half, Count - Half);
	// Children are stored next to each other so a node can name them by first and count.
	const FNode LeftNode = Nodes[Left];
	const FNode RightNode = Nodes[Right];
	Node.First = Nodes.Num() + 1;
	Node.Count = 2;
	const int32 Index = Nodes.Add(Node);
	Nodes.Add(LeftNode);
	Nodes.Add(RightNode);
	return Index;
}

FMcBiomeId FMcBiomeSource::Find(const FMcClimate& Climate, int32& InOutHint) const
{
	if (Root == INDEX_NONE)
	{
		return 0;
	}
	double Point[NumAxes];
	for (int32 Axis = 0; Axis < 6; ++Axis)
	{
		Point[Axis] = Climate.Values[Axis];
	}
	Point[6] = 0.0;

	int32 Best = Entries.IsValidIndex(InOutHint) ? InOutHint : 0;
	double BestDistance = Entries[Best].Box.Distance(Point);

	// Depth first, nearer child first, skipping anything that can't beat the best so far.
	TArray<int32, TInlineAllocator<64>> Stack;
	Stack.Add(Root);
	while (!Stack.IsEmpty())
	{
		const FNode& Node = Nodes[Stack.Pop(EAllowShrinking::No)];
		if (Node.Bounds.Distance(Point) >= BestDistance)
		{
			continue;
		}
		if (Node.bLeaf)
		{
			for (int32 Index = Node.First; Index < Node.First + Node.Count; ++Index)
			{
				const double Distance = Entries[Index].Box.Distance(Point);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Index;
				}
			}
			continue;
		}
		const int32 A = Node.First;
		const int32 B = Node.First + 1;
		const bool bAFirst = Nodes[A].Bounds.Distance(Point) <= Nodes[B].Bounds.Distance(Point);
		Stack.Add(bAFirst ? B : A);
		Stack.Add(bAFirst ? A : B);
	}
	InOutHint = Best;
	return Entries[Best].Biome;
}

FMcBiomeId FMcBiomeSource::FindSlow(const FMcClimate& Climate) const
{
	double Point[NumAxes];
	for (int32 Axis = 0; Axis < 6; ++Axis)
	{
		Point[Axis] = Climate.Values[Axis];
	}
	Point[6] = 0.0;
	int32 Best = 0;
	double BestDistance = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const double Distance = Entries[Index].Box.Distance(Point);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Entries.IsEmpty() ? 0 : Entries[Best].Biome;
}
