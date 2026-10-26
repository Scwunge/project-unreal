#include "McProceduralStructures.h"
#include "McBlockRegistry.h"
#include "McGen.h"
#include "McGenRegion.h"
#include "McRandom.h"
#include "McStructureTemplate.h"

namespace
{
	using namespace McProcedural;

	enum class EMode : uint8
	{
		Set,     // over anything but bedrock
		IfOpen,  // only into air and water
		Carve,   // over anything but liquid and bedrock
	};

	struct FBounds
	{
		FIntVector Min;
		FIntVector Max;

		bool Overlaps(const FBounds& Other) const
		{
			return Min.X <= Other.Max.X && Max.X >= Other.Min.X && Min.Y <= Other.Max.Y && Max.Y >= Other.Min.Y && Min.Z <= Other.Max.Z && Max.Z >= Other.Min.Z;
		}
	};

	uint32 Hash(int32 X, int32 Y, int32 Z, uint32 Salt = 0)
	{
		uint32 H = static_cast<uint32>(X) * 73856093u ^ static_cast<uint32>(Y) * 19349663u ^ static_cast<uint32>(Z) * 83492791u ^ Salt * 2654435761u;
		H ^= H >> 13;
		H *= 0x5bd1e995u;
		H ^= H >> 15;
		return H;
	}

	// A piece's own axes: v along the way it faces, u to its right, y up. Dir 0 faces north (-z), 1 east, 2 south,
	// 3 west; blocks drawn in it are turned to match (stairs, rails, torches written facing north).
	struct FFrame
	{
		FIntVector Origin = FIntVector::ZeroValue;
		int32 Dir = 0;

