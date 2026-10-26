#include "McJigsaw.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McFeature.h"
#include "McGen.h"
#include "McGenRegion.h"
#include "McRandom.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using FJigsaw = FMcJigsawLibrary::FJigsaw;
	using FElement = FMcJigsawLibrary::FElement;
	using FPiece = FMcJigsawLibrary::FPiece;

	int32 Quarters(EMcRotation Rotation)
	{
		return static_cast<int32>(Rotation);
	}

	int32 Turn(int32 Direction, EMcRotation Rotation)
	{
		return Direction >= 4 ? Direction : (Direction + Quarters(Rotation)) & 3;
	}

	int32 Opposite(int32 Direction)
	{
		return Direction >= 4 ? (Direction == 4 ? 5 : 4) : (Direction + 2) & 3;
	}

	int32 DirectionOf(const FString& Name)
	{
		static const TCHAR* const Names[6] = { TEXT("north"), TEXT("east"), TEXT("south"), TEXT("west"), TEXT("up"), TEXT("down") };
		for (int32 Index = 0; Index < 6; ++Index)
		{
			if (Name == Names[Index])
			{
				return Index;
			}
		}
		return 0;
	}

	TSharedPtr<FJsonObject> LoadJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json))
		{
			return nullptr;
		}
		return Json;
	}

	FString PoolPath(FName Id)
	{
		FString Path = Id.ToString();
		Path.RemoveFromStart(TEXT("minecraft:"));
		return McCache::Data(TEXT("worldgen/template_pool/") + Path + TEXT(".json"));
	}

	// The box of a template placed with its corner at Origin and turned about it.
	void BoxOf(const FIntVector& Size, const FIntVector& Origin, EMcRotation Rotation, FIntVector& OutMin, FIntVector& OutMax)
	{
		OutMin = FIntVector(MAX_int32);
		OutMax = FIntVector(MIN_int32);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FIntVector Local((Corner & 1) ? Size.X - 1 : 0, (Corner & 2) ? Size.Y - 1 : 0, (Corner & 4) ? Size.Z - 1 : 0);
			const FIntVector World = Origin + McTemplate::TransformPos(Local, Rotation, EMcMirror::None);
			OutMin = FIntVector(FMath::Min(OutMin.X, World.X), FMath::Min(OutMin.Y, World.Y), FMath::Min(OutMin.Z, World.Z));
			OutMax = FIntVector(FMath::Max(OutMax.X, World.X), FMath::Max(OutMax.Y, World.Y), FMath::Max(OutMax.Z, World.Z));
		}
	}

	bool Overlaps(const FIntVector& MinA, const FIntVector& MaxA, const FIntVector& MinB, const FIntVector& MaxB)
	{
		return MinA.X <= MaxB.X && MaxA.X >= MinB.X && MinA.Y <= MaxB.Y && MaxA.Y >= MinB.Y && MinA.Z <= MaxB.Z && MaxA.Z >= MinB.Z;
	}

	// The pool's elements in a random order where each shows up as often as its weight (first show only).
	void Shuffled(const FMcJigsawLibrary::FPool& Pool, FMcRandom& Random, TArray<TSharedPtr<const FElement>>& Out)
	{
		TArray<TSharedPtr<const FElement>> Bag;
		for (const TPair<TSharedPtr<const FElement>, int32>& Entry : Pool.Elements)
		{
			for (int32 Copy = 0; Copy < Entry.Value; ++Copy)
			{
				Bag.Add(Entry.Key);
			}
		}
		for (int32 Index = Bag.Num() - 1; Index > 0; --Index)
		{
			Bag.Swap(Index, Random.NextInt(Index + 1));
		}
		for (const TSharedPtr<const FElement>& Element : Bag)
		{
			Out.AddUnique(Element);
		}
	}

	struct FWorldJigsaw
	{
		FIntVector Pos;
		int32 Front;
		int32 Top;
		const FJigsaw* Source;
	};

	void Rotated(const FPiece& Piece, TArray<FWorldJigsaw>& Out)
	{
		for (const FJigsaw& Jigsaw : Piece.Element->Jigsaws)
		{
			Out.Add({ Piece.Origin + McTemplate::TransformPos(Jigsaw.Pos, Piece.Rotation, EMcMirror::None), Turn(Jigsaw.Front, Piece.Rotation), Turn(Jigsaw.Top, Piece.Rotation), &Jigsaw });
		}
	}
}

