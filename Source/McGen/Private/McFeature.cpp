#include "McFeature.h"
#include "McStructures.h"
#include "Dom/JsonObject.h"
#include "McBlockPredicate.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McFeatureTypes.h"
#include "McGen.h"
#include "McGenRegion.h"
#include "McNoise.h"
#include "McProviders.h"
#include "McRandom.h"
#include "McStateProvider.h"

namespace
{
	TMap<FString, FMcFeatureLibrary::FFactory>& Types()
	{
		static TMap<FString, FMcFeatureLibrary::FFactory> Map;
		return Map;
	}

	// A feature type we haven't written: places nothing.
	class FNothing final : public FMcFeature
	{
	public:
		virtual bool Place(FMcFeatureContext&, const FIntVector&) const override { return false; }
	};

	class FCount final : public FMcPlacementModifier
	{
	public:
		FMcIntProvider Count;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			for (int32 Index = Count.Sample(Context.Random); Index > 0; --Index)
			{
				Out.Add(Pos);
			}
		}
	};

	class FInSquare final : public FMcPlacementModifier
	{
	public:
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			const int32 X = Context.Random.NextInt(16);
			const int32 Z = Context.Random.NextInt(16);
			Out.Add(Pos + FIntVector(X, 0, Z));
		}
	};

	class FHeightmap final : public FMcPlacementModifier
	{
	public:
		EMcHeightmap Heightmap = EMcHeightmap::MotionBlocking;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			const int32 Y = Context.Region.GetHeight(Heightmap, Pos.X, Pos.Z);
			if (Y > Context.Region.GetMinY())
			{
				Out.Add(FIntVector(Pos.X, Y, Pos.Z));
			}
		}
	};

	class FHeightRange final : public FMcPlacementModifier
	{
	public:
		FMcHeightProvider Height;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			Out.Add(FIntVector(Pos.X, Height.Sample(Context.Random, Context.Library.GetMinY(), Context.Library.GetHeight()), Pos.Z));
		}
	};

	class FBiomeFilter final : public FMcPlacementModifier
	{
	public:
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			if (!Context.TopLevel || Context.Library.BiomeHas(Context.Region.GetBiome(Pos), Context.TopLevel))
			{
				Out.Add(Pos);
			}
		}
	};

	class FPredicateFilter final : public FMcPlacementModifier
	{
	public:
		TSharedPtr<const FMcBlockPredicate> Predicate;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			if (Predicate->Test(Context.Region, Pos))
			{
				Out.Add(Pos);
			}
		}
	};

	class FOffset final : public FMcPlacementModifier
	{
	public:
		FMcIntProvider X;
		FMcIntProvider Y;
		FMcIntProvider Z;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			const int32 DX = X.Sample(Context.Random);
			const int32 DY = Y.Sample(Context.Random);
			const int32 DZ = Z.Sample(Context.Random);
			Out.Add(Pos + FIntVector(DX, DY, DZ));
		}
	};

	class FChance final : public FMcPlacementModifier
	{
	public:
		double Chance = 1.0;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			if (Context.Random.NextDouble() < Chance)
			{
				Out.Add(Pos);
			}
		}
	};

	class FSurfaceWaterDepth final : public FMcPlacementModifier
	{
	public:
		int32 MaxDepth = 0;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			const int32 Floor = Context.Region.GetHeight(EMcHeightmap::OceanFloor, Pos.X, Pos.Z);
			const int32 Surface = Context.Region.GetHeight(EMcHeightmap::WorldSurface, Pos.X, Pos.Z);
			if (Surface - Floor <= MaxDepth)
			{
				Out.Add(Pos);
			}
		}
	};

	class FEnvironmentScan final : public FMcPlacementModifier
	{
	public:
		int32 Step = 1;
		int32 MaxSteps = 1;
		TSharedPtr<const FMcBlockPredicate> Target;
		TSharedPtr<const FMcBlockPredicate> Allowed;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			FMcGenRegion& Region = Context.Region;
			FIntVector At = Pos;
			if (Allowed.IsValid() && !Allowed->Test(Region, At))
			{
				return;
			}
			for (int32 Index = 0; Index < MaxSteps; ++Index)
			{
				if (Target->Test(Region, At))
				{
					Out.Add(At);
					return;
				}
				At.Y += Step;
				if (At.Y < Region.GetMinY() || At.Y > Region.GetMaxY() || (Allowed.IsValid() && !Allowed->Test(Region, At)))
				{
					break;
				}
			}
			if (Target->Test(Region, At))
			{
				Out.Add(At);
			}
		}
	};

	class FCountOnEveryLayer final : public FMcPlacementModifier
	{
	public:
		FMcIntProvider Count;

		// The y just above the Layer-th floor (an air block on a solid one) down from the top, or unset.
		static TOptional<int32> FindLayer(FMcGenRegion& Region, int32 X, int32 Z, int32 Layer)
		{
			const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
			int32 Found = 0;
			bool bAboveEmpty = false;
			for (int32 Y = Region.GetHeight(EMcHeightmap::MotionBlocking, X, Z); Y > Region.GetMinY(); --Y)
			{
				const FMcStateId State = Region.GetBlock(FIntVector(X, Y - 1, Z));
				const bool bEmpty = Region.IsAir(State);
				const bool bFloor = !bEmpty && Shapes.GetFluid(State) == EMcFluid::None && FMcBlockRegistry::Get().GetBlockOf(State).Name != TEXT("minecraft:bedrock");
				if (bAboveEmpty && bFloor)
				{
					if (Found == Layer)
					{
						return Y;
					}
					++Found;
				}
				bAboveEmpty = bEmpty;
			}
			return {};
		}

		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			for (int32 Layer = 0;; ++Layer)
			{
				bool bAny = false;
				for (int32 Index = Count.Sample(Context.Random); Index > 0; --Index)
				{
					const int32 X = Pos.X + Context.Random.NextInt(16);
					const int32 Z = Pos.Z + Context.Random.NextInt(16);
					if (const TOptional<int32> Y = FindLayer(Context.Region, X, Z, Layer))
					{
						Out.Add(FIntVector(X, Y.GetValue(), Z));
						bAny = true;
					}
				}
				if (!bAny)
				{
					break;
				}
			}
		}
	};

	class FNoiseThresholdCount final : public FMcPlacementModifier
	{
	public:
		double Level = 0.0;
		int32 Below = 0;
		int32 Above = 0;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			const double Noise = Context.Library.GetInfoNoise().Sample(Pos.X / 200.0, 0.0, Pos.Z / 200.0);
			for (int32 Index = Noise < Level ? Below : Above; Index > 0; --Index)
			{
				Out.Add(Pos);
			}
		}
	};

	class FNoiseBasedCount final : public FMcPlacementModifier
	{
	public:
		int32 Ratio = 1;
		double Factor = 1.0;
		double Offset = 0.0;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			const double Noise = Context.Library.GetInfoNoise().Sample(Pos.X / Factor, 0.0, Pos.Z / Factor);
			for (int32 Index = FMath::Max(0, FMath::CeilToInt32((Noise + Offset) * Ratio)); Index > 0; --Index)
			{
				Out.Add(Pos);
			}
		}
	};

	class FSurfaceRelative final : public FMcPlacementModifier
	{
	public:
		EMcHeightmap Heightmap = EMcHeightmap::OceanFloorWG;
		int32 Min = MIN_int32;
		int32 Max = MAX_int32;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			const int64 Surface = Context.Region.GetHeight(Heightmap, Pos.X, Pos.Z);
			if (Surface + Min <= Pos.Y && Pos.Y <= Surface + Max)
			{
				Out.Add(Pos);
			}
		}
	};

	class FFixed final : public FMcPlacementModifier
	{
	public:
		TArray<FIntVector> Positions;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			for (const FIntVector& At : Positions)
			{
				if ((At.X >> 4) == (Pos.X >> 4) && (At.Z >> 4) == (Pos.Z >> 4))
				{
					Out.Add(At);
				}
			}
		}
	};

	// The blocks of a box with its corner at the position (coral mushrooms): its faces, and optionally its edges
	// (where two or three faces meet) and inside.
	class FCuboid final : public FMcPlacementModifier
	{
	public:
		FMcIntProvider XZSize;
		FMcIntProvider YSize;
		bool bEdges = true;
		bool bInterior = true;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			const FIntVector Size(XZSize.Sample(Context.Random), YSize.Sample(Context.Random), XZSize.Sample(Context.Random));
			for (int32 X = 0; X < Size.X; ++X)
			{
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					for (int32 Z = 0; Z < Size.Z; ++Z)
					{
						const int32 Faces = (X == 0 || X == Size.X - 1 ? 1 : 0) + (Y == 0 || Y == Size.Y - 1 ? 1 : 0) + (Z == 0 || Z == Size.Z - 1 ? 1 : 0);
						if ((Faces == 0 && !bInterior) || (Faces >= 2 && !bEdges))
						{
							continue;
						}
						Out.Add(Pos + FIntVector(X, Y, Z));
					}
				}
			}
		}
	};

	// One of several modifiers, picked at random each time.
	class FRandomlySelected final : public FMcPlacementModifier
	{
	public:
		TArray<TSharedPtr<const FMcPlacementModifier>> Choices;
		virtual void Apply(FMcFeatureContext& Context, const FIntVector& Pos, TArray<FIntVector>& Out) const override
		{
			Choices[Context.Random.NextInt(Choices.Num())]->Apply(Context, Pos, Out);
		}
	};

	bool ReadHeightmap(const FJsonObject& Json, EMcHeightmap& Out)
	{
		const TOptional<EMcHeightmap> Heightmap = FMcGenRegion::ParseHeightmap(Json.GetStringField(TEXT("heightmap")));
		Out = Heightmap.Get(EMcHeightmap::MotionBlocking);
		return Heightmap.IsSet();
	}
}