		static FIntVector Heading(int32 Index)
		{
			static const FIntVector Headings[4] = { FIntVector(0, 0, -1), FIntVector(1, 0, 0), FIntVector(0, 0, 1), FIntVector(-1, 0, 0) };
			return Headings[Index & 3];
		}
		FIntVector Forward() const { return Heading(Dir); }
		FIntVector Right() const { return Heading(Dir + 1); }
		FIntVector W(int32 U, int32 Y, int32 V) const { return Origin + Right() * U + FIntVector(0, Y, 0) + Forward() * V; }
		FFrame Child(int32 U, int32 V, int32 Turn) const { return FFrame{ W(U, 0, V), (Dir + Turn) & 3 }; }
		EMcRotation Rotation() const { return static_cast<EMcRotation>(Dir & 3); }
		FBounds Box(int32 U0, int32 Y0, int32 V0, int32 U1, int32 Y1, int32 V1) const
		{
			const FIntVector A = W(U0, Y0, V0);
			const FIntVector B = W(U1, Y1, V1);
			return FBounds{ FIntVector(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Min(A.Z, B.Z)), FIntVector(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y), FMath::Max(A.Z, B.Z)) };
		}
	};

	// Writes blocks inside the clip rectangle only, and never draws from the random stream, so every chunk sees
	// the same structure.
	struct FDraw
	{
		FEnv& Env;
		TMap<FString, FMcStateId> Cache;

		FMcStateId S(const TCHAR* Text)
		{
			const FString Key(Text);
			if (const FMcStateId* Found = Cache.Find(Key))
			{
				return *Found;
			}
			const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
			FMcStateId Result = Registry.FindDefaultState(TEXT("minecraft:air")).Get(0);
			if (const TOptional<FMcStateId> Found = Registry.ParseState(Key))
			{
				Result = *Found;
			}
			else
			{
				UE_LOG(LogMcGen, Warning, TEXT("Structure block %s is unknown"), *Key);
			}
			return Cache.Add(Key, Result);
		}

		bool IsLiquid(FMcStateId State) const
		{
			const FName Name = FMcBlockRegistry::Get().GetBlockOf(State).Name;
			return Name == FName("minecraft:water") || Name == FName("minecraft:lava");
		}

		void Put(int32 X, int32 Y, int32 Z, FMcStateId State, EMode Mode = EMode::Set)
		{
			if (X < Env.ClipMin.X || X > Env.ClipMax.X || Z < Env.ClipMin.Y || Z > Env.ClipMax.Y || Y < Env.MinY || Y > Env.MaxY)
			{
				return;
			}
			const FIntVector Pos(X, Y, Z);
			const FMcStateId Old = Env.Region.GetBlock(Pos);
			if (FMcBlockRegistry::Get().GetBlockOf(Old).Name == FName("minecraft:bedrock"))
			{
				return;
			}
			if (Mode == EMode::IfOpen && !Env.Region.IsAir(Old) && FMcBlockRegistry::Get().GetBlockOf(Old).Name != FName("minecraft:water"))
			{
				return;
			}
			if (Mode == EMode::Carve && IsLiquid(Old))
			{
				return;
			}
			Env.Region.SetBlock(Pos, State);
		}

		void Put(const FFrame& F, int32 U, int32 Y, int32 V, FMcStateId State, EMode Mode = EMode::Set)
		{
			const FIntVector P = F.W(U, Y, V);
			Put(P.X, P.Y, P.Z, McTemplate::TransformState(State, F.Rotation(), EMcMirror::None), Mode);
		}

		// A box of what Pick says for each block (given by its world position and local u, y, v).
		template <typename TPicker>
		void FillWith(const FFrame& F, int32 U0, int32 Y0, int32 V0, int32 U1, int32 Y1, int32 V1, TPicker&& Pick, EMode Mode = EMode::Set)
		{
			const FBounds Box = F.Box(U0, Y0, V0, U1, Y1, V1);
			for (int32 Y = FMath::Max(Box.Min.Y, Env.MinY); Y <= FMath::Min(Box.Max.Y, Env.MaxY); ++Y)
			{
				for (int32 Z = FMath::Max(Box.Min.Z, Env.ClipMin.Y); Z <= FMath::Min(Box.Max.Z, Env.ClipMax.Y); ++Z)
				{
					for (int32 X = FMath::Max(Box.Min.X, Env.ClipMin.X); X <= FMath::Min(Box.Max.X, Env.ClipMax.X); ++X)
					{
						const FMcStateId State = Pick(X, Y, Z);
						if (State != McTemplate::SkipState)
						{
							Put(X, Y, Z, McTemplate::TransformState(State, F.Rotation(), EMcMirror::None), Mode);
						}
					}
				}
			}
		}

		void Fill(const FFrame& F, int32 U0, int32 Y0, int32 V0, int32 U1, int32 Y1, int32 V1, FMcStateId State, EMode Mode = EMode::Set)
		{
			FillWith(F, U0, Y0, V0, U1, Y1, V1, [State](int32, int32, int32) { return State; }, Mode);
		}
	};

	FFrame North(int32 X, int32 Y, int32 Z) { return FFrame{ FIntVector(X, Y, Z), 0 }; }

	bool Within(const FBounds& Box, int32 CentreX, int32 CentreZ, int32 Radius)
	{
		return Box.Min.X >= CentreX - Radius && Box.Max.X <= CentreX + Radius && Box.Min.Z >= CentreZ - Radius && Box.Max.Z <= CentreZ + Radius;
	}

	// ---- mineshaft --------------------------------------------------------------------------------------------
	// (minecraft.wiki, "Mineshaft"): a room with timbered corridors running out of it, crossings with more
	// corridors, rooms at some ends; rails on some corridors, cobwebs, a cave spider spawner in some. Mesa
	// mineshafts are dark oak.

	struct FMineshaft
	{
		FDraw& D;
		FMcRandom& R;
		bool bMesa;
		FIntVector Centre;
		TArray<FBounds> Boxes;
		int32 Count = 0;
		FMcStateId Planks, Fence, Rail, Powered, Web, Spawner, Air, Log;

		FMineshaft(FDraw& InD, FMcRandom& InR, bool bInMesa, const FIntVector& InCentre) : D(InD), R(InR), bMesa(bInMesa), Centre(InCentre)
		{
			Planks = D.S(bMesa ? TEXT("minecraft:dark_oak_planks") : TEXT("minecraft:oak_planks"));
			Fence = D.S(bMesa ? TEXT("minecraft:dark_oak_fence") : TEXT("minecraft:oak_fence"));
			Log = D.S(bMesa ? TEXT("minecraft:dark_oak_log") : TEXT("minecraft:oak_log"));
			Rail = D.S(TEXT("minecraft:rail"));
			Powered = D.S(TEXT("minecraft:powered_rail"));
			Web = D.S(TEXT("minecraft:cobweb"));
			Spawner = D.S(TEXT("minecraft:spawner"));
			Air = D.S(TEXT("minecraft:air"));
		}

		bool Free(const FBounds& Box) const
		{
			if (!Within(Box, Centre.X, Centre.Z, 88) || Box.Min.Y < D.Env.MinY + 4 || Box.Max.Y > D.Env.MaxY - 4)
			{
				return false;
			}
			for (const FBounds& Other : Boxes)
			{
				if (Box.Overlaps(Other))
				{
					return false;
				}
			}
			return true;
		}

		void Corridor(const FFrame& F, int32 Depth)
		{
			// Drawn whatever happens next, so the stream is the same for every chunk.
			int32 Length = 5 * (2 + R.NextInt(3));
			const bool bRails = R.NextInt(2) == 0;
			const bool bSpawner = R.NextInt(23) == 0;
			const int32 Next = R.NextInt(100);
			if (Depth > 9 || Count >= 70)
			{
				return;
			}
			FBounds Box;
			for (; Length >= 5; Length -= 5)
			{
				Box = F.Box(-1, 0, 0, 1, 2, Length - 1);
				if (Free(Box))
				{
					break;
				}
			}
			if (Length < 5)
			{
				return;
			}
			Boxes.Add(Box);
			++Count;
			const uint32 Salt = Hash(F.Origin.X, F.Origin.Y, F.Origin.Z);
			D.FillWith(F, -1, 0, 0, 1, 2, Length - 1, [&](int32 X, int32 Y, int32 Z) { return Hash(X, Y, Z, Salt) % 100 < 4 ? Web : Air; }, EMode::Carve);
			D.Fill(F, -1, -1, 0, 1, -1, Length - 1, Planks, EMode::IfOpen);
			for (int32 V = 1; V < Length; V += 4)
			{
				D.Fill(F, -1, 0, V, -1, 1, V, Fence);
				D.Fill(F, 1, 0, V, 1, 1, V, Fence);
				D.Fill(F, -1, 2, V, 1, 2, V, Planks);
			}
			if (bRails)
			{
				for (int32 V = 0; V < Length; ++V)
				{
					D.Put(F, 0, 0, V, V % 6 == 5 ? Powered : Rail);
				}
			}
			if (bSpawner)
			{
				D.Fill(F, -1, 0, Length / 2 - 1, 1, 2, Length / 2 + 1, Web, EMode::IfOpen);
				D.Put(F, 0, 0, Length / 2, Spawner);
			}
			const FFrame End = F.Child(0, Length, 0);
			if (Next < 45)
			{
				Corridor(End, Depth + 1);
			}
			else if (Next < 85)
			{
				Crossing(End, Depth + 1);
			}
			else if (Next < 95)
			{
				Room(End, Depth + 1);
			}
		}

		// A bigger cross-shaped chamber where corridors meet.
		void Crossing(const FFrame& F, int32 Depth)
		{
			const bool bForward = R.NextInt(10) < 7;
			const bool bLeft = R.NextInt(10) < 7;
			const bool bRight = R.NextInt(10) < 7;
			const FBounds Box = F.Box(-2, 0, 0, 2, 3, 4);
			if (!Free(Box))
			{
				return;
			}
			Boxes.Add(Box);
			++Count;
			D.Fill(F, -2, 0, 0, 2, 3, 4, Air, EMode::Carve);
			D.Fill(F, -2, -1, 0, 2, -1, 4, Planks, EMode::IfOpen);
			for (const int32 U : { -2, 2 })
			{
				for (const int32 V : { 0, 4 })
				{
					D.Fill(F, U, 0, V, U, 3, V, Log);
				}
			}
			if (bForward)
			{
				Corridor(F.Child(0, 5, 0), Depth);
			}
			if (bLeft)
			{
				Corridor(F.Child(-3, 2, 3), Depth);
			}
			if (bRight)
			{
				Corridor(F.Child(3, 2, 1), Depth);
			}
		}

		void Room(const FFrame& F, int32 Depth)
		{
			const int32 Width = 7 + 2 * R.NextInt(3);
			const int32 Length = 6 + R.NextInt(4);
			const int32 Half = Width / 2;
			// Where the exits go, drawn whatever happens.
			const int32 ExitU = R.NextInt(Width - 2) - (Half - 1);
			const int32 ExitLeft = 1 + R.NextInt(Length - 2);
			const int32 ExitRight = 1 + R.NextInt(Length - 2);
			const bool bForward = R.NextInt(10) < 8;
			const bool bLeft = R.NextInt(10) < 7;
			const bool bRight = R.NextInt(10) < 7;
			const FBounds Box = F.Box(-Half, 0, 0, Half, 4, Length - 1);
			if (Depth > 0 && !Free(Box))
			{
				return;
			}
			Boxes.Add(Box);
			++Count;
			D.Fill(F, -Half, 0, 0, Half, 4, Length - 1, Air, EMode::Carve);
			D.Fill(F, -Half, -1, 0, Half, -1, Length - 1, Planks, EMode::IfOpen);
			D.Fill(F, -Half, 0, 0, -Half, 1, 0, Fence);
			D.Fill(F, Half, 0, 0, Half, 1, 0, Fence);
			if (bForward)
			{
				Corridor(F.Child(ExitU, Length, 0), Depth + 1);
			}
			if (bLeft)
			{
				Corridor(F.Child(-Half - 1, ExitLeft, 3), Depth + 1);
			}
			if (bRight)
			{
				Corridor(F.Child(Half + 1, ExitRight, 1), Depth + 1);
			}
		}
	};

	// ---- stronghold ------------------------------------------------------------------------------------------
	// (minecraft.wiki, "Stronghold"): a spiral staircase down into corridors and rooms of stone bricks, ending in
	// the portal room (twelve frames round lava, a silverfish spawner); some rooms are libraries or fountains.

	struct FStronghold
	{
		FDraw& D;
		FMcRandom& R;
		FIntVector Centre;
		TArray<FBounds> Boxes;
		// Exits that lead nowhere yet, with the piece each is on.
		TArray<TPair<FFrame, int32>> Open;
		int32 Count = 0;
		bool bLibrary = false;
		FMcStateId Air, Torch, Bars;

		FStronghold(FDraw& InD, FMcRandom& InR, const FIntVector& InCentre) : D(InD), R(InR), Centre(InCentre)
		{
			Air = D.S(TEXT("minecraft:air"));
			Torch = D.S(TEXT("minecraft:wall_torch[facing=east]"));
			Bars = D.S(TEXT("minecraft:iron_bars"));
		}

		FMcStateId Brick(int32 X, int32 Y, int32 Z)
		{
			const uint32 H = Hash(X, Y, Z, 7) % 100;
			if (H < 55)
			{
				return D.S(TEXT("minecraft:stone_bricks"));
			}
			if (H < 75)
			{
				return D.S(TEXT("minecraft:mossy_stone_bricks"));
			}
			if (H < 90)
			{
				return D.S(TEXT("minecraft:cracked_stone_bricks"));
			}
			return D.S(TEXT("minecraft:stone"));
		}

		bool Free(const FBounds& Box, int32 Ignore) const
		{
			if (!Within(Box, Centre.X, Centre.Z, 92) || Box.Min.Y < D.Env.MinY + 4)
			{
				return false;
			}
			for (int32 Index = 0; Index < Boxes.Num(); ++Index)
			{
				if (Index != Ignore && Box.Overlaps(Boxes[Index]))
				{
					return false;
				}
			}
			return true;
		}

		// Walls round an interior of u U0..U1, v V0..V1, y 0..Height, then the interior emptied.
		void Hull(const FFrame& F, int32 U0, int32 V0, int32 U1, int32 V1, int32 Height)
		{
			D.FillWith(F, U0 - 1, -1, V0 - 1, U1 + 1, Height + 1, V1 + 1, [this](int32 X, int32 Y, int32 Z) { return Brick(X, Y, Z); });
			D.Fill(F, U0, 0, V0, U1, Height, V1, Air, EMode::Carve);
		}

		FBounds BoxOf(const FFrame& F, int32 U0, int32 V0, int32 U1, int32 V1, int32 Height) const { return F.Box(U0 - 1, -1, V0 - 1, U1 + 1, Height + 1, V1 + 1); }

		// The doorway in the wall just behind a piece's first block.
		void Door(const FFrame& F) { D.Fill(F, -1, 0, -1, 1, 2, -1, Air, EMode::Carve); }

		void Straight(const FFrame& F, int32 Parent)
		{
			const int32 Next = R.NextInt(100);
			if (Count >= 24)
			{
				Open.Add({ F, Parent });
				return;
			}
			const FBounds Box = BoxOf(F, -1, 0, 1, 4, 2);
			if (!Free(Box, Parent))
			{
				Open.Add({ F, Parent });
				return;
			}
			const int32 Index = Boxes.Add(Box);
			++Count;
			Hull(F, -1, 0, 1, 4, 2);
			Door(F);
			D.Put(F, -1, 2, 2, Torch);
			const FFrame End = F.Child(0, 6, 0);
			if (Next < 45)
			{
				Straight(End, Index);
			}
			else if (Next < 70)
			{
				Room(End, Index);
			}
			else if (Next < 82)
			{
				Straight(F.Child(3, 2, 1), Index);
			}
			else if (Next < 94)
			{
				Straight(F.Child(-3, 2, 3), Index);
			}
			else
			{
				Open.Add({ End, Index });
			}
		}

		void Room(const FFrame& F, int32 Parent)
		{
			const int32 Variant = R.NextInt(10);
			const int32 ExitU = R.NextInt(5) - 2;
			const int32 ExitLeft = 2 + R.NextInt(6);
			const int32 ExitRight = 2 + R.NextInt(6);
			const bool bForward = R.NextInt(10) < 7;
			const bool bLeft = R.NextInt(10) < 6;
			const bool bRight = R.NextInt(10) < 6;
			if (Variant == 9 && !bLibrary)
			{
				Library(F, Parent);
				return;
			}
			const FBounds Box = BoxOf(F, -5, 0, 5, 10, 5);
			if (Count >= 24 || !Free(Box, Parent))
			{
				Open.Add({ F, Parent });
				return;
			}
			const int32 Index = Boxes.Add(Box);
			++Count;
			Hull(F, -5, 0, 5, 10, 5);
			Door(F);
			if (Variant < 4)
			{
				// A fountain: a basin of bricks with water in it.
				D.FillWith(F, -2, 0, 3, 2, 0, 7, [this](int32 X, int32 Y, int32 Z) { return Brick(X, Y, Z); });
				D.Fill(F, -1, 0, 4, 1, 0, 6, D.S(TEXT("minecraft:water")));
				D.Fill(F, 0, 1, 5, 0, 3, 5, D.S(TEXT("minecraft:stone_bricks")));
			}
			else if (Variant < 8)
			{
				for (const int32 U : { -3, 3 })
				{
					for (const int32 V : { 3, 7 })
					{
						D.FillWith(F, U, 0, V, U, 4, V, [this](int32 X, int32 Y, int32 Z) { return Brick(X, Y, Z); });
					}
				}
			}
			for (const int32 V : { 2, 8 })
			{
				D.Put(F, -5, 3, V, D.S(TEXT("minecraft:wall_torch[facing=east]")));
				D.Put(F, 5, 3, V, D.S(TEXT("minecraft:wall_torch[facing=west]")));
			}
			if (bForward)
			{
				Straight(F.Child(ExitU, 12, 0), Index);
			}
			if (bLeft)
			{
				Straight(F.Child(-7, ExitLeft, 3), Index);
			}
			if (bRight)
			{
				Straight(F.Child(7, ExitRight, 1), Index);
			}
		}

		// Two floors of bookshelves round a balcony; a dead end.
		void Library(const FFrame& F, int32 Parent)
		{
			const FBounds Box = BoxOf(F, -6, 0, 6, 12, 9);
			if (!Free(Box, Parent))
			{
				Open.Add({ F, Parent });
				return;
			}
			Boxes.Add(Box);
			++Count;
			bLibrary = true;
			Hull(F, -6, 0, 6, 12, 9);
			Door(F);
			const FMcStateId Shelf = D.S(TEXT("minecraft:bookshelf"));
			const FMcStateId Planks = D.S(TEXT("minecraft:oak_planks"));
			const FMcStateId Fence = D.S(TEXT("minecraft:oak_fence"));
			for (const int32 Side : { -6, 6 })
			{
				D.Fill(F, Side, 0, 1, Side, 3, 12, Shelf);
				D.Fill(F, Side, 5, 1, Side, 8, 12, Shelf);
			}
			D.Fill(F, -6, 12 - 12, 12, 6, 3, 12, Shelf);
			D.Fill(F, -6, 4, 1, -4, 4, 12, Planks);
			D.Fill(F, 4, 4, 1, 6, 4, 12, Planks);
			D.Fill(F, -4, 5, 1, -4, 5, 12, Fence);
			D.Fill(F, 4, 5, 1, 4, 5, 12, Fence);
			D.Fill(F, 0, 6, 6, 0, 8, 6, Fence);
			D.Put(F, 0, 5, 6, D.S(TEXT("minecraft:torch")));
		}

		// A spiral staircase of full steps in the middle of a small chamber.
		void Start(const FFrame& F)
		{
			Boxes.Add(BoxOf(F, -2, 0, 2, 4, 9));
			++Count;
			Hull(F, -2, 0, 2, 4, 9);
			static const int32 Ring[8][2] = { { -1, 1 }, { 0, 1 }, { 1, 1 }, { 1, 2 }, { 1, 3 }, { 0, 3 }, { -1, 3 }, { -1, 2 } };
			for (int32 Step = 0; Step < 8; ++Step)
			{
				D.FillWith(F, Ring[Step][0], 0, Ring[Step][1], Ring[Step][0], Step, Ring[Step][1], [this](int32 X, int32 Y, int32 Z) { return Brick(X, Y, Z); });
			}
			D.Fill(F, 0, 0, 2, 0, 9, 2, D.S(TEXT("minecraft:stone_bricks")));
			D.Fill(F, -1, 9, 0, 1, 9, 0, Air, EMode::Carve);
			const int32 Next = R.NextInt(3);
			Straight(F.Child(0, 6, 0), 0);
			if (Next > 0)
			{
				Straight(F.Child(4, 2, 1), 0);
			}
			if (Next > 1)
			{
				Straight(F.Child(-4, 2, 3), 0);
			}
		}

		void Portal(const FFrame& F)
		{
			Hull(F, -5, 0, 5, 15, 8);
			Door(F);
			// The lava pool, the platform in it and the frames round the portal.
			D.Fill(F, -4, -3, 6, 4, -1, 14, D.S(TEXT("minecraft:lava")), EMode::Carve);
			D.FillWith(F, -2, -1, 8, 2, -1, 12, [this](int32 X, int32 Y, int32 Z) { return Brick(X, Y, Z); });
			D.Fill(F, -4, 0, 1, 4, 0, 5, D.S(TEXT("minecraft:stone_brick_slab")));
			struct FFrameSpot { int32 U, V; const TCHAR* Facing; };
			TArray<FFrameSpot> Spots;
			for (int32 U = -1; U <= 1; ++U)
			{
				Spots.Add({ U, 8, TEXT("north") });
				Spots.Add({ U, 12, TEXT("south") });
			}
			for (int32 V = 9; V <= 11; ++V)
			{
				Spots.Add({ -2, V, TEXT("east") });
				Spots.Add({ 2, V, TEXT("west") });
			}
			for (const FFrameSpot& Spot : Spots)
			{
				const bool bEye = Hash(Centre.X, Spot.U, Spot.V, 11) % 10 == 0;
				D.Put(F, Spot.U, 0, Spot.V, D.S(*FString::Printf(TEXT("minecraft:end_portal_frame[eye=%s,facing=%s]"), bEye ? TEXT("true") : TEXT("false"), Spot.Facing)));
			}
			D.Put(F, 0, -1, 10, D.S(TEXT("minecraft:spawner")));
			for (const int32 V : { 3, 13 })
			{
				D.Put(F, -5, 4, V, D.S(TEXT("minecraft:wall_torch[facing=east]")));
				D.Put(F, 5, 4, V, D.S(TEXT("minecraft:wall_torch[facing=west]")));
			}
		}

		void Finish()
		{
			// The portal room goes at the exit furthest from the staircase that has room for it.
			Open.Sort([this](const TPair<FFrame, int32>& A, const TPair<FFrame, int32>& B)
			{
				const int32 DA = FMath::Square(A.Key.Origin.X - Centre.X) + FMath::Square(A.Key.Origin.Z - Centre.Z);
				const int32 DB = FMath::Square(B.Key.Origin.X - Centre.X) + FMath::Square(B.Key.Origin.Z - Centre.Z);
				return DA > DB;
			});
			for (const TPair<FFrame, int32>& Exit : Open)
			{
				const FBounds Box = BoxOf(Exit.Key, -5, 0, 5, 15, 8);
				if (Free(Box, Exit.Value))
				{
					Boxes.Add(Box);
					Portal(Exit.Key);
					return;
				}
			}
		}
	};

	// ---- ocean monument --------------------------------------------------------------------------------------
	// (minecraft.wiki, "Ocean monument"): prismarine on the sea floor, three tiers of flooded rooms with sea
	// lanterns, the elder guardian's room of dark prismarine with eight gold blocks, and a spire on top.

	void MonumentWall(FDraw& D, const FFrame& F, int32 U0, int32 V0, int32 U1, int32 V1, int32 Y0, int32 Y1, int32 Seed)
	{
		const FMcStateId Bricks = D.S(TEXT("minecraft:prismarine_bricks"));
		const FMcStateId Plain = D.S(TEXT("minecraft:prismarine"));
		const FMcStateId Dark = D.S(TEXT("minecraft:dark_prismarine"));
		const FMcStateId Lantern = D.S(TEXT("minecraft:sea_lantern"));
		const FMcStateId Water = D.S(TEXT("minecraft:water"));
		D.FillWith(F, U0, Y0, V0, U1, Y1, V1, [&](int32 X, int32 Y, int32 Z)
		{
			const uint32 H = Hash(X, Y, Z, Seed) % 100;
			if (Y == Y0 + 2 && H < 18)
			{
				return Lantern;
			}
			return H < 55 ? Bricks : (H < 85 ? Plain : Dark);
		});
		D.Fill(F, U0 + 1, Y0, V0 + 1, U1 - 1, Y1, V1 - 1, Water, EMode::Carve);
	}

	void MonumentBuild(FDraw& D, FMcRandom& R, int32 CentreX, int32 CentreZ, int32 BaseY)
	{
		const FFrame F{ FIntVector(CentreX, BaseY, CentreZ), R.NextInt(4) };
		const FMcStateId Bricks = D.S(TEXT("minecraft:prismarine_bricks"));
		const FMcStateId Dark = D.S(TEXT("minecraft:dark_prismarine"));
		const FMcStateId Lantern = D.S(TEXT("minecraft:sea_lantern"));
		const FMcStateId Water = D.S(TEXT("minecraft:water"));
		const FMcStateId Gold = D.S(TEXT("minecraft:gold_block"));
		const FMcStateId Sponge = D.S(TEXT("minecraft:wet_sponge"));
		// Legs down to the sea floor, and the floor of the lowest tier.
		for (int32 U = -22; U <= 22; U += 11)
		{
			for (int32 V = -22; V <= 22; V += 11)
			{
				D.Fill(F, U, -40, V, U, -1, V, Bricks, EMode::IfOpen);
			}
		}
		D.FillWith(F, -22, 0, -22, 22, 0, 22, [&](int32 X, int32 Y, int32 Z) { return ((X + Z) % 6 == 0 && Hash(X, 0, Z, 3) % 3 == 0) ? Lantern : Bricks; });
		// The wings.
		MonumentWall(D, F, -29, -9, -21, 9, 1, 6, 5);
		MonumentWall(D, F, 21, -9, 29, 9, 1, 6, 6);
		// Tier one: a hollow block of rooms.
		MonumentWall(D, F, -22, -22, 22, 22, 1, 6, 1);
		for (const int32 Line : { -11, 11 })
		{
			D.Fill(F, Line, 1, -21, Line, 6, 21, Bricks);
			D.Fill(F, -21, 1, Line, 21, 6, Line, Bricks);
			D.Fill(F, Line, 1, -2, Line, 4, 2, Water, EMode::Carve);
			D.Fill(F, -2, 1, Line, 2, 4, Line, Water, EMode::Carve);
		}
		D.Fill(F, -22, 7, -22, 22, 7, 22, Bricks);
		D.Fill(F, -2, 1, -22, 2, 4, -22, Water, EMode::Carve);  // the way in
		// Tier two, with the elder guardian's room in its middle.
		MonumentWall(D, F, -14, -14, 14, 14, 8, 14, 2);
		D.Fill(F, -14, 15, -14, 14, 15, 14, Bricks);
		D.Fill(F, -2, 7, -2, 2, 7, 2, Water, EMode::Carve);
		D.Fill(F, -3, 8, -3, 3, 14, 3, Dark);
		D.Fill(F, -2, 9, -2, 2, 13, 2, Water, EMode::Carve);
		D.Fill(F, -1, 9, -1, 0, 10, 0, Gold);
		D.Fill(F, -2, 11, -2, 2, 11, 2, Sponge, EMode::IfOpen);
		for (const int32 U : { -2, 2 })
		{
			for (const int32 V : { -2, 2 })
			{
				D.Put(F, U, 9, V, Lantern);
			}
		}
		D.Fill(F, -2, 9, -3, 2, 12, -3, Water, EMode::Carve);
		D.Fill(F, -1, 8, -14, 1, 11, -14, Water, EMode::Carve);
		// Tier three and the spire.
		MonumentWall(D, F, -7, -7, 7, 7, 16, 21, 3);
		D.Fill(F, -7, 22, -7, 7, 22, 7, Bricks);
		D.Fill(F, -1, 15, -1, 1, 15, 1, Water, EMode::Carve);
		for (int32 Layer = 0; Layer < 6; ++Layer)
		{
			const int32 Half = 5 - Layer;
			D.FillWith(F, -Half, 23 + Layer, -Half, Half, 23 + Layer, Half, [&](int32 X, int32 Y, int32 Z) { return Hash(X, Y, Z, 9) % 5 == 0 ? Lantern : Dark; });
		}
		D.Fill(F, 0, 29, 0, 0, 30, 0, Dark);
	}

	// ---- nether fortress -------------------------------------------------------------------------------------
	// (minecraft.wiki, "Nether fortress"): bridges and covered corridors of nether bricks, crossings, rooms with
	// nether wart on soul sand, and the blaze spawner on a platform in one room.

	struct FFortress
	{
		FDraw& D;
		FMcRandom& R;
		FIntVector Centre;
		TArray<FBounds> Boxes;
		int32 Count = 0;
		bool bThrone = false;
		FMcStateId Bricks, Fence, Air, Lava;

		FFortress(FDraw& InD, FMcRandom& InR, const FIntVector& InCentre) : D(InD), R(InR), Centre(InCentre)
		{
			Bricks = D.S(TEXT("minecraft:nether_bricks"));
			Fence = D.S(TEXT("minecraft:nether_brick_fence"));
			Air = D.S(TEXT("minecraft:air"));
			Lava = D.S(TEXT("minecraft:lava"));
		}

		bool Free(const FBounds& Box, int32 Ignore) const
		{
			if (!Within(Box, Centre.X, Centre.Z, 92) || Box.Min.Y < D.Env.MinY + 4 || Box.Max.Y > D.Env.MaxY - 4)
			{
				return false;
			}
			for (int32 Index = 0; Index < Boxes.Num(); ++Index)
			{
				if (Index != Ignore && Box.Overlaps(Boxes[Index]))
				{
					return false;
				}
			}
			return true;
		}

		// Pillars from under a bridge down until they meet something.
		void Support(const FFrame& F, int32 U, int32 V)
		{
			D.Fill(F, U, -40, V, U, -2, V, Bricks, EMode::IfOpen);
		}

		void Bridge(const FFrame& F, int32 Parent)
		{
			const int32 Pick = R.NextInt(100);
			if (Count >= 40)
			{
				return;
			}
			const FBounds Box = F.Box(-2, -2, 0, 2, 4, 9);
			if (!Free(Box, Parent))
			{
				return;
			}
			const int32 Index = Boxes.Add(Box);
			++Count;
			D.Fill(F, -2, 0, 0, 2, 4, 9, Air, EMode::Carve);
			D.Fill(F, -2, -1, 0, 2, -1, 9, Bricks);
			D.Fill(F, -2, 0, 0, -2, 0, 9, Fence);
			D.Fill(F, 2, 0, 0, 2, 0, 9, Fence);
			D.Fill(F, -2, -2, 0, 2, -2, 9, Bricks, EMode::IfOpen);
			Support(F, -2, 4);
			Support(F, 2, 4);
			const FFrame End = F.Child(0, 10, 0);
			if (Pick < 35)
			{
				Bridge(End, Index);
			}
			else if (Pick < 55)
			{
				Crossing(End, Index);
			}
			else if (Pick < 80)
			{
				Corridor(End, Index);
			}
			else if (Pick < 92)
			{
				Hall(End, Index, false);
			}
			else
			{
				Hall(End, Index, true);
			}
		}

		void Crossing(const FFrame& F, int32 Parent)
		{
			const bool bForward = R.NextInt(10) < 8;
			const bool bLeft = R.NextInt(10) < 6;
			const bool bRight = R.NextInt(10) < 6;
			const FBounds Box = F.Box(-4, -2, 0, 4, 4, 8);
			if (Count >= 40 || !Free(Box, Parent))
			{
				return;
			}
			const int32 Index = Boxes.Add(Box);
			++Count;
			D.Fill(F, -4, 0, 0, 4, 4, 8, Air, EMode::Carve);
			D.Fill(F, -4, -1, 0, 4, -1, 8, Bricks);
			D.Fill(F, -4, 0, 0, -4, 0, 8, Fence);
			D.Fill(F, 4, 0, 0, 4, 0, 8, Fence);
			D.Fill(F, -4, 0, 8, 4, 0, 8, Fence);
			D.Fill(F, -4, 0, 0, 4, 0, 0, Air, EMode::Carve);
			D.Fill(F, -1, 0, 8, 1, 0, 8, Air, EMode::Carve);
			D.Fill(F, -4, 0, 3, -4, 0, 5, Air, EMode::Carve);
			D.Fill(F, 4, 0, 3, 4, 0, 5, Air, EMode::Carve);
			for (const int32 U : { -4, 4 })
			{
				for (const int32 V : { 0, 8 })
				{
					D.Fill(F, U, 0, V, U, 3, V, Bricks);
				}
			}
			Support(F, -4, 4);
			Support(F, 4, 4);
			Support(F, 0, 4);
			if (bForward)
			{
				Bridge(F.Child(0, 9, 0), Index);
			}
			if (bLeft)
			{
				Bridge(F.Child(-5, 4, 3), Index);
			}
			if (bRight)
			{
				Bridge(F.Child(5, 4, 1), Index);
			}
		}

		// A covered hall: walls, a roof, windows of fence.
		void Corridor(const FFrame& F, int32 Parent)
		{
			const int32 Pick = R.NextInt(100);
			const int32 Length = 9 + 2 * R.NextInt(3);
			const FBounds Box = F.Box(-3, -2, 0, 3, 6, Length - 1);
			if (Count >= 40 || !Free(Box, Parent))
			{
				return;
			}
			const int32 Index = Boxes.Add(Box);
			++Count;
			D.Fill(F, -3, -1, 0, 3, 6, Length - 1, Bricks);
			D.Fill(F, -2, 0, 0, 2, 4, Length - 1, Air, EMode::Carve);
			for (int32 V = 2; V < Length - 1; V += 3)
			{
				D.Fill(F, -3, 2, V, -3, 3, V, Fence);
				D.Fill(F, 3, 2, V, 3, 3, V, Fence);
			}
			D.Fill(F, -2, 0, 0, 2, 4, 0, Air, EMode::Carve);
			D.Fill(F, -3, -1, 0, 3, 0, 0, Bricks, EMode::IfOpen);
			D.Fill(F, -1, 0, Length - 1, 1, 2, Length - 1, Air, EMode::Carve);
			Support(F, -2, Length / 2);
			Support(F, 2, Length / 2);
			const FFrame End = F.Child(0, Length, 0);
			if (Pick < 45)
			{
				Bridge(End, Index);
			}
			else if (Pick < 70)
			{
				Corridor(End, Index);
			}
			else
			{
				Hall(End, Index, !bThrone && Count > 6);
			}
		}

		// A big room: the throne room with the blaze spawner, or one of nether wart.
		void Hall(const FFrame& F, int32 Parent, bool bBlaze)
		{
			const bool bForward = R.NextInt(10) < 5;
			const bool bLeft = R.NextInt(10) < 5;
			const bool bRight = R.NextInt(10) < 5;
			const FBounds Box = F.Box(-7, -2, 0, 7, 9, 14);
			if (Count >= 40 || !Free(Box, Parent))
			{
				return;
			}
			const int32 Index = Boxes.Add(Box);
			++Count;
			D.Fill(F, -7, -1, 0, 7, 9, 14, Bricks);
			D.Fill(F, -6, 0, 1, 6, 7, 13, Air, EMode::Carve);
			D.Fill(F, -1, 0, 0, 1, 3, 0, Air, EMode::Carve);
			D.Fill(F, -7, -1, 0, 7, 0, 0, Bricks, EMode::IfOpen);
			if (bBlaze && !bThrone)
			{
				bThrone = true;
				D.Fill(F, -2, 0, 5, 2, 0, 9, D.S(TEXT("minecraft:nether_bricks")));
				D.Fill(F, -2, 1, 5, 2, 1, 9, Fence);
				D.Fill(F, -1, 1, 6, 1, 1, 8, Air, EMode::Carve);
				D.Put(F, 0, 1, 7, D.S(TEXT("minecraft:spawner")));
			}
			else
			{
				const FMcStateId Sand = D.S(TEXT("minecraft:soul_sand"));
				for (int32 U = -5; U <= 5; U += 2)
				{
					for (int32 V = 3; V <= 11; V += 2)
					{
						if (Hash(Centre.X + U, 0, Centre.Z + V, 17) % 3 != 0)
						{
							D.Put(F, U, 0, V, Sand);
							D.Put(F, U, 1, V, D.S(*FString::Printf(TEXT("minecraft:nether_wart[age=%d]"), static_cast<int32>(Hash(U, V, Centre.X, 5) % 4))));
						}
					}
				}
			}
			D.Fill(F, -7, 6, 1, -7, 7, 13, Fence);
			Support(F, -6, 7);
			Support(F, 6, 7);
			if (bForward)
			{
				D.Fill(F, -1, 0, 14, 1, 3, 14, Air, EMode::Carve);
				Bridge(F.Child(0, 15, 0), Index);
			}
			if (bLeft)
			{
				D.Fill(F, -7, 0, 6, -7, 3, 8, Air, EMode::Carve);
				Bridge(F.Child(-8, 7, 3), Index);
			}
			if (bRight)
			{
				D.Fill(F, 7, 0, 6, 7, 3, 8, Air, EMode::Carve);
				Bridge(F.Child(8, 7, 1), Index);
			}
		}
	};

	// ---- woodland mansion ------------------------------------------------------------------------------------
	// (minecraft.wiki, "Woodland mansion"): a cobblestone and dark oak house of two floors under a roof, a grid
	// of rooms joined by doorways, with an entrance hall, windows and a ladder up.

	void MansionBuild(FDraw& D, FMcRandom& R, int32 CentreX, int32 CentreZ, int32 GroundY)
	{
		const int32 Cell = 7;
		const int32 CellsX = 3 + R.NextInt(3);
		const int32 CellsZ = 4 + R.NextInt(3);
		const int32 Width = CellsX * Cell + 1;
		const int32 Depth = CellsZ * Cell + 1;
		// Axis aligned: u runs east and v north from the north west corner, so the house lies at negative v.
		const FFrame Base = North(CentreX - Width / 2, GroundY, CentreZ - Depth / 2);
		const FMcStateId Cobble = D.S(TEXT("minecraft:cobblestone"));
		const FMcStateId Planks = D.S(TEXT("minecraft:dark_oak_planks"));
		const FMcStateId Log = D.S(TEXT("minecraft:dark_oak_log"));
		const FMcStateId Pane = D.S(TEXT("minecraft:glass_pane"));
		const FMcStateId Air = D.S(TEXT("minecraft:air"));
		const FMcStateId Carpet = D.S(TEXT("minecraft:red_carpet"));
		const FMcStateId Birch = D.S(TEXT("minecraft:birch_planks"));
		const FMcStateId Slab = D.S(TEXT("minecraft:dark_oak_slab"));
		const int32 Floors = 2;
		const int32 Storey = 5;
		const int32 Top = Floors * Storey;

		// Doorways between cells: a spanning tree of them, then a few more.
		const int32 Cells = CellsX * CellsZ;
		TArray<int32> Parent;
		for (int32 Index = 0; Index < Cells; ++Index)
		{
			Parent.Add(Index);
		}
		TFunction<int32(int32)> FindRoot = [&](int32 Index) { return Parent[Index] == Index ? Index : Parent[Index] = FindRoot(Parent[Index]); };
		struct FEdge { int32 A, B; bool bAlongX; };
		TArray<FEdge> Edges;
		for (int32 Z = 0; Z < CellsZ; ++Z)
		{
			for (int32 X = 0; X < CellsX; ++X)
			{
				if (X + 1 < CellsX)
				{
					Edges.Add({ Z * CellsX + X, Z * CellsX + X + 1, true });
				}
				if (Z + 1 < CellsZ)
				{
					Edges.Add({ Z * CellsX + X, (Z + 1) * CellsX + X, false });
				}
			}
		}
		for (int32 Index = Edges.Num() - 1; Index > 0; --Index)
		{
			Edges.Swap(Index, R.NextInt(Index + 1));
		}
		TArray<FEdge> Doors;
		for (const FEdge& Edge : Edges)
		{
			const bool bExtra = R.NextInt(5) == 0;
			if (FindRoot(Edge.A) != FindRoot(Edge.B))
			{
				Parent[FindRoot(Edge.A)] = FindRoot(Edge.B);
				Doors.Add(Edge);
			}
			else if (bExtra)
			{
				Doors.Add(Edge);
			}
		}
		const int32 Front = R.NextInt(CellsX);

		// Footing and ground: bricks down to the soil, the hill above cut away.
		D.Fill(Base, 0, -12, -(Depth - 1), Width - 1, -1, 0, Cobble, EMode::IfOpen);
		D.Fill(Base, 0, 0, -(Depth - 1), Width - 1, Top + 8, 0, Air, EMode::Carve);
		for (int32 Level = 0; Level <= Floors; ++Level)
		{
			D.Fill(Base, 0, Level * Storey, -(Depth - 1), Width - 1, Level * Storey, 0, Level == 0 ? Planks : Planks);
		}
		// Outer walls with pillars and windows.
		for (int32 Level = 0; Level < Floors; ++Level)
		{
			const int32 Y0 = Level * Storey + 1;
			const int32 Y1 = Level * Storey + Storey - 1;
			const FMcStateId Wall = Level == 0 ? Cobble : Planks;
			D.Fill(Base, 0, Y0, -(Depth - 1), Width - 1, Y1, 0, Wall);
			D.Fill(Base, 1, Y0, -(Depth - 2), Width - 2, Y1, -1, Air, EMode::Carve);
			for (int32 X = 0; X < Width; X += Cell)
			{
				D.Fill(Base, X, Y0, -(Depth - 1), X, Y1, -(Depth - 1), Log);
				D.Fill(Base, X, Y0, 0, X, Y1, 0, Log);
			}
			for (int32 Z = 0; Z < Depth; Z += Cell)
			{
				D.Fill(Base, 0, Y0, -Z, 0, Y1, -Z, Log);
				D.Fill(Base, Width - 1, Y0, -Z, Width - 1, Y1, -Z, Log);
			}
			for (int32 Index = 0; Index < CellsX; ++Index)
			{
				const int32 X = Index * Cell + Cell / 2;
				D.Fill(Base, X, Y0 + 1, 0, X, Y0 + 2, 0, Pane);
				D.Fill(Base, X, Y0 + 1, -(Depth - 1), X, Y0 + 2, -(Depth - 1), Pane);
			}
			for (int32 Index = 0; Index < CellsZ; ++Index)
			{
				const int32 Z = Index * Cell + Cell / 2;
				D.Fill(Base, 0, Y0 + 1, -Z, 0, Y0 + 2, -Z, Pane);
				D.Fill(Base, Width - 1, Y0 + 1, -Z, Width - 1, Y0 + 2, -Z, Pane);
			}
			// Inner walls between rooms, with doorways where the tree says.
			for (int32 Z = 1; Z < CellsZ; ++Z)
			{
				D.Fill(Base, 1, Y0, -Z * Cell, Width - 2, Y1, -Z * Cell, Level == 0 ? Birch : Planks);
			}
			for (int32 X = 1; X < CellsX; ++X)
			{
				D.Fill(Base, X * Cell, Y0, -(Depth - 2), X * Cell, Y1, -1, Level == 0 ? Birch : Planks);
			}
			for (const FEdge& Edge : Doors)
			{
				const int32 AX = Edge.A % CellsX;
				const int32 AZ = Edge.A / CellsX;
				if (Edge.bAlongX)
				{
					const int32 X = (AX + 1) * Cell;
					const int32 Z = AZ * Cell + Cell / 2;
					D.Fill(Base, X, Y0, -Z, X, Y0 + 2, -Z, Air, EMode::Carve);
				}
				else
				{
					const int32 X = AX * Cell + Cell / 2;
					const int32 Z = (AZ + 1) * Cell;
					D.Fill(Base, X, Y0, -Z, X, Y0 + 2, -Z, Air, EMode::Carve);
				}
			}
		}
		// The entrance, on the south wall, and the hall behind it.
		const int32 DoorX = Front * Cell + Cell / 2;
		D.Fill(Base, DoorX - 1, 1, 0, DoorX + 1, 3, 0, Air, EMode::Carve);
		D.Fill(Base, DoorX - 1, 1, -1, DoorX + 1, 1, -(Depth - 2), Carpet, EMode::IfOpen);
		D.Fill(Base, DoorX - 3, -1, 1, DoorX + 3, -1, 3, Cobble, EMode::IfOpen);
		// A ladder up from the first room.
		D.Fill(Base, 2, Storey, -(Depth - 2), 2, Storey, -(Depth - 2), Air, EMode::Carve);
		D.Fill(Base, 2, 1, -(Depth - 2), 2, Storey + 3, -(Depth - 2), D.S(TEXT("minecraft:ladder[facing=north]")));
		// The roof: stepped dark oak, a slab on the ridge.
		const int32 HalfX = (Width - 1) / 2;
		for (int32 Layer = 0; Layer <= HalfX; ++Layer)
		{
			D.Fill(Base, Layer, Top + 1 + Layer, -(Depth - 1), Width - 1 - Layer, Top + 1 + Layer, 0, Layer == HalfX ? Slab : Planks);
		}
	}

	// ---- end city --------------------------------------------------------------------------------------------
	// (minecraft.wiki, "End city"): from templates in the jar. A tower of floors, a smaller tower or two joined
	// to it by bridges, and sometimes a ship at the end of one.

	struct FTemplatePlacer
	{
		FEnv& Env;
		FMcRandom& R;
		EMcRotation Rotation;

		// The template centred on a column, its bottom at Y. Returns its height.
		int32 Put(const TCHAR* Id, int32 X, int32 Y, int32 Z)
		{
			const TSharedPtr<const FMcStructureTemplate> Template = FMcStructureTemplate::Find(Id);
			if (!Template.IsValid())
			{
				return 0;
			}
			FMcTemplatePlaceSettings Settings;
			Settings.Rotation = Rotation;
			Settings.bClip = true;
			Settings.ClipMin = Env.ClipMin;
			Settings.ClipMax = Env.ClipMax;
			Settings.Pivot = FIntVector(Template->Size.X / 2, 0, Template->Size.Z / 2);
			FMcRandom Local = R;
			Template->Place(Env.Region, FIntVector(X, Y, Z) - Settings.Pivot, Settings, Local);
			return Template->Size.Y;
		}
	};
}