FIntVector FMcJigsawLibrary::Direction(int32 Index)
{
	static const FIntVector Vectors[6] = { FIntVector(0, 0, -1), FIntVector(1, 0, 0), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0) };
	return Vectors[Index];
}

void FMcJigsawLibrary::CollectJigsaws(const FMcStructureTemplate& Template, TArray<FJigsaw>& Out)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	for (const FMcStructureTemplate::FBlock& Block : Template.Blocks)
	{
		if (!Block.bJigsaw || !Block.Nbt.IsValid() || !Template.Palettes[0].IsValidIndex(Block.Palette))
		{
			continue;
		}
		FJigsaw& Jigsaw = Out.AddDefaulted_GetRef();
		Jigsaw.Pos = Block.Pos;
		FString Orientation = Registry.GetPropertyValue(Template.Palettes[0][Block.Palette], TEXT("orientation")).ToString();
		FString Front = Orientation;
		FString Top = TEXT("up");
		Orientation.Split(TEXT("_"), &Front, &Top);
		Jigsaw.Front = DirectionOf(Front);
		Jigsaw.Top = DirectionOf(Top);
		Jigsaw.Name = FName(*Block.Nbt->GetString(TEXT("name")));
		Jigsaw.Target = FName(*Block.Nbt->GetString(TEXT("target")));
		Jigsaw.Pool = FName(*Block.Nbt->GetString(TEXT("pool")));
		Jigsaw.bRollable = Block.Nbt->GetString(TEXT("joint")) != TEXT("aligned");
		Jigsaw.Selection = static_cast<int32>(Block.Nbt->GetInt(TEXT("selection_priority")));
		Jigsaw.Placement = static_cast<int32>(Block.Nbt->GetInt(TEXT("placement_priority")));
	}
}