void FMcFeatureLibrary::RegisterType(const FString& Type, FFactory Factory)
{
	Types().Add(McCache::Namespaced(Type), MoveTemp(Factory));
}

FMcFeatureLibrary::FMcFeatureLibrary(int32 InSeaLevel, int32 InMinY, int32 InHeight)
	: SeaLevel(InSeaLevel)
	, MinY(InMinY)
	, Height(InHeight)
{
	static bool bRegistered = false;
	if (!bRegistered)
	{
		bRegistered = true;
		McRegisterBasicFeatures();
		McRegisterTreeFeatures();
	}
	FMcRandom Random(2345);
	FMcNoiseParams Params;
	Params.Amplitudes = { 1.0 };
	InfoNoise = MakeShared<FMcNoise>(Random, Params);
}

TSharedPtr<const FMcPlacedFeature> FMcFeatureLibrary::GetPlaced(const FString& Id)
{
	return ParsePlaced(MakeShared<FJsonValueString>(Id));
}

TSharedPtr<const FMcPlacedFeature> FMcFeatureLibrary::ParsePlaced(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		bErrors = true;
		return nullptr;
	}
	FString Id;
	TSharedPtr<FJsonObject> Object;
	if (Json->Type == EJson::String)
	{
		Id = McCache::Namespaced(Json->AsString());
		if (const TSharedPtr<const FMcPlacedFeature>* Found = Placed.Find(Id))
		{
			return *Found;
		}
		Object = McCache::LoadJsonObject(McCache::Data(TEXT("worldgen/placed_feature/") + McCache::PathOf(Id) + TEXT(".json")));
		if (!Object.IsValid())
		{
			UE_LOG(LogMcGen, Error, TEXT("Unknown placed feature %s"), *Id);
			bErrors = true;
			Placed.Add(Id, nullptr);
			return nullptr;
		}
	}
	else
	{
		Object = Json->AsObject();
	}

	TSharedRef<FMcPlacedFeature> Result = MakeShared<FMcPlacedFeature>();
	Result->Id = Id;
	Result->StatId = Id;
	Result->Feature = ParseFeature(Object->TryGetField(TEXT("feature")));
	if (Id.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* FeatureObject = nullptr;
		if (Object->TryGetObjectField(TEXT("feature"), FeatureObject) && FeatureObject)
		{
			Result->StatId = TEXT("inline ") + (*FeatureObject)->GetStringField(TEXT("type"));
			const TArray<TSharedPtr<FJsonValue>>* Layers = nullptr;
			if ((*FeatureObject)->TryGetArrayField(TEXT("layers"), Layers) && Layers->Num() > 0 && (*Layers)[0]->AsObject().IsValid())
			{
				const TSharedPtr<FJsonObject>* Provider = nullptr;
				FString Block;
				if ((*Layers)[0]->AsObject()->TryGetObjectField(TEXT("provider"), Provider) && (*Provider)->TryGetStringField(TEXT("id"), Block))
				{
					Result->StatId += TEXT(" ") + Block;
				}
			}
		}
	}
	bool bValid = Result->Feature.IsValid();
	const TArray<TSharedPtr<FJsonValue>>* Modifiers;
	if (bValid && Object->TryGetArrayField(TEXT("placement"), Modifiers))
	{
		for (const TSharedPtr<FJsonValue>& Modifier : *Modifiers)
		{
			TSharedPtr<const FMcPlacementModifier> Parsed = ParseModifier(Modifier);
			bValid &= Parsed.IsValid();
			Result->Modifiers.Add(Parsed);
		}
	}
	TSharedPtr<const FMcPlacedFeature> Out = bValid ? Result : TSharedPtr<FMcPlacedFeature>();
	if (!Id.IsEmpty())
	{
		Placed.Add(Id, Out);
	}
	if (!bValid)
	{
		UE_LOG(LogMcGen, Error, TEXT("Couldn't read placed feature %s"), Id.IsEmpty() ? TEXT("(inline)") : *Id);
		bErrors = true;
	}
	return Out;
}