namespace McProcedural
{
	void BuildMineshaft(FEnv& Env, FMcRandom& Random, bool bMesa)
	{
		FDraw D{ Env };
		const int32 X = Env.CentreX();
		const int32 Z = Env.CentreZ();
		int32 Y;
		if (bMesa)
		{
			Y = FMath::Max(Env.MinY + 16, Env.Floor(X, Z) - 6 - Random.NextInt(5));
		}
		else
		{
			Y = FMath::Max(Env.MinY + 12, 8 + Random.NextInt(34));
		}
		FMineshaft Shaft(D, Random, bMesa, FIntVector(X, Y, Z));
		Shaft.Room(FFrame{ FIntVector(X, Y, Z), Random.NextInt(4) }, 0);
	}

	void BuildStronghold(FEnv& Env, FMcRandom& Random)
	{
		FDraw D{ Env };
		const int32 X = Env.CentreX();
		const int32 Z = Env.CentreZ();
		const int32 Y = FMath::Max(Env.MinY + 12, 10 + Random.NextInt(30));
		FStronghold Hold(D, Random, FIntVector(X, Y, Z));
		Hold.Start(FFrame{ FIntVector(X, Y, Z), Random.NextInt(4) });
		Hold.Finish();
	}

	void BuildMonument(FEnv& Env, FMcRandom& Random)
	{
		FDraw D{ Env };
		MonumentBuild(D, Random, Env.CentreX(), Env.CentreZ(), Env.SeaLevel - 24);
	}

