#include "McBlockModels.h"
#include "McTextureTiles.h"
#include "Async/ParallelFor.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

const TCHAR* McDirectionName(EMcDirection Direction)
{
	static const TCHAR* const Names[] = { TEXT("down"), TEXT("up"), TEXT("north"), TEXT("south"), TEXT("west"), TEXT("east"), TEXT("none") };
	return Names[static_cast<int32>(Direction)];
}

namespace
{
	constexpr int32 NumFaces = 6;
	constexpr int32 MaxProblems = 25;
	const TCHAR* const MissingTexture = TEXT("minecraft:missingno");

	EMcDirection ParseDirection(const FString& Name)
	{
		for (int32 Index = 0; Index < NumFaces; ++Index)
		{
			if (Name == McDirectionName(static_cast<EMcDirection>(Index)))
			{
				return static_cast<EMcDirection>(Index);
			}
		}
		return EMcDirection::None;
	}

	FVector3f DirectionVector(EMcDirection Direction)
	{
		switch (Direction)
		{
		case EMcDirection::Down:  return FVector3f(0, -1, 0);
		case EMcDirection::Up:    return FVector3f(0, 1, 0);
		case EMcDirection::North: return FVector3f(0, 0, -1);
		case EMcDirection::South: return FVector3f(0, 0, 1);
		case EMcDirection::West:  return FVector3f(-1, 0, 0);
		case EMcDirection::East:  return FVector3f(1, 0, 0);
		default:                  return FVector3f::ZeroVector;
		}
	}

	EMcDirection DirectionFromVector(const FVector3f& V)
	{
		const FVector3f A = V.GetAbs();
		if (A.X >= A.Y && A.X >= A.Z)
		{
			return V.X < 0 ? EMcDirection::West : EMcDirection::East;
		}
		if (A.Y >= A.Z)
		{
			return V.Y < 0 ? EMcDirection::Down : EMcDirection::Up;
		}
		return V.Z < 0 ? EMcDirection::North : EMcDirection::South;
	}