TSharedPtr<const FMcFeature> FMcFeatureLibrary::ParseFeature(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		bErrors = true;
		return nullptr;
	}
	FString Id;
	TSharedPtr<FJsonObject> Object;
	if (Json->Type == EJson::String)
	{
		Id = McCache::Namespaced(Json->AsString());
		if (const TSharedPtr<const FMcFeature>* Found = Configured.Find(Id))
		{
			return *Found;
		}
		Object = McCache::LoadJsonObject(McCache::Data(TEXT("worldgen/feature/") + McCache::PathOf(Id) + TEXT(".json")));
		if (!Object.IsValid())
		{
			UE_LOG(LogMcGen, Error, TEXT("Unknown feature %s"), *Id);
			bErrors = true;
			Configured.Add(Id, nullptr);
			return nullptr;
		}
	}
	else
	{
		Object = Json->AsObject();
	}

	const FString Type = McCache::Namespaced(Object->GetStringField(TEXT("type")));
	TSharedPtr<const FMcFeature> Result;
	if (const FFactory* Factory = Types().Find(Type))
	{
		Result = (*Factory)(*Object, *this);
		if (!Result.IsValid())
		{
			UE_LOG(LogMcGen, Error, TEXT("Couldn't read feature %s (%s)"), Id.IsEmpty() ? TEXT("(inline)") : *Id, *Type);
			bErrors = true;
		}
	}
	else
	{
		Unsupported.FindOrAdd(Type)++;
		Result = MakeShared<FNothing>();
	}
	if (!Id.IsEmpty())
	{
		Configured.Add(Id, Result);
	}
	return Result;
}