	void BuildFortress(FEnv& Env, FMcRandom& Random)
	{
		FDraw D{ Env };
		const int32 X = Env.CentreX();
		const int32 Z = Env.CentreZ();
		const int32 Y = 48 + Random.NextInt(16);
		FFortress Fort(D, Random, FIntVector(X, Y, Z));
		const FFrame Start{ FIntVector(X, Y, Z), Random.NextInt(4) };
		Fort.Crossing(Start, INDEX_NONE);
	}

	void BuildMansion(FEnv& Env, FMcRandom& Random)
	{
		FDraw D{ Env };
		const int32 X = Env.CentreX();
		const int32 Z = Env.CentreZ();
		MansionBuild(D, Random, X, Z, Env.Floor(X, Z));
	}

	void BuildEndCity(FEnv& Env, FMcRandom& Random)
	{
		const int32 X = Env.CentreX();
		const int32 Z = Env.CentreZ();
		const int32 Ground = Env.Floor(X, Z);
		FTemplatePlacer Placer{ Env, Random, static_cast<EMcRotation>(Random.NextInt(4)) };
		const int32 BridgeCount = 2 + Random.NextInt(3);
		const bool bShip = Random.NextInt(2) == 0;
		const int32 Towers = 1 + Random.NextInt(2);
		// The main tower: floors stacked on a base, each a little wider than the one under.
		int32 Y = Ground - 1;
		Y += Placer.Put(TEXT("end_city/base_floor"), X, Y, Z);
		Y += Placer.Put(TEXT("end_city/base_roof"), X, Y, Z);
		const int32 SecondY = Y;
		Y += Placer.Put(Random.NextInt(2) ? TEXT("end_city/second_floor_1") : TEXT("end_city/second_floor_2"), X, Y, Z);
		Y += Placer.Put(TEXT("end_city/second_roof"), X, Y, Z);
		if (Random.NextInt(3) > 0)
		{
			Y += Placer.Put(Random.NextInt(2) ? TEXT("end_city/third_floor_1") : TEXT("end_city/third_floor_2"), X, Y, Z);
			Placer.Put(TEXT("end_city/third_roof"), X, Y, Z);
		}
		// Towers off a bridge from the second floor, each shorter than the one before.
		const FIntVector Heading = McTemplate::TransformPos(FIntVector(0, 0, 1), Placer.Rotation, EMcMirror::None);
		int32 FromX = X;
		int32 FromZ = Z;
		int32 BridgeY = SecondY;
		for (int32 Tower = 0; Tower < Towers; ++Tower)
		{
			const int32 Half = Tower == 0 ? 6 : 3;
			int32 AtX = FromX + Heading.X * Half;
			int32 AtZ = FromZ + Heading.Z * Half;
			for (int32 Piece = 0; Piece < BridgeCount; ++Piece)
			{
				Placer.Put(TEXT("end_city/bridge_piece"), AtX + Heading.X * 2, BridgeY, AtZ + Heading.Z * 2);
				AtX += Heading.X * 4;
				AtZ += Heading.Z * 4;
			}
			Placer.Put(TEXT("end_city/bridge_end"), AtX + Heading.X, BridgeY, AtZ + Heading.Z);
			const int32 TowerX = AtX + Heading.X * 5;
			const int32 TowerZ = AtZ + Heading.Z * 5;
			int32 TowerY = BridgeY - 3;
			TowerY += Placer.Put(TEXT("end_city/tower_base"), TowerX, TowerY, TowerZ);
			const int32 Pieces = 1 + Random.NextInt(3);
			for (int32 Piece = 0; Piece < Pieces; ++Piece)
			{
				TowerY += Placer.Put(Piece == Pieces - 1 ? TEXT("end_city/tower_floor") : TEXT("end_city/tower_piece"), TowerX, TowerY, TowerZ);
			}
			Placer.Put(TEXT("end_city/tower_top"), TowerX, TowerY, TowerZ);
			if (bShip && Tower == Towers - 1)
			{
				Placer.Put(TEXT("end_city/ship"), TowerX + Heading.X * 12, BridgeY + 2, TowerZ + Heading.Z * 12);
			}
			FromX = TowerX;
			FromZ = TowerZ;
			BridgeY += 4;
		}
	}