TSharedPtr<const FMcJigsawLibrary::FElement> FMcJigsawLibrary::ParseElement(const FJsonObject& Json, FMcFeatureLibrary* Features, TArray<FName>& Referenced)
{
	FString Type = Json.GetStringField(TEXT("element_type"));
	Type.RemoveFromStart(TEXT("minecraft:"));
	TSharedRef<FElement> Result = MakeShared<FElement>();
	FString Projection;
	Json.TryGetStringField(TEXT("projection"), Projection);
	Result->bTerrainMatching = Projection == TEXT("terrain_matching");
	if (Type == TEXT("single_pool_element") || Type == TEXT("legacy_single_pool_element"))
	{
		const FString Location = Json.GetStringField(TEXT("location"));
		Result->Template = FMcStructureTemplate::Find(Location);
		Result->Location = Location;
		if (!Result->Template.IsValid())
		{
			Missing.Add(Location);
			return Result;  // Empty
		}
		Result->Kind = FElement::EKind::Single;
		Result->Size = Result->Template->Size;
		CollectJigsaws(*Result->Template, Result->Jigsaws);
		TSharedRef<FMcProcessorList> List = MakeShared<FMcProcessorList>();
		if (const TSharedPtr<FJsonValue> Processors = Json.TryGetField(TEXT("processors")))
		{
			if (const TSharedPtr<const FMcProcessorList> Parsed = FMcProcessorList::ParseRef(Processors))
			{
				List->Append(*Parsed);
			}
		}
		if (Result->bTerrainMatching)
		{
			List->AddGravity(EMcHeightmap::WorldSurfaceWG, -1);
		}
		Result->Processors = List;
	}
	else if (Type == TEXT("feature_pool_element"))
	{
		Result->Kind = FElement::EKind::Feature;
		Result->Size = FIntVector(1, 1, 1);
		if (Features)
		{
			Result->Feature = Features->ParsePlaced(Json.TryGetField(TEXT("feature")));
		}
		// A one block piece with a single jigsaw pointing down, so it hangs under the jigsaw it's put on.
		FJigsaw& Jigsaw = Result->Jigsaws.AddDefaulted_GetRef();
		Jigsaw.Name = TEXT("minecraft:bottom");
		Jigsaw.Target = TEXT("minecraft:bottom");
		Jigsaw.Pool = TEXT("minecraft:empty");
		Jigsaw.Front = 5;
		Jigsaw.Top = 2;
		if (!Result->Feature.IsValid())
		{
			Result->Kind = FElement::EKind::Empty;
		}
	}
	else if (Type == TEXT("list_pool_element"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (Json.TryGetArrayField(TEXT("elements"), Parts))
		{
			for (const TSharedPtr<FJsonValue>& Part : *Parts)
			{
				if (const TSharedPtr<const FElement> Element = ParseElement(*Part->AsObject(), Features, Referenced))
				{
					Result->Parts.Add(Element);
				}
			}
		}
		if (Result->Parts.IsEmpty() || Result->Parts[0]->Kind != FElement::EKind::Single)
		{
			return Result;  // Empty
		}
		Result->Kind = FElement::EKind::List;
		Result->Size = Result->Parts[0]->Size;
		Result->Jigsaws = Result->Parts[0]->Jigsaws;
	}
	for (const FJigsaw& Jigsaw : Result->Jigsaws)
	{
		Referenced.AddUnique(Jigsaw.Pool);
	}
	return Result;
}

bool FMcJigsawLibrary::LoadPool(FName Id, FMcFeatureLibrary* Features)
{
	if (Id.IsNone() || Pools.Contains(Id))
	{
		return Pools.Contains(Id);
	}
	TSharedRef<FPool> Pool = MakeShared<FPool>();
	Pools.Add(Id, Pool);
	const TSharedPtr<FJsonObject> Json = LoadJson(PoolPath(Id));
	if (!Json.IsValid())
	{
		if (Id != FName("minecraft:empty"))
		{
			UE_LOG(LogMcGen, Warning, TEXT("Template pool %s can't be read"), *Id.ToString());
		}
		return false;
	}
	TArray<FName> Referenced;
	FString Fallback;
	if (Json->TryGetStringField(TEXT("fallback"), Fallback))
	{
		Pool->Fallback = FName(*Fallback);
		Referenced.Add(Pool->Fallback);
	}
	const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
	if (Json->TryGetArrayField(TEXT("elements"), Elements))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *Elements)
		{
			const TSharedPtr<FJsonObject> Object = Entry->AsObject();
			const TSharedPtr<FJsonObject>* Element = nullptr;
			if (!Object->TryGetObjectField(TEXT("element"), Element))
			{
				continue;
			}
			const TSharedPtr<const FElement> Parsed = ParseElement(**Element, Features, Referenced);
			const int32 Weight = static_cast<int32>(Object->GetNumberField(TEXT("weight")));
			if (Parsed.IsValid() && Weight > 0)
			{
				Pool->Elements.Emplace(Parsed, Weight);
			}
		}
	}
	for (const FName Reference : Referenced)
	{
		LoadPool(Reference, Features);
	}
	return true;
}

const FMcJigsawLibrary::FPool* FMcJigsawLibrary::GetPool(FName Id) const
{
	const TSharedPtr<FPool>* Found = Pools.Find(Id);
	return Found ? Found->Get() : nullptr;
}