TSharedPtr<const FMcPlacementModifier> FMcFeatureLibrary::ParseModifier(const TSharedPtr<FJsonValue>& Json)
{
	const TSharedPtr<FJsonObject> Object = Json.IsValid() && Json->Type == EJson::Object ? Json->AsObject() : nullptr;
	if (!Object.IsValid())
	{
		return nullptr;
	}
	const FString Type = McCache::Namespaced(Object->GetStringField(TEXT("type")));
	if (Type == TEXT("minecraft:count"))
	{
		TSharedRef<FCount> Result = MakeShared<FCount>();
		return Result->Count.Parse(Object->TryGetField(TEXT("count"))) ? Result : TSharedPtr<FCount>();
	}
	if (Type == TEXT("minecraft:in_square"))
	{
		return MakeShared<FInSquare>();
	}
	if (Type == TEXT("minecraft:heightmap"))
	{
		TSharedRef<FHeightmap> Result = MakeShared<FHeightmap>();
		return ReadHeightmap(*Object, Result->Heightmap) ? Result : TSharedPtr<FHeightmap>();
	}
	if (Type == TEXT("minecraft:height_range"))
	{
		TSharedRef<FHeightRange> Result = MakeShared<FHeightRange>();
		return Result->Height.Parse(Object->TryGetField(TEXT("height"))) ? Result : TSharedPtr<FHeightRange>();
	}
	if (Type == TEXT("minecraft:biome"))
	{
		return MakeShared<FBiomeFilter>();
	}
	if (Type == TEXT("minecraft:block_predicate_filter"))
	{
		TSharedRef<FPredicateFilter> Result = MakeShared<FPredicateFilter>();
		Result->Predicate = FMcBlockPredicate::Parse(Object->TryGetField(TEXT("predicate")), SeaLevel);
		return Result->Predicate.IsValid() ? Result : TSharedPtr<FPredicateFilter>();
	}
	if (Type == TEXT("minecraft:offset"))
	{
		TSharedRef<FOffset> Result = MakeShared<FOffset>();
		const bool bValid = Result->X.Parse(Object->TryGetField(TEXT("x"))) && Result->Y.Parse(Object->TryGetField(TEXT("y"))) && Result->Z.Parse(Object->TryGetField(TEXT("z")));
		return bValid ? Result : TSharedPtr<FOffset>();
	}
	if (Type == TEXT("minecraft:rarity_filter"))
	{
		TSharedRef<FChance> Result = MakeShared<FChance>();
		Result->Chance = 1.0 / FMath::Max(1, Object->GetIntegerField(TEXT("chance")));
		return Result;
	}
	if (Type == TEXT("minecraft:random_chance"))
	{
		TSharedRef<FChance> Result = MakeShared<FChance>();
		Result->Chance = Object->GetNumberField(TEXT("chance"));
		return Result;
	}
	if (Type == TEXT("minecraft:surface_water_depth_filter"))
	{
		TSharedRef<FSurfaceWaterDepth> Result = MakeShared<FSurfaceWaterDepth>();
		Result->MaxDepth = Object->GetIntegerField(TEXT("max_water_depth"));
		return Result;
	}
	if (Type == TEXT("minecraft:environment_scan"))
	{
		TSharedRef<FEnvironmentScan> Result = MakeShared<FEnvironmentScan>();
		Result->Step = Object->GetStringField(TEXT("direction_of_search")) == TEXT("up") ? 1 : -1;
		Result->MaxSteps = Object->GetIntegerField(TEXT("max_steps"));
		Result->Target = FMcBlockPredicate::Parse(Object->TryGetField(TEXT("target_condition")), SeaLevel);
		if (Object->HasField(TEXT("allowed_search_condition")))
		{
			Result->Allowed = FMcBlockPredicate::Parse(Object->TryGetField(TEXT("allowed_search_condition")), SeaLevel);
			if (!Result->Allowed.IsValid())
			{
				return nullptr;
			}
		}
		return Result->Target.IsValid() ? Result : TSharedPtr<FEnvironmentScan>();
	}
	if (Type == TEXT("minecraft:count_on_every_layer"))
	{
		TSharedRef<FCountOnEveryLayer> Result = MakeShared<FCountOnEveryLayer>();
		return Result->Count.Parse(Object->TryGetField(TEXT("count"))) ? Result : TSharedPtr<FCountOnEveryLayer>();
	}
	if (Type == TEXT("minecraft:noise_threshold_count"))
	{
		TSharedRef<FNoiseThresholdCount> Result = MakeShared<FNoiseThresholdCount>();
		Result->Level = Object->GetNumberField(TEXT("noise_level"));
		Result->Below = Object->GetIntegerField(TEXT("below_noise"));
		Result->Above = Object->GetIntegerField(TEXT("above_noise"));
		return Result;
	}
	if (Type == TEXT("minecraft:noise_based_count"))
	{
		TSharedRef<FNoiseBasedCount> Result = MakeShared<FNoiseBasedCount>();
		Result->Ratio = Object->GetIntegerField(TEXT("noise_to_count_ratio"));
		Result->Factor = Object->GetNumberField(TEXT("noise_factor"));
		Object->TryGetNumberField(TEXT("noise_offset"), Result->Offset);
		return Result;
	}
	if (Type == TEXT("minecraft:surface_relative_threshold_filter"))
	{
		TSharedRef<FSurfaceRelative> Result = MakeShared<FSurfaceRelative>();
		Object->TryGetNumberField(TEXT("min_inclusive"), Result->Min);
		Object->TryGetNumberField(TEXT("max_inclusive"), Result->Max);
		return ReadHeightmap(*Object, Result->Heightmap) ? Result : TSharedPtr<FSurfaceRelative>();
	}
	if (Type == TEXT("minecraft:fixed_placement"))
	{
		TSharedRef<FFixed> Result = MakeShared<FFixed>();
		for (const TSharedPtr<FJsonValue>& Position : Object->GetArrayField(TEXT("positions")))
		{
			Result->Positions.Add(McParseOffset(Position));
		}
		return Result;
	}
	if (Type == TEXT("minecraft:cuboid"))
	{
		TSharedRef<FCuboid> Result = MakeShared<FCuboid>();
		Object->TryGetBoolField(TEXT("include_edges"), Result->bEdges);
		Object->TryGetBoolField(TEXT("include_interior"), Result->bInterior);
		const bool bValid = Result->XZSize.Parse(Object->TryGetField(TEXT("xz_size"))) && Result->YSize.Parse(Object->TryGetField(TEXT("y_size")));
		return bValid ? Result : TSharedPtr<FCuboid>();
	}
	if (Type == TEXT("minecraft:randomly_selected"))
	{
		TSharedRef<FRandomlySelected> Result = MakeShared<FRandomlySelected>();
		for (const TSharedPtr<FJsonValue>& Choice : Object->GetArrayField(TEXT("placements")))
		{
			TSharedPtr<const FMcPlacementModifier> Parsed = ParseModifier(Choice);
			if (!Parsed.IsValid())
			{
				return nullptr;
			}
			Result->Choices.Add(Parsed);
		}
		return Result->Choices.IsEmpty() ? TSharedPtr<FRandomlySelected>() : Result;
	}
	UE_LOG(LogMcGen, Error, TEXT("Unknown placement modifier %s"), *Type);
	return nullptr;
}