	void BuildNetherFossil(FEnv& Env, FMcRandom& Random)
	{
		const int32 Fossil = 1 + Random.NextInt(14);
		const int32 X = Env.Start.X * 16 + Random.NextInt(16);
		const int32 Z = Env.Start.Y * 16 + Random.NextInt(16);
		const int32 TopY = 32 + Random.NextInt(Env.MaxY - 34 - 32);
		const EMcRotation Rotation = McTemplate::RandomRotation(Random);
		// Down from the chosen height to the first floor of the cavern.
		int32 Y = TopY;
		FMcGenRegion& Region = Env.Region;
		if (!Region.Contains(X, Z))
		{
			return;
		}
		while (Y > Env.MinY + 33 && !(Region.IsAir(Region.GetBlock(FIntVector(X, Y, Z))) && !Region.IsAir(Region.GetBlock(FIntVector(X, Y - 1, Z))) && !Region.IsAir(Region.GetBlock(FIntVector(X, Y - 2, Z)))))
		{
			--Y;
		}
		if (Y <= Env.MinY + 33)
		{
			return;
		}
		const TSharedPtr<const FMcStructureTemplate> Template = FMcStructureTemplate::Find(FString::Printf(TEXT("nether_fossils/fossil_%d"), Fossil));
		if (!Template.IsValid())
		{
			return;
		}
		FMcTemplatePlaceSettings Settings;
		Settings.Rotation = Rotation;
		Settings.bClip = true;
		Settings.ClipMin = Env.ClipMin;
		Settings.ClipMax = Env.ClipMax;
		Settings.Pivot = FIntVector(Template->Size.X / 2, 0, Template->Size.Z / 2);
		Settings.bSkipAir = true;
		Template->Place(Region, FIntVector(X, Y - Template->Size.Y / 2, Z) - Settings.Pivot, Settings, Random);
	}

	void StrongholdChunks(uint64 Seed, int32 Count, int32 Distance, int32 Spread, TArray<FIntPoint>& Out)
	{
		FMcRandom Random = FMcRandom(Seed).Fork(Count, Distance, Spread);
		double Angle = Random.NextDouble() * PI * 2.0;
		int32 InRing = 0;
		int32 Ring = 0;
		int32 RingSize = Spread;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Reach = (4.0 * Distance + Distance * Ring * 6.0) + (Random.NextDouble() - 0.5) * Distance * 2.5;
			Out.Add(FIntPoint(FMath::RoundToInt32(FMath::Cos(Angle) * Reach), FMath::RoundToInt32(FMath::Sin(Angle) * Reach)));
			Angle += PI * 2.0 / RingSize;
			if (++InRing == RingSize)
			{
				++Ring;
				InRing = 0;
				RingSize = FMath::Min(RingSize + 2 * RingSize / (Ring + 1), Count - Index - 1);
				if (RingSize <= 0)
				{
					break;
				}
				Angle += Random.NextDouble() * PI * 2.0;
			}
		}
	}
}