TSharedPtr<FMcJigsawLibrary::FLayout> FMcJigsawLibrary::Assemble(const FStart& Start, FMcRandom& Random, TFunctionRef<int32(int32, int32)> Height) const
{
	const FPool* StartPool = GetPool(Start.Pool);
	if (!StartPool || StartPool->Elements.IsEmpty())
	{
		return nullptr;
	}
	// The start piece: a weighted pick, turned at random.
	int32 Total = 0;
	for (const TPair<TSharedPtr<const FElement>, int32>& Entry : StartPool->Elements)
	{
		Total += Entry.Value;
	}
	int32 Pick = Random.NextInt(Total);
	TSharedPtr<const FElement> Chosen;
	for (const TPair<TSharedPtr<const FElement>, int32>& Entry : StartPool->Elements)
	{
		if (Pick < Entry.Value)
		{
			Chosen = Entry.Key;
			break;
		}
		Pick -= Entry.Value;
	}
	if (!Chosen.IsValid() || Chosen->Kind == FElement::EKind::Empty)
	{
		return nullptr;
	}
	FPiece First;
	First.Element = Chosen;
	First.Rotation = McTemplate::RandomRotation(Random);
	FIntVector Origin = FIntVector::ZeroValue;
	const FJigsaw* Anchor = nullptr;
	if (!Start.StartJigsaw.IsNone())
	{
		for (const FJigsaw& Jigsaw : Chosen->Jigsaws)
		{
			if (Jigsaw.Name == Start.StartJigsaw)
			{
				Anchor = &Jigsaw;
				break;
			}
		}
	}
	if (Anchor)
	{
		const FIntVector Turned = McTemplate::TransformPos(Anchor->Pos, First.Rotation, EMcMirror::None);
		Origin = FIntVector(Start.CentreX - Turned.X, Start.Y - Anchor->Pos.Y, Start.CentreZ - Turned.Z);
	}
	else
	{
		FIntVector Min, Max;
		BoxOf(Chosen->Size, FIntVector::ZeroValue, First.Rotation, Min, Max);
		const int32 Width = Max.X - Min.X + 1;
		const int32 Depth = Max.Z - Min.Z + 1;
		// The piece's lowest layer is one under the ground.
		Origin = FIntVector(Start.CentreX - Width / 2 - Min.X, Start.Y - 1, Start.CentreZ - Depth / 2 - Min.Z);
	}
	First.Origin = Origin;
	BoxOf(Chosen->Size, First.Origin, First.Rotation, First.Min, First.Max);

	TSharedRef<FLayout> Layout = MakeShared<FLayout>();
	Layout->Pieces.Add(First);

	struct FQueued
	{
		int32 Piece;
		int32 Priority;
		int32 Sequence;
	};
	TArray<FQueued> Queue;
	int32 Sequence = 0;
	Queue.Add({ 0, 0, Sequence++ });

	const FIntVector BoundsMin(Start.CentreX - Start.MaxDistance, Start.MinY, Start.CentreZ - Start.MaxDistance);
	const FIntVector BoundsMax(Start.CentreX + Start.MaxDistance, Start.MaxY, Start.CentreZ + Start.MaxDistance);

	while (!Queue.IsEmpty())
	{
		int32 Best = 0;
		for (int32 Index = 1; Index < Queue.Num(); ++Index)
		{
			if (Queue[Index].Priority > Queue[Best].Priority || (Queue[Index].Priority == Queue[Best].Priority && Queue[Index].Sequence < Queue[Best].Sequence))
			{
				Best = Index;
			}
		}
		const FQueued Current = Queue[Best];
		Queue.RemoveAt(Best);
		const FPiece Parent = Layout->Pieces[Current.Piece];

		TArray<FWorldJigsaw> Sources;
		Rotated(Parent, Sources);
		for (int32 Index = Sources.Num() - 1; Index > 0; --Index)
		{
			Sources.Swap(Index, Random.NextInt(Index + 1));
		}
		Sources.StableSort([](const FWorldJigsaw& A, const FWorldJigsaw& B) { return A.Source->Selection > B.Source->Selection; });

		for (const FWorldJigsaw& Source : Sources)
		{
			const FPool* Target = GetPool(Source.Source->Pool);
			if (!Target)
			{
				continue;
			}
			const FPool* Fallback = Target->Fallback.IsNone() ? nullptr : GetPool(Target->Fallback);
			TArray<TSharedPtr<const FElement>> Candidates;
			if (Parent.Depth != Start.MaxDepth)
			{
				Shuffled(*Target, Random, Candidates);
			}
			if (Fallback)
			{
				Shuffled(*Fallback, Random, Candidates);
			}
			// Where the next piece's jigsaw has to be: beside this one, and for pieces that follow the ground, at
			// the ground.
			FIntVector SourcePos = Source.Pos;
			if (Parent.Element->bTerrainMatching)
			{
				SourcePos.Y = Height(SourcePos.X, SourcePos.Z) - 1 + Source.Source->Pos.Y;
			}
			const FIntVector Wanted = SourcePos + Direction(Source.Front);
			// A jigsaw that looks into its own piece's box (a street's side jigsaw, over the margin the street
			// reserves) puts its piece inside that box; anything else goes in the space still free.
			const bool bInside = Wanted.X >= Parent.Min.X && Wanted.X <= Parent.Max.X && Wanted.Z >= Parent.Min.Z && Wanted.Z <= Parent.Max.Z;

			bool bAttached = false;
			for (const TSharedPtr<const FElement>& Candidate : Candidates)
			{
				if (Candidate->Kind == FElement::EKind::Empty)
				{
					break;
				}
				TArray<int32> Rotations = { 0, 1, 2, 3 };
				for (int32 Index = 3; Index > 0; --Index)
				{
					Rotations.Swap(Index, Random.NextInt(Index + 1));
				}
				TArray<FJigsaw> Matches = Candidate->Jigsaws;
				for (int32 Index = Matches.Num() - 1; Index > 0; --Index)
				{
					Matches.Swap(Index, Random.NextInt(Index + 1));
				}
				for (const int32 Quarter : Rotations)
				{
					const EMcRotation Rotation = static_cast<EMcRotation>(Quarter);
					for (const FJigsaw& Match : Matches)
					{
						if (Match.Name != Source.Source->Target || Turn(Match.Front, Rotation) != Opposite(Source.Front)
							|| (!Source.Source->bRollable && Turn(Match.Top, Rotation) != Source.Top))
						{
							continue;
						}
						const FIntVector Origin2 = Wanted - McTemplate::TransformPos(Match.Pos, Rotation, EMcMirror::None);
						FIntVector Min, Max;
						BoxOf(Candidate->Size, Origin2, Rotation, Min, Max);
						if (Min.X < BoundsMin.X || Min.Y < BoundsMin.Y || Min.Z < BoundsMin.Z || Max.X > BoundsMax.X || Max.Y > BoundsMax.Y || Max.Z > BoundsMax.Z)
						{
							continue;
						}
						if (bInside && (Min.X < Parent.Min.X || Max.X > Parent.Max.X || Min.Z < Parent.Min.Z || Max.Z > Parent.Max.Z))
						{
							continue;
						}
						bool bFree = true;
						for (int32 OtherIndex = 0; OtherIndex < Layout->Pieces.Num(); ++OtherIndex)
						{
							const FPiece& Other = Layout->Pieces[OtherIndex];
							if (bInside && OtherIndex == Current.Piece)
							{
								continue;
							}
							if (Overlaps(Min, Max, Other.Min, Other.Max))
							{
								bFree = false;
								break;
							}
						}
						if (!bFree)
						{
							continue;
						}
						FPiece Child;
						Child.Element = Candidate;
						Child.Origin = Origin2;
						Child.Rotation = Rotation;
						Child.Min = Min;
						Child.Max = Max;
						Child.Depth = Parent.Depth + 1;
						Child.Parent = Current.Piece;
						Layout->Pieces.Add(Child);
						Queue.Add({ Layout->Pieces.Num() - 1, Source.Source->Placement, Sequence++ });
						bAttached = true;
						break;
					}
					if (bAttached)
					{
						break;
					}
				}
				if (bAttached)
				{
					break;
				}
			}
		}
	}
	return Layout;
}