bool FMcFeatureLibrary::LoadBiomes()
{
	const FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	Steps.Reset();
	Biomes.Reset();
	Temperatures.Init(0.8f, Registry.NumBiomes());
	FrozenTemperature.Init(false, Registry.NumBiomes());
	TArray<TSet<const FMcPlacedFeature*>> Seen;
	bool bOk = true;
	for (int32 Biome = 0; Biome < Registry.NumBiomes(); ++Biome)
	{
		const FString Id = Registry.GetName(static_cast<FMcBiomeId>(Biome)).ToString();
		const TSharedPtr<FJsonObject> Object = McCache::LoadJsonObject(McCache::Data(TEXT("worldgen/biome/") + McCache::PathOf(Id) + TEXT(".json")));
		if (!Object.IsValid())
		{
			bOk = false;
			continue;
		}
		double Temperature = 0.8;
		Object->TryGetNumberField(TEXT("temperature"), Temperature);
		Temperatures[Biome] = static_cast<float>(Temperature);
		FString Modifier;
		FrozenTemperature[Biome] = Object->TryGetStringField(TEXT("temperature_modifier"), Modifier) && Modifier == TEXT("frozen");
		const TArray<TSharedPtr<FJsonValue>>* StepLists;
		if (!Object->TryGetArrayField(TEXT("features"), StepLists))
		{
			continue;
		}
		for (int32 Step = 0; Step < StepLists->Num(); ++Step)
		{
			if (Steps.Num() <= Step)
			{
				Steps.SetNum(Step + 1);
				Seen.SetNum(Step + 1);
			}
			for (const TSharedPtr<FJsonValue>& Entry : (*StepLists)[Step]->AsArray())
			{
				const TSharedPtr<const FMcPlacedFeature> Feature = ParsePlaced(Entry);
				if (!Feature.IsValid())
				{
					bOk = false;
					continue;
				}
				if (!Seen[Step].Contains(Feature.Get()))
				{
					Seen[Step].Add(Feature.Get());
					Steps[Step].Add(Feature);
				}
				TBitArray<>& Bits = Biomes.FindOrAdd(Feature.Get());
				if (Bits.Num() == 0)
				{
					Bits.Init(false, Registry.NumBiomes());
				}
				Bits[Biome] = true;
			}
		}
	}
	Structures = MakeShared<FMcStructureLibrary>(SeaLevel, MinY);
	if (!Structures->Load(this))
	{
		UE_LOG(LogMcGen, Warning, TEXT("No structures loaded"));
	}
	return bOk;
}