	// Exact for multiples of 90 degrees, so blockstate rotations don't pick up float noise.
	void SinCosDegrees(float Degrees, float& Sin, float& Cos)
	{
		const int32 Quarter = FMath::RoundToInt(Degrees / 90.f);
		if (FMath::IsNearlyEqual(Degrees, Quarter * 90.f))
		{
			switch (((Quarter % 4) + 4) % 4)
			{
			case 0: Sin = 0; Cos = 1; break;
			case 1: Sin = 1; Cos = 0; break;
			case 2: Sin = 0; Cos = -1; break;
			default: Sin = -1; Cos = 0; break;
			}
			return;
		}
		FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(Degrees));
	}

	// Right-handed rotation about an axis (0 = X, 1 = Y, 2 = Z). Checked against the vanilla torch data: element
	// angles use it as is. The blockstate's "x" and "y" are both the negative of it: "y" turns clockwise from
	// above, and "x": 90 takes a model's north side down (observers and pistons facing down, end rods facing north
	// have their tip at the model's top turned to the north).
	FVector3f RotateAxis(const FVector3f& V, int32 Axis, float Degrees)
	{
		float S, C;
		SinCosDegrees(Degrees, S, C);
		switch (Axis)
		{
		case 0:  return FVector3f(V.X, V.Y * C - V.Z * S, V.Y * S + V.Z * C);
		case 1:  return FVector3f(V.X * C + V.Z * S, V.Y, -V.X * S + V.Z * C);
		default: return FVector3f(V.X * C - V.Y * S, V.X * S + V.Y * C, V.Z);
		}
	}

	// The blockstate's x and y rotation about the block centre (given relative to it): x first, then y.
	FVector3f RotateVariant(const FVector3f& V, int32 RotX, int32 RotY)
	{
		return RotateAxis(RotateAxis(V, 0, -static_cast<float>(RotX)), 1, -static_cast<float>(RotY));
	}

	// The corners of a face, top-left, bottom-left, bottom-right, top-right as seen from outside, so that
	// they run counter-clockwise. "Top" is up for the sides, north for up and south for down, as in the game.
	void FaceCorners(EMcDirection Face, const FVector3f& A, const FVector3f& B, FVector3f Out[4])
	{
		switch (Face)
		{
		case EMcDirection::Down:
			Out[0] = FVector3f(A.X, A.Y, B.Z); Out[1] = FVector3f(A.X, A.Y, A.Z); Out[2] = FVector3f(B.X, A.Y, A.Z); Out[3] = FVector3f(B.X, A.Y, B.Z);
			break;
		case EMcDirection::Up:
			Out[0] = FVector3f(A.X, B.Y, A.Z); Out[1] = FVector3f(A.X, B.Y, B.Z); Out[2] = FVector3f(B.X, B.Y, B.Z); Out[3] = FVector3f(B.X, B.Y, A.Z);
			break;
		case EMcDirection::North:
			Out[0] = FVector3f(B.X, B.Y, A.Z); Out[1] = FVector3f(B.X, A.Y, A.Z); Out[2] = FVector3f(A.X, A.Y, A.Z); Out[3] = FVector3f(A.X, B.Y, A.Z);
			break;
		case EMcDirection::South:
			Out[0] = FVector3f(A.X, B.Y, B.Z); Out[1] = FVector3f(A.X, A.Y, B.Z); Out[2] = FVector3f(B.X, A.Y, B.Z); Out[3] = FVector3f(B.X, B.Y, B.Z);
			break;
		case EMcDirection::West:
			Out[0] = FVector3f(A.X, B.Y, A.Z); Out[1] = FVector3f(A.X, A.Y, A.Z); Out[2] = FVector3f(A.X, A.Y, B.Z); Out[3] = FVector3f(A.X, B.Y, B.Z);
			break;
		default:
			Out[0] = FVector3f(B.X, B.Y, B.Z); Out[1] = FVector3f(B.X, A.Y, B.Z); Out[2] = FVector3f(B.X, A.Y, A.Z); Out[3] = FVector3f(B.X, B.Y, A.Z);
			break;
		}
	}

	// A face's texture axes in model space: which way increasing U and increasing V run along the block.
	void FaceTextureAxes(EMcDirection Face, FVector3f& Right, FVector3f& Down)
	{
		FVector3f C[4];
		FaceCorners(Face, FVector3f::ZeroVector, FVector3f(1, 1, 1), C);
		Right = C[2] - C[1];
		Down = C[1] - C[0];
	}

	// What the face's uv is when the model doesn't give one: the face's own position in the block, in 1/16ths.
	void DefaultUV(EMcDirection Face, const FVector3f& A, const FVector3f& B, float Out[4])
	{
		switch (Face)
		{
		case EMcDirection::Down:  Out[0] = A.X;       Out[1] = 16 - B.Z; Out[2] = B.X;       Out[3] = 16 - A.Z; break;
		case EMcDirection::Up:    Out[0] = A.X;       Out[1] = A.Z;      Out[2] = B.X;       Out[3] = B.Z;      break;
		case EMcDirection::North: Out[0] = 16 - B.X;  Out[1] = 16 - B.Y; Out[2] = 16 - A.X;  Out[3] = 16 - A.Y; break;
		case EMcDirection::South: Out[0] = A.X;       Out[1] = 16 - B.Y; Out[2] = B.X;       Out[3] = 16 - A.Y; break;
		case EMcDirection::West:  Out[0] = A.Z;       Out[1] = 16 - B.Y; Out[2] = B.Z;       Out[3] = 16 - A.Y; break;
		default:                  Out[0] = 16 - B.Z;  Out[1] = 16 - B.Y; Out[2] = 16 - A.Z;  Out[3] = 16 - A.Y; break;
		}
	}

	struct FElementFace
	{
		bool bPresent = false;
		bool bHasUV = false;
		float UV[4] = {};
		FString Texture;
		EMcDirection CullFace = EMcDirection::None;
		int32 Rotation = 0;
		int32 TintIndex = -1;
	};

	struct FElement
	{
		FVector3f From = FVector3f::ZeroVector;
		FVector3f To = FVector3f::ZeroVector;
		int32 RotationAxis = INDEX_NONE;  // single-axis rotation: 0, 1 or 2
		float RotationAngle = 0;
		FVector3f RotationOrigin = FVector3f(8, 8, 8);
		bool bRescale = false;
		bool bEuler = false;              // the {"x","y","z"} form of rotation
		FVector3f EulerAngles = FVector3f::ZeroVector;
		bool bShade = true;
		EMcDirection ShadeOverride = EMcDirection::None;
		FElementFace Faces[NumFaces];
	};

	// A "textures" value: a string, or in 26.3 also {"sprite": ..., "force_translucent": ...}.
	struct FTextureValue
	{
		FString Value;
		bool bForceTranslucent = false;
	};

	// A model with its parent chain folded in.
	struct FResolvedModel
	{
		TArray<FElement> Elements;
		TMap<FName, FTextureValue> Textures;
		bool bAmbientOcclusion = true;
		bool bBuiltin = false;
		bool bGenerated = false;
		bool bGuiFront = false;
		TMap<FString, FMcModelDisplay> Display;
		bool bFailed = false;
		FString Error;
	};

	struct FApply
	{
		FString Model;
		int32 RotX = 0;
		int32 RotY = 0;
		bool bUvLock = false;
		int32 Weight = 1;
	};

	struct FCondition
	{
		enum class EKind : uint8 { Properties, And, Or };
		EKind Kind = EKind::Properties;
		TArray<TPair<FName, TArray<FName>>> Properties;  // every property must match one of its values
		TArray<FCondition> Children;
	};

	struct FVariantDef
	{
		TArray<TPair<FName, FName>> Match;  // empty matches every state
		TArray<FApply> Alternatives;        // weighted in the game; the baker uses the first for now
	};

	struct FPartDef
	{
		bool bHasWhen = false;
		FCondition When;
		TArray<FApply> Alternatives;
	};

	struct FBlockStateDef
	{
		bool bFailed = false;
		FString Error;
		TArray<FVariantDef> Variants;
		TArray<FPartDef> Parts;
	};

	struct FBakedApply
	{
		TArray<FMcBakedQuad> Quads;
		EMcModelStatus Status = EMcModelStatus::Failed;
	};

	FString ValueToString(const TSharedPtr<FJsonValue>& Value)
	{
		bool bValue;
		double Number;
		FString Text;
		if (Value->TryGetString(Text))
		{
			return Text;
		}
		if (Value->TryGetBool(bValue))
		{
			return bValue ? TEXT("true") : TEXT("false");
		}
		if (Value->TryGetNumber(Number))
		{
			return FString::FromInt(static_cast<int32>(Number));
		}
		return FString();
	}

	bool ReadFloats(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float* Out, int32 Count)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array;
		if (!Object->TryGetArrayField(Field, Array) || Array->Num() != Count)
		{
			return false;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			double Number;
			if (!(*Array)[Index]->TryGetNumber(Number))
			{
				return false;
			}
			Out[Index] = static_cast<float>(Number);
		}
		return true;
	}

	class FBaker
	{
	public:
		explicit FBaker(FMcBakeStats& InStats) : Stats(InStats) {}

		void AddProblem(const FString& Message)
		{
			if (Stats.Problems.Num() < MaxProblems)
			{
				Stats.Problems.AddUnique(Message);
			}
		}

		FBlockStateDef LoadBlockState(const FMcBlock& Block);
		bool Matches(const FCondition& Condition, FMcStateId State) const;
		const FBakedApply& BakeApply(const FApply& Apply);
		FMcItemModel BakeItem(const FString& ModelId);

	private:
		TSharedRef<const FResolvedModel> LoadModel(const FString& RawId, int32 Depth);
		void ParseElements(const TArray<TSharedPtr<FJsonValue>>& Elements, FResolvedModel& Model, const FString& ModelId);
		void BakeElement(const FResolvedModel& Model, const FElement& Element, const FApply& Apply, TArray<FMcBakedQuad>& Out);
		FName ResolveTexture(const FResolvedModel& Model, const FString& Reference, bool& bResolved, bool& bForceTranslucent) const;
		void ParseApplies(const TSharedPtr<FJsonValue>& Value, TArray<FApply>& Out) const;
		FCondition ParseCondition(const TSharedPtr<FJsonObject>& Object) const;

		FMcBakeStats& Stats;
		TMap<FString, TSharedRef<const FResolvedModel>> Models;
		TMap<FString, FBakedApply> BakedApplies;
	};

	void FBaker::ParseElements(const TArray<TSharedPtr<FJsonValue>>& Elements, FResolvedModel& Model, const FString& ModelId)
	{
		Model.Elements.Reset();
		for (const TSharedPtr<FJsonValue>& ElementValue : Elements)
		{
			const TSharedPtr<FJsonObject>* ElementObject;
			float Corner[3];
			if (!ElementValue->TryGetObject(ElementObject)
				|| !ReadFloats(*ElementObject, TEXT("from"), Corner, 3))
			{
				AddProblem(FString::Printf(TEXT("%s: element without from"), *ModelId));
				continue;
			}

			FElement& Element = Model.Elements.AddDefaulted_GetRef();
			Element.From = FVector3f(Corner[0], Corner[1], Corner[2]);
			if (ReadFloats(*ElementObject, TEXT("to"), Corner, 3))
			{
				Element.To = FVector3f(Corner[0], Corner[1], Corner[2]);
			}

			const TSharedPtr<FJsonObject>* RotationObject;
			if ((*ElementObject)->TryGetObjectField(TEXT("rotation"), RotationObject))
			{
				float Origin[3];
				if (ReadFloats(*RotationObject, TEXT("origin"), Origin, 3))
				{
					Element.RotationOrigin = FVector3f(Origin[0], Origin[1], Origin[2]);
				}

				FString Axis;
				double Angle = 0;
				if ((*RotationObject)->TryGetStringField(TEXT("axis"), Axis) && (*RotationObject)->TryGetNumberField(TEXT("angle"), Angle))
				{
					Element.RotationAxis = Axis == TEXT("x") ? 0 : Axis == TEXT("y") ? 1 : 2;
					Element.RotationAngle = static_cast<float>(Angle);
					(*RotationObject)->TryGetBoolField(TEXT("rescale"), Element.bRescale);
				}
				else
				{
					// The three-angle form; only the hanging sign templates use it. The order x, y, z is a guess.
					double X = 0, Y = 0, Z = 0;
					(*RotationObject)->TryGetNumberField(TEXT("x"), X);
					(*RotationObject)->TryGetNumberField(TEXT("y"), Y);
					(*RotationObject)->TryGetNumberField(TEXT("z"), Z);
					Element.bEuler = true;
					Element.EulerAngles = FVector3f(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
					++Stats.UnhandledElements;
				}
			}

			(*ElementObject)->TryGetBoolField(TEXT("shade"), Element.bShade);
			FString Override;
			if ((*ElementObject)->TryGetStringField(TEXT("shade_direction_override"), Override))
			{
				Element.ShadeOverride = ParseDirection(Override);
			}

			const TSharedPtr<FJsonObject>* FacesObject;
			if (!(*ElementObject)->TryGetObjectField(TEXT("faces"), FacesObject))
			{
				continue;
			}
			for (const auto& FaceEntry : (*FacesObject)->Values)
			{
				const EMcDirection Direction = ParseDirection(FaceEntry.Key);
				const TSharedPtr<FJsonObject>* FaceObject;
				if (Direction == EMcDirection::None || !FaceEntry.Value->TryGetObject(FaceObject))
				{
					AddProblem(FString::Printf(TEXT("%s: bad face %s"), *ModelId, *FaceEntry.Key));
					continue;
				}

				FElementFace& Face = Element.Faces[static_cast<int32>(Direction)];
				Face.bPresent = true;
				Face.bHasUV = ReadFloats(*FaceObject, TEXT("uv"), Face.UV, 4);
				(*FaceObject)->TryGetStringField(TEXT("texture"), Face.Texture);
				FString Cull;
				if ((*FaceObject)->TryGetStringField(TEXT("cullface"), Cull))
				{
					Face.CullFace = ParseDirection(Cull);
				}
				int32 Rotation;
				if ((*FaceObject)->TryGetNumberField(TEXT("rotation"), Rotation))
				{
					Face.Rotation = ((Rotation / 90) % 4 + 4) % 4 * 90;
				}
				(*FaceObject)->TryGetNumberField(TEXT("tintindex"), Face.TintIndex);
			}
		}
	}

	TSharedRef<const FResolvedModel> FBaker::LoadModel(const FString& RawId, int32 Depth)
	{
		const FString Id = McCache::Namespaced(RawId);
		if (const TSharedRef<const FResolvedModel>* Cached = Models.Find(Id))
		{
			return *Cached;
		}

		TSharedRef<FResolvedModel> Model = MakeShared<FResolvedModel>();
		const FString ModelPath = McCache::PathOf(Id);
		if (ModelPath.StartsWith(TEXT("builtin/")))
		{
			Model->bBuiltin = true;
			Model->bGenerated = ModelPath == TEXT("builtin/generated");
		}
		else if (Depth > 32)
		{
			Model->bFailed = true;
			Model->Error = FString::Printf(TEXT("%s: parent chain too deep"), *Id);
		}
		else
		{
			const FString File = McCache::AssetOf(Id, TEXT("models/"), TEXT(".json"));
			const TSharedPtr<FJsonObject> Json = FPaths::FileExists(File) ? McCache::LoadJsonObject(File) : nullptr;
			if (!Json.IsValid())
			{
				Model->bFailed = true;
				Model->Error = FString::Printf(TEXT("model %s can't be read"), *Id);
			}
			else
			{
				FString Parent;
				if (Json->TryGetStringField(TEXT("parent"), Parent))
				{
					// The child's own fields go on top of a copy of the parent's.
					*Model = *LoadModel(Parent, Depth + 1);
				}

				const TSharedPtr<FJsonObject>* TexturesObject;
				if (Json->TryGetObjectField(TEXT("textures"), TexturesObject))
				{
					for (const auto& Texture : (*TexturesObject)->Values)
					{
						FTextureValue Value;
						const TSharedPtr<FJsonObject>* Object;
						if (Texture.Value->TryGetObject(Object))
						{
							(*Object)->TryGetStringField(TEXT("sprite"), Value.Value);
							(*Object)->TryGetBoolField(TEXT("force_translucent"), Value.bForceTranslucent);
						}
						else
						{
							Value.Value = ValueToString(Texture.Value);
						}
						Model->Textures.Add(FName(*Texture.Key), MoveTemp(Value));
					}
				}
				const TArray<TSharedPtr<FJsonValue>>* Elements;
				if (Json->TryGetArrayField(TEXT("elements"), Elements))
				{
					ParseElements(*Elements, *Model, Id);
				}
				Json->TryGetBoolField(TEXT("ambientocclusion"), Model->bAmbientOcclusion);
				const TSharedPtr<FJsonObject>* DisplayObject;
				if (Json->TryGetObjectField(TEXT("display"), DisplayObject))
				{
					for (const auto& Entry : (*DisplayObject)->Values)
					{
						const TSharedPtr<FJsonObject>* Transform;
						if (!Entry.Value->TryGetObject(Transform))
						{
							continue;
						}
						FMcModelDisplay Display;
						float Values[3];
						if (ReadFloats(*Transform, TEXT("rotation"), Values, 3)) { Display.Rotation = FVector(Values[0], Values[1], Values[2]); }
						if (ReadFloats(*Transform, TEXT("translation"), Values, 3)) { Display.Translation = FVector(Values[0], Values[1], Values[2]); }
						if (ReadFloats(*Transform, TEXT("scale"), Values, 3)) { Display.Scale = FVector(Values[0], Values[1], Values[2]); }
						Model->Display.Add(Entry.Key, Display);
					}
				}
				FString GuiLight;
				if (Json->TryGetStringField(TEXT("gui_light"), GuiLight))
				{
					Model->bGuiFront = GuiLight == TEXT("front");
				}
			}
		}

		if (Model->bFailed)
		{
			AddProblem(Model->Error);
		}
		Models.Add(Id, Model);
		return Model;
	}

	FName FBaker::ResolveTexture(const FResolvedModel& Model, const FString& Reference, bool& bResolved, bool& bForceTranslucent) const
	{
		FString Current = Reference;
		bForceTranslucent = false;
		for (int32 Hop = 0; Hop < 16; ++Hop)
		{
			if (!Current.StartsWith(TEXT("#")))
			{
				bResolved = !Current.IsEmpty();
				return bResolved ? FName(*McCache::Namespaced(Current)) : FName(MissingTexture);
			}
			const FTextureValue* Next = Model.Textures.Find(FName(*Current.Mid(1)));
			if (!Next)
			{
				break;
			}
			bForceTranslucent |= Next->bForceTranslucent;
			Current = Next->Value;
		}
		bResolved = false;
		return FName(MissingTexture);
	}

	void FBaker::BakeElement(const FResolvedModel& Model, const FElement& Element, const FApply& Apply, TArray<FMcBakedQuad>& Out)
	{
		const bool bRotated = Apply.RotX != 0 || Apply.RotY != 0;

		for (int32 FaceIndex = 0; FaceIndex < NumFaces; ++FaceIndex)
		{
			const FElementFace& Face = Element.Faces[FaceIndex];
			if (!Face.bPresent)
			{
				continue;
			}
			const EMcDirection Direction = static_cast<EMcDirection>(FaceIndex);

			FMcBakedQuad& Quad = Out.AddDefaulted_GetRef();

			bool bResolved;
			Quad.Texture = ResolveTexture(Model, Face.Texture, bResolved, Quad.bForceTranslucent);
			if (!bResolved)
			{
				++Stats.UnresolvedTextures;
			}

			FVector3f Corners[4];
			FaceCorners(Direction, Element.From, Element.To, Corners);
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				FVector3f P = Corners[Corner];
				if (Element.bEuler)
				{
					P -= Element.RotationOrigin;
					P = RotateAxis(RotateAxis(RotateAxis(P, 0, Element.EulerAngles.X), 1, Element.EulerAngles.Y), 2, Element.EulerAngles.Z);
					P += Element.RotationOrigin;
				}
				else if (Element.RotationAxis != INDEX_NONE)
				{
					P -= Element.RotationOrigin;
					P = RotateAxis(P, Element.RotationAxis, Element.RotationAngle);
					if (Element.bRescale)
					{
						// Stretch the other two axes so the rotated element still spans the same block width.
						const float Scale = 1.f / FMath::Max(FMath::Cos(FMath::DegreesToRadians(Element.RotationAngle)), 0.01f);
						for (int32 Axis = 0; Axis < 3; ++Axis)
						{
							if (Axis != Element.RotationAxis)
							{
								P[Axis] *= Scale;
							}
						}
					}
					P += Element.RotationOrigin;
				}
				if (bRotated)
				{
					P = RotateVariant(P - FVector3f(8, 8, 8), Apply.RotX, Apply.RotY) + FVector3f(8, 8, 8);
				}
				Quad.Positions[Corner] = P / 16.f;
			}

			float Region[4];
			if (Face.bHasUV)
			{
				FMemory::Memcpy(Region, Face.UV, sizeof(Region));
			}
			else
			{
				DefaultUV(Direction, Element.From, Element.To, Region);
			}
			FVector2f UV[4] = { FVector2f(Region[0], Region[1]), FVector2f(Region[0], Region[3]), FVector2f(Region[2], Region[3]), FVector2f(Region[2], Region[1]) };

			// Rotating the texture shifts which corner takes which uv.
			if (Face.Rotation != 0)
			{
				const FVector2f Original[4] = { UV[0], UV[1], UV[2], UV[3] };
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					UV[Corner] = Original[(Corner + Face.Rotation / 90) % 4];
				}
			}

			EMcDirection FaceOut = Direction;
			EMcDirection CullOut = Face.CullFace;
			if (bRotated)
			{
				FaceOut = DirectionFromVector(RotateVariant(DirectionVector(Direction), Apply.RotX, Apply.RotY));
				if (CullOut != EMcDirection::None)
				{
					CullOut = DirectionFromVector(RotateVariant(DirectionVector(CullOut), Apply.RotX, Apply.RotY));
				}

				if (Apply.bUvLock)
				{
					// The texture stays put in the world: turn the uv about the middle of the texture by however
					// far the rotation turned the face's own texture axes away from that face's usual ones.
					FVector3f Right, Down, NewRight, NewDown;
					FaceTextureAxes(Direction, Right, Down);
					FaceTextureAxes(FaceOut, NewRight, NewDown);
					const FVector3f TurnedRight = RotateVariant(Right, Apply.RotX, Apply.RotY);
					const FVector3f TurnedDown = RotateVariant(Down, Apply.RotX, Apply.RotY);
					const float A = FVector3f::DotProduct(TurnedRight, NewRight);
					const float B = FVector3f::DotProduct(TurnedRight, NewDown);
					const float C = FVector3f::DotProduct(TurnedDown, NewRight);
					const float D = FVector3f::DotProduct(TurnedDown, NewDown);
					for (FVector2f& Corner : UV)
					{
						const float U = Corner.X - 8.f;
						const float V = Corner.Y - 8.f;
						Corner = FVector2f(A * U + C * V + 8.f, B * U + D * V + 8.f);
					}
				}
			}

			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Quad.UVs[Corner] = UV[Corner] / 16.f;
			}
			Quad.Face = FaceOut;
			Quad.CullFace = CullOut;
			Quad.bShade = Element.bShade;
			Quad.bAmbientOcclusion = Model.bAmbientOcclusion;
			Quad.TintIndex = static_cast<int8>(Face.TintIndex);
			Quad.ShadeFace = Element.ShadeOverride == EMcDirection::None
				? FaceOut
				: (bRotated ? DirectionFromVector(RotateVariant(DirectionVector(Element.ShadeOverride), Apply.RotX, Apply.RotY)) : Element.ShadeOverride);
		}
	}

	const FBakedApply& FBaker::BakeApply(const FApply& Apply)
	{
		const FString Key = FString::Printf(TEXT("%s|%d|%d|%d"), *Apply.Model, Apply.RotX, Apply.RotY, Apply.bUvLock ? 1 : 0);
		if (const FBakedApply* Cached = BakedApplies.Find(Key))
		{
			return *Cached;
		}

		FBakedApply& Result = BakedApplies.Add(Key);
		const TSharedRef<const FResolvedModel> Model = LoadModel(Apply.Model, 0);
		if (Model->bFailed)
		{
			Result.Status = EMcModelStatus::Failed;
			return Result;
		}

		for (const FElement& Element : Model->Elements)
		{
			BakeElement(*Model, Element, Apply, Result.Quads);
		}
		Result.Status = Result.Quads.Num() > 0 ? EMcModelStatus::Baked : Model->bBuiltin ? EMcModelStatus::Builtin : EMcModelStatus::Empty;
		return Result;
	}

	FMcItemModel FBaker::BakeItem(const FString& ModelId)
	{
		FMcItemModel Out;
		const TSharedRef<const FResolvedModel> Model = LoadModel(ModelId, 0);
		if (Model->bFailed)
		{
			Out.bFailed = true;
			return Out;
		}
		Out.bGenerated = Model->bGenerated;
		Out.bGuiFront = Model->bGuiFront;
		Out.Display = Model->Display;
		const FApply Identity{ ModelId };
		for (const FElement& Element : Model->Elements)
		{
			BakeElement(*Model, Element, Identity, Out.Quads);
		}
		if (Model->bGenerated)
		{
			for (int32 Layer = 0; Layer < 5; ++Layer)
			{
				bool bResolved, bTranslucent;
				const FName Texture = ResolveTexture(*Model, FString::Printf(TEXT("#layer%d"), Layer), bResolved, bTranslucent);
				if (!bResolved)
				{
					break;
				}
				Out.Layers.Add(Texture);
			}
		}
		return Out;
	}

	void FBaker::ParseApplies(const TSharedPtr<FJsonValue>& Value, TArray<FApply>& Out) const
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		const TArray<TSharedPtr<FJsonValue>>* Array;
		if (Value->TryGetArray(Array))
		{
			Items = *Array;
		}
		else
		{
			Items.Add(Value);
		}

		for (const TSharedPtr<FJsonValue>& Item : Items)
		{
			const TSharedPtr<FJsonObject>* Object;
			FApply Apply;
			if (Item->TryGetObject(Object) && (*Object)->TryGetStringField(TEXT("model"), Apply.Model))
			{
				(*Object)->TryGetNumberField(TEXT("x"), Apply.RotX);
				(*Object)->TryGetNumberField(TEXT("y"), Apply.RotY);
				(*Object)->TryGetBoolField(TEXT("uvlock"), Apply.bUvLock);
				(*Object)->TryGetNumberField(TEXT("weight"), Apply.Weight);
				Out.Add(MoveTemp(Apply));
			}
		}
	}

	FCondition FBaker::ParseCondition(const TSharedPtr<FJsonObject>& Object) const
	{
		FCondition Condition;
		for (const auto& Entry : Object->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>* Array;
			if ((Entry.Key == TEXT("AND") || Entry.Key == TEXT("OR")) && Entry.Value->TryGetArray(Array))
			{
				Condition.Kind = Entry.Key == TEXT("AND") ? FCondition::EKind::And : FCondition::EKind::Or;
				for (const TSharedPtr<FJsonValue>& Child : *Array)
				{
					const TSharedPtr<FJsonObject>* ChildObject;
					if (Child->TryGetObject(ChildObject))
					{
						Condition.Children.Add(ParseCondition(*ChildObject));
					}
				}
				return Condition;
			}

			// "a|b" means either value.
			TArray<FString> Parts;
			ValueToString(Entry.Value).ParseIntoArray(Parts, TEXT("|"));
			TPair<FName, TArray<FName>>& Property = Condition.Properties.Emplace_GetRef(FName(*Entry.Key), TArray<FName>());
			for (const FString& Part : Parts)
			{
				Property.Value.Add(FName(*Part));
			}
		}
		return Condition;
	}

	FBlockStateDef FBaker::LoadBlockState(const FMcBlock& Block)
	{
		FBlockStateDef Def;
		const FString File = McCache::AssetOf(Block.Name.ToString(), TEXT("blockstates/"), TEXT(".json"));
		const TSharedPtr<FJsonObject> Json = FPaths::FileExists(File) ? McCache::LoadJsonObject(File) : nullptr;
		if (!Json.IsValid())
		{
			Def.bFailed = true;
			Def.Error = FString::Printf(TEXT("blockstate %s can't be read"), *Block.Name.ToString());
			return Def;
		}

		const TSharedPtr<FJsonObject>* VariantsObject;
		if (Json->TryGetObjectField(TEXT("variants"), VariantsObject))
		{
			for (const auto& Entry : (*VariantsObject)->Values)
			{
				FVariantDef& Variant = Def.Variants.AddDefaulted_GetRef();
				TArray<FString> Pairs;
				Entry.Key.ParseIntoArray(Pairs, TEXT(","));
				for (const FString& Pair : Pairs)
				{
					FString Name, Value;
					if (Pair.Split(TEXT("="), &Name, &Value))
					{
						Variant.Match.Emplace(FName(*Name), FName(*Value));
					}
				}
				ParseApplies(Entry.Value, Variant.Alternatives);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* PartsArray;
		if (Json->TryGetArrayField(TEXT("multipart"), PartsArray))
		{
			for (const TSharedPtr<FJsonValue>& PartValue : *PartsArray)
			{
				const TSharedPtr<FJsonObject>* PartObject;
				if (!PartValue->TryGetObject(PartObject))
				{
					continue;
				}
				FPartDef& Part = Def.Parts.AddDefaulted_GetRef();
				const TSharedPtr<FJsonObject>* WhenObject;
				if ((*PartObject)->TryGetObjectField(TEXT("when"), WhenObject))
				{
					Part.bHasWhen = true;
					Part.When = ParseCondition(*WhenObject);
				}
				if ((*PartObject)->HasField(TEXT("apply")))
				{
					ParseApplies((*PartObject)->GetField<EJson::None>(TEXT("apply")), Part.Alternatives);
				}
			}
		}
		return Def;
	}

	bool FBaker::Matches(const FCondition& Condition, FMcStateId State) const
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		switch (Condition.Kind)
		{
		case FCondition::EKind::And:
			for (const FCondition& Child : Condition.Children)
			{
				if (!Matches(Child, State))
				{
					return false;
				}
			}
			return true;
		case FCondition::EKind::Or:
			for (const FCondition& Child : Condition.Children)
			{
				if (Matches(Child, State))
				{
					return true;
				}
			}
			return false;
		default:
			for (const TPair<FName, TArray<FName>>& Property : Condition.Properties)
			{
				if (!Property.Value.Contains(Registry.GetPropertyValue(State, Property.Key)))
				{
					return false;
				}
			}
			return true;
		}
	}
}