void FMcJigsawLibrary::Place(FMcGenRegion& Region, const FLayout& Layout, FIntPoint ClipMin, FIntPoint ClipMax, const FMcRandom& Base, bool bBeard, const FMcFeatureLibrary* Features, int32 SeaLevel) const
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	for (int32 Index = 0; Index < Layout.Pieces.Num(); ++Index)
	{
		const FPiece& Piece = Layout.Pieces[Index];
		if (Piece.Max.X < ClipMin.X || Piece.Min.X > ClipMax.X || Piece.Max.Z < ClipMin.Y || Piece.Min.Z > ClipMax.Y)
		{
			continue;
		}
		FMcRandom Random = Base.Fork(Index, 77, 0);
		if (Piece.Element->Kind == FElement::EKind::Feature)
		{
			if (Piece.Element->Feature.IsValid() && Features && Piece.Origin.X >= ClipMin.X && Piece.Origin.X <= ClipMax.X && Piece.Origin.Z >= ClipMin.Y && Piece.Origin.Z <= ClipMax.Y)
			{
				FMcFeatureContext Context{ Region, Random, *Features, SeaLevel };
				Piece.Element->Feature->Place(Context, Piece.Origin);
			}
			continue;
		}
		TArray<TSharedPtr<const FElement>> Parts;
		if (Piece.Element->Kind == FElement::EKind::List)
		{
			Parts = Piece.Element->Parts;
		}
		else
		{
			Parts.Add(Piece.Element);
		}
		for (const TSharedPtr<const FElement>& Part : Parts)
		{
			if (Part->Kind != FElement::EKind::Single)
			{
				continue;
			}
			FMcTemplatePlaceSettings Settings;
			Settings.Rotation = Piece.Rotation;
			Settings.bClip = true;
			Settings.ClipMin = ClipMin;
			Settings.ClipMax = ClipMax;
			Settings.Processors = Part->Processors.Get();
			// The ground under a rigid piece: the terrain is built up to its bottom layer, with what it stands on.
			if (bBeard && !Part->bTerrainMatching)
			{
				for (const FMcStructureTemplate::FBlock& Block : Part->Template->Blocks)
				{
					const FMcStateId Old = Part->Template->Palettes[0].IsValidIndex(Block.Palette) ? Part->Template->Palettes[0][Block.Palette] : McTemplate::SkipState;
					if (Block.Pos.Y != 0 || Old == McTemplate::SkipState || Region.IsAir(Old))
					{
						continue;
					}
					const FIntVector Pos = Part->Template->ToWorld(Block.Pos, Piece.Origin, Settings);
					if (Pos.X < ClipMin.X || Pos.X > ClipMax.X || Pos.Z < ClipMin.Y || Pos.Z > ClipMax.Y)
					{
						continue;
					}
					int32 Depth = 1;
					FMcStateId Ground = McTemplate::SkipState;
					for (; Depth <= 12; ++Depth)
					{
						const FMcStateId Below = Region.GetBlock(Pos - FIntVector(0, Depth, 0));
						if (!Region.IsAir(Below) && Registry.GetBlockOf(Below).Name != FName("minecraft:water"))
						{
							Ground = Below;
							break;
						}
					}
					if (Ground == McTemplate::SkipState || Depth == 1)
					{
						continue;
					}
					// Grass and the like give way to dirt under the building.
					const FName GroundName = Registry.GetBlockOf(Ground).Name;
					if (GroundName == FName("minecraft:grass_block") || GroundName == FName("minecraft:podzol") || GroundName == FName("minecraft:mycelium"))
					{
						Ground = Registry.FindDefaultState(TEXT("minecraft:dirt")).Get(Ground);
					}
					for (int32 Fill = 1; Fill < Depth; ++Fill)
					{
						Region.SetBlock(Pos - FIntVector(0, Fill, 0), Ground);
					}
				}
			}
			Part->Template->Place(Region, Piece.Origin, Settings, Random);
		}
	}
}