bool FMcFeatureLibrary::BiomeHas(FMcBiomeId Biome, const FMcPlacedFeature* Feature) const
{
	const TBitArray<>* Bits = Biomes.Find(Feature);
	return Bits && Bits->IsValidIndex(Biome) && (*Bits)[Biome];
}

float FMcFeatureLibrary::GetTemperature(FMcBiomeId Biome, int32 Y) const
{
	const float Base = Temperatures.IsValidIndex(Biome) ? Temperatures[Biome] : 0.8f;
	return Y > 80 ? Base - (Y - 80) * 0.00125f : Base;
}

static constexpr float FrozenNoiseSigma = 0.4f;

float FMcFeatureLibrary::GetTemperature(FMcBiomeId Biome, int32 X, int32 Y, int32 Z) const
{
	float Temperature = GetTemperature(Biome, Y);
	if (FrozenTemperature.IsValidIndex(Biome) && FrozenTemperature[Biome])
	{
		// The "frozen" modifier moves the temperature up and down in patches (fitted to the reference client: about
		// 61% of a frozen ocean's surface freezes, and about 18% of a deep frozen ocean, whose own temperature is 0.5).
		Temperature += 0.033f + 0.42f * static_cast<float>(InfoNoise->Sample(X * 0.05, 0.0, Z * 0.05) / FrozenNoiseSigma);
	}
	return Temperature;
}