FMcItemModel McBakeItemModel(const FString& ModelId)
{
	FMcBakeStats Stats;
	FBaker Baker(Stats);
	return Baker.BakeItem(ModelId);
}

FMcBlockModels& FMcBlockModels::Get()
{
	static FMcBlockModels Instance;
	return Instance;
}

bool FMcBlockModels::EnsureBaked()
{
	check(IsInGameThread());
	if (IsBaked())
	{
		return true;
	}

	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!Registry.EnsureLoaded())
	{
		return false;
	}

	const double StartTime = FPlatformTime::Seconds();
	FMcBakeStats NewStats;
	FBaker Baker(NewStats);
	TArray<FMcBakedModel> NewModels;
	NewModels.SetNum(Registry.NumStates());
	TSet<FName> TextureSet;

	for (int32 BlockIndex = 0; BlockIndex < Registry.NumBlocks(); ++BlockIndex)
	{
		const FMcBlock& Block = Registry.GetBlock(BlockIndex);
		const FBlockStateDef Def = Baker.LoadBlockState(Block);
		if (Def.bFailed)
		{
			Baker.AddProblem(Def.Error);
		}

		for (const FMcStateId StateId : Block.States)
		{
			FMcBakedModel& Model = NewModels[StateId];
			bool bFailed = Def.bFailed;
			bool bBuiltin = false;
			bool bMatched = false;

			auto AddApply = [&](const TArray<FApply>& Alternatives)
			{
				if (Alternatives.Num() == 0)
				{
					bFailed = true;
					return;
				}
				const FBakedApply& Baked = Baker.BakeApply(Alternatives[0]);
				bFailed |= Baked.Status == EMcModelStatus::Failed;
				bBuiltin |= Baked.Status == EMcModelStatus::Builtin;
				Model.Quads.Append(Baked.Quads);
			};

			if (!Def.bFailed)
			{
				for (const FVariantDef& Variant : Def.Variants)
				{
					bool bAll = true;
					for (const TPair<FName, FName>& Pair : Variant.Match)
					{
						bAll &= Registry.GetPropertyValue(StateId, Pair.Key) == Pair.Value;
					}
					if (bAll)
					{
						bMatched = true;
						AddApply(Variant.Alternatives);
						break;
					}
				}
				if (Def.Variants.Num() == 0)
				{
					bMatched = true;  // multipart: an empty result is legitimate
					for (const FPartDef& Part : Def.Parts)
					{
						if (!Part.bHasWhen || Baker.Matches(Part.When, StateId))
						{
							AddApply(Part.Alternatives);
						}
					}
				}
				if (!bMatched)
				{
					bFailed = true;
					Baker.AddProblem(FString::Printf(TEXT("no variant matches %s"), *Registry.Describe(StateId)));
				}
			}

			Model.Status = bFailed ? EMcModelStatus::Failed
				: Model.Quads.Num() > 0 ? EMcModelStatus::Baked
				: bBuiltin ? EMcModelStatus::Builtin : EMcModelStatus::Empty;
			switch (Model.Status)
			{
			case EMcModelStatus::Baked:   ++NewStats.Baked; break;
			case EMcModelStatus::Empty:   ++NewStats.Empty; break;
			case EMcModelStatus::Builtin: ++NewStats.Builtin; break;
			default:                      ++NewStats.Failed; break;
			}
			NewStats.Quads += Model.Quads.Num();
			for (const FMcBakedQuad& Quad : Model.Quads)
			{
				TextureSet.Add(Quad.Texture);
			}
		}
	}

	// Pictures bigger than a layer are drawn a 16-pixel tile at a time: the quads using them are cut along the tiles.
	TMap<FName, FIntPoint> Grids;
	for (const FName Texture : TextureSet)
	{
		const FIntPoint Grid = McTextureTiles::GetGrid(Texture);
		if (Grid != FIntPoint(1, 1))
		{
			Grids.Add(Texture, Grid);
		}
	}
	if (Grids.Num() > 0)
	{
		ParallelFor(NewModels.Num(), [&](int32 Index)
		{
			TArray<FMcBakedQuad>& Quads = NewModels[Index].Quads;
			if (!Quads.ContainsByPredicate([&](const FMcBakedQuad& Quad) { return Grids.Contains(Quad.Texture); }))
			{
				return;
			}
			TArray<FMcBakedQuad> Cut;
			for (const FMcBakedQuad& Quad : Quads)
			{
				const FIntPoint* Grid = Grids.Find(Quad.Texture);
				McTextureTiles::SplitQuad(Quad, Grid ? *Grid : FIntPoint(1, 1), Cut);
			}
			Quads = MoveTemp(Cut);
		});
		TextureSet.Reset();
		NewStats.Quads = 0;
		for (const FMcBakedModel& Model : NewModels)
		{
			NewStats.Quads += Model.Quads.Num();
			for (const FMcBakedQuad& Quad : Model.Quads)
			{
				TextureSet.Add(Quad.Texture);
			}
		}
	}

	Textures = TextureSet.Array();
	Textures.Sort(FNameLexicalLess());
	Stats = MoveTemp(NewStats);
	Models = MoveTemp(NewModels);

	UE_LOG(LogMcAssets, Log, TEXT("Baked %d block states in %.0f ms: %d with quads (%d quads), %d empty, %d builtin, %d failed; %d textures"),
		Models.Num(), (FPlatformTime::Seconds() - StartTime) * 1000.0, Stats.Baked, Stats.Quads, Stats.Empty, Stats.Builtin, Stats.Failed, Textures.Num());
	for (const FString& Problem : Stats.Problems)
	{
		UE_LOG(LogMcAssets, Warning, TEXT("  %s"), *Problem);
	}
	return true;
}