void FMcFeatureLibrary::SetHeightSource(TFunction<int32(int32, int32)> Source)
{
	if (Structures.IsValid())
	{
		Structures->SetHeightSource(MoveTemp(Source));
	}
}

void FMcFeatureLibrary::SetBiomeSource(TFunction<FMcBiomeId(int32, int32, int32)> Source)
{
	if (Structures.IsValid())
	{
		Structures->SetBiomeSource(MoveTemp(Source));
	}
}

FCriticalSection McFeatureStatsLock;
TMap<FString, TPair<int64, int64>> McFeatureStats;
bool bMcFeatureStatsOn = false;

void McCountBlocks(const TCHAR* Key, int64 Blocks)
{
	if (bMcFeatureStatsOn)
	{
		FScopeLock Lock(&McFeatureStatsLock);
		TPair<int64, int64>& Stat = McFeatureStats.FindOrAdd(FString(TEXT("BLOCKS ")) + Key, TPair<int64, int64>(0, 0));
		Stat.Key++;
		Stat.Value += Blocks;
	}
}

TMap<FString, double>* GMcDecorateProfile = nullptr;

void FMcFeatureLibrary::Decorate(FMcGenRegion& Region, uint64 Seed) const
{
	const FIntPoint Chunk = Region.GetCentre();
	// The biomes in and around the chunk, sampled every 4 blocks like biome storage.
	TBitArray<> Present(false, FMcBiomeRegistry::Get().NumBiomes());
	for (int32 X = (Chunk.X - 1) * 16; X < (Chunk.X + 2) * 16; X += 4)
	{
		for (int32 Z = (Chunk.Y - 1) * 16; Z < (Chunk.Y + 2) * 16; Z += 4)
		{
			for (int32 Y = Region.GetMinY(); Y <= Region.GetMaxY(); Y += 4)
			{
				const FMcBiomeId Biome = Region.GetBiome(FIntVector(X, Y, Z));
				if (Present.IsValidIndex(Biome))
				{
					Present[Biome] = true;
				}
			}
		}
	}

	// Structures first, like the game's structure stage before the feature steps.
	if (Structures.IsValid())
	{
		const double StructuresBegan = GMcDecorateProfile ? FPlatformTime::Seconds() : 0.0;
		Structures->Place(Region, Seed);
		if (GMcDecorateProfile)
		{
			GMcDecorateProfile->FindOrAdd(TEXT("structures")) += FPlatformTime::Seconds() - StructuresBegan;
		}
	}

	const FMcRandom Base = FMcRandom(Seed).Fork(TEXT("decoration"));
	const FIntVector Origin(Chunk.X * 16, Region.GetMinY(), Chunk.Y * 16);
	for (int32 Step = 0; Step < Steps.Num(); ++Step)
	{
		for (int32 Index = 0; Index < Steps[Step].Num(); ++Index)
		{
			const FMcPlacedFeature* Feature = Steps[Step][Index].Get();
			const TBitArray<>& Bits = Biomes.FindChecked(Feature);
			if (!TBitArray<>::BitwiseAND(Bits, Present, EBitwiseOperatorFlags::MinSize).Contains(true))
			{
				continue;
			}
			FMcRandom Random = Base.Fork(Chunk.X, Step * 4096 + Index, Chunk.Y);
			FMcFeatureContext Context{ Region, Random, *this, SeaLevel, Feature };
			const double FeatureBegan = GMcDecorateProfile ? FPlatformTime::Seconds() : 0.0;
			Feature->Place(Context, Origin);
			if (GMcDecorateProfile)
			{
				GMcDecorateProfile->FindOrAdd(Feature->StatId) += FPlatformTime::Seconds() - FeatureBegan;
			}
		}
	}
}

bool FMcPlacedFeature::Place(FMcFeatureContext& Context, const FIntVector& Origin) const
{
	TArray<FIntVector> Positions = { Origin };
	TArray<FIntVector> Next;
	for (const TSharedPtr<const FMcPlacementModifier>& Modifier : Modifiers)
	{
		Next.Reset();
		for (const FIntVector& Pos : Positions)
		{
			Modifier->Apply(Context, Pos, Next);
		}
		Swap(Positions, Next);
	}
	bool bPlaced = false;
	for (const FIntVector& Pos : Positions)
	{
		const bool bThis = Feature->Place(Context, Pos);
		bPlaced |= bThis;
		if (bMcFeatureStatsOn && !StatId.IsEmpty())
		{
			FScopeLock Lock(&McFeatureStatsLock);
			TPair<int64, int64>& Stat = McFeatureStats.FindOrAdd(StatId, TPair<int64, int64>(0, 0));
			Stat.Key++;
			Stat.Value += bThis ? 1 : 0;
		}
	}
	return bPlaced;
}

namespace McFeature
{
	bool PlacePlant(FMcGenRegion& Region, const FIntVector& Pos, FMcStateId State)
	{
		if (!FMcBlockPredicate::CanSurvive(Region, Pos, State))
		{
			return false;
		}
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		const auto InWater = [&Region, &Shapes](const FIntVector& At, FMcStateId Placed)
		{
			const FMcStateId Here = Region.GetBlock(At);
			const bool bWater = Shapes.GetFluid(Here) == EMcFluid::Water && Shapes.GetFluidLevel(Here) == 0;
			return bWater ? McWithProperty(Placed, TEXT("waterlogged"), TEXT("true")) : Placed;
		};
		if (FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("half")) == TEXT("lower"))
		{
			const FIntVector Above = Pos + FIntVector(0, 1, 0);
			const FMcStateId AboveState = Region.GetBlock(Above);
			if (!Region.IsAir(AboveState) && Shapes.GetFluid(AboveState) != EMcFluid::Water)
			{
				return false;
			}
			const FMcStateId Upper = McWithProperty(State, TEXT("half"), TEXT("upper"));
			Region.SetBlock(Pos, InWater(Pos, State));
			Region.SetBlock(Above, InWater(Above, Upper));
			return true;
		}
		return Region.SetBlock(Pos, InWater(Pos, State));
	}

	bool CanReplace(FMcStateId State)
	{
		return !FMcBlockTags::Get().Has(TEXT("minecraft:features_cannot_replace"), State);
	}
}

bool FMcFeatureLibrary::FindNearestStronghold(uint64 Seed, double X, double Z, FIntPoint& OutCorner) const
{
	FIntPoint Chunk;
	if (!Structures.IsValid() || !Structures->FindNearestRingStart(Seed, X, Z, Chunk))
	{
		return false;
	}
	OutCorner = FIntPoint(Chunk.X * 16, Chunk.Y * 16);
	return true;
}
