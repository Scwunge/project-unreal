#include "McHdTextures.h"

namespace
{
	using namespace McHd;

	// ---- The scaler ----

	// Wide: close enough to blend with. Tight: the same colour for the corner rule.
	constexpr int32 BlendDistance = 56;
	constexpr int32 SameDistance = 14;

	FColor GetPixel(const FPicture& Picture, int32 X, int32 Y, bool bWrap)
	{
		if (bWrap)
		{
			X = ((X % Picture.Width) + Picture.Width) % Picture.Width;
			Y = ((Y % Picture.Height) + Picture.Height) % Picture.Height;
		}
		else
		{
			X = FMath::Clamp(X, 0, Picture.Width - 1);
			Y = FMath::Clamp(Y, 0, Picture.Height - 1);
		}
		return Picture.Pixels[Y * Picture.Width + X];
	}

	bool IsSame(const FColor& A, const FColor& B)
	{
		return GetColourDistance(A, B) <= SameDistance;
	}

	// The sub-pixel of E that lies towards two neighbours Side1 and Side2 and the diagonal one between them:
	// bilinear weights 9:3:3:1, but a neighbour too different from E counts as E.
	FColor BlendCorner(const FColor& E, const FColor& Side1, const FColor& Side2, const FColor& Diagonal)
	{
		const FColor* Parts[4] = { &E, &Side1, &Side2, &Diagonal };
		const int32 Weights[4] = { 9, 3, 3, 1 };
		int32 Sum[4] = { 0, 0, 0, 0 };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FColor& Part = (Index == 0 || GetColourDistance(E, *Parts[Index]) <= BlendDistance) ? *Parts[Index] : E;
			Sum[0] += Part.R * Weights[Index];
			Sum[1] += Part.G * Weights[Index];
			Sum[2] += Part.B * Weights[Index];
			Sum[3] += Part.A * Weights[Index];
		}
		return FColor(static_cast<uint8>((Sum[0] + 8) / 16), static_cast<uint8>((Sum[1] + 8) / 16),
			static_cast<uint8>((Sum[2] + 8) / 16), static_cast<uint8>((Sum[3] + 8) / 16));
	}

	// ---- Noise ----

	uint32 Hash(uint32 X, uint32 Y, uint32 Seed)
	{
		uint32 H = X * 374761393u + Y * 668265263u + Seed * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return H ^ (H >> 16);
	}

	float Hash01(uint32 X, uint32 Y, uint32 Seed)
	{
		return static_cast<float>(Hash(X, Y, Seed) & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	// Value noise on a grid of CellsX x CellsY cells over the unit square that wraps, 0..1.
	float Noise(float U, float V, int32 CellsX, int32 CellsY, uint32 Seed)
	{
		const float X = U * CellsX;
		const float Y = V * CellsY;
		const int32 X0 = FMath::FloorToInt32(X);
		const int32 Y0 = FMath::FloorToInt32(Y);
		const float Fx = X - X0;
		const float Fy = Y - Y0;
		const float Sx = Fx * Fx * (3.0f - 2.0f * Fx);
		const float Sy = Fy * Fy * (3.0f - 2.0f * Fy);
		auto Cell = [&](int32 Cx, int32 Cy)
		{
			return Hash01(static_cast<uint32>(((Cx % CellsX) + CellsX) % CellsX), static_cast<uint32>(((Cy % CellsY) + CellsY) % CellsY), Seed);
		};
		return FMath::Lerp(FMath::Lerp(Cell(X0, Y0), Cell(X0 + 1, Y0), Sx), FMath::Lerp(Cell(X0, Y0 + 1), Cell(X0 + 1, Y0 + 1), Sx), Sy);
	}

	// -1..1
	float Signed(float U, float V, int32 CellsX, int32 CellsY, uint32 Seed)
	{
		return Noise(U, V, CellsX, CellsY, Seed) * 2.0f - 1.0f;
	}

	float Luma(const FColor& Colour)
	{
		return (0.299f * Colour.R + 0.587f * Colour.G + 0.114f * Colour.B) / 255.0f;
	}

	float SmoothStep(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / (B - A), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	// ---- The classes ----

	struct FClassParams
	{
		float DetailHeight;   // how much of the height is the class's detail
		float AlbedoAmount;   // how much the detail darkens and brightens the colour
		float NormalStrength;
		uint8 Smoothness;
		uint8 F0;
	};

	FClassParams GetParams(EMaterialClass Class)
	{
		switch (Class)
		{
		case EMaterialClass::Stone: return { 1.0f, 0.10f, 2.0f, 55, 12 };
		case EMaterialClass::Wood: return { 1.0f, 0.08f, 1.6f, 50, 10 };
		case EMaterialClass::Soil: return { 1.0f, 0.12f, 1.4f, 25, 8 };
		case EMaterialClass::Metal: return { 1.0f, 0.08f, 1.0f, 190, 250 };
		case EMaterialClass::Ore: return { 1.0f, 0.10f, 2.0f, 55, 12 };
		case EMaterialClass::Gem: return { 1.0f, 0.05f, 1.5f, 225, 14 };
		case EMaterialClass::Leaves: return { 1.0f, 0.10f, 1.2f, 40, 8 };
		case EMaterialClass::Plant: return { 1.0f, 0.08f, 1.0f, 45, 8 };
		case EMaterialClass::Wool: return { 1.0f, 0.06f, 1.0f, 10, 5 };
		case EMaterialClass::Glass: return { 0.0f, 0.0f, 0.0f, 240, 18 };
		case EMaterialClass::Fabric: return { 0.35f, 0.02f, 0.3f, 30, 8 };
		default: return { 0.6f, 0.05f, 1.2f, 60, 10 };
		}
	}

	bool HasWord(const FString& Name, const TCHAR* Word)
	{
		return Name.Contains(Word, ESearchCase::CaseSensitive);
	}

	bool HasWord(const FString& Name, const char* Word)
	{
		return Name.Contains(FString(ANSI_TO_TCHAR(Word)), ESearchCase::CaseSensitive);
	}

	// The class's detail at a point, -1..1. Every pattern is periodic over the unit square, so it tiles.
	float DetailAt(EMaterialClass Class, const FString& Name, float U, float V, float PixelU, float PixelV, uint32 Seed)
	{
		switch (Class)
		{
		case EMaterialClass::Stone:
		case EMaterialClass::Ore:
		{
			// Fine grain, small pits where a fine noise is low, and the odd chip where another is high.
			float D = 0.22f * Signed(U, V, 48, 48, Seed) + 0.12f * Signed(U, V, 96, 96, Seed + 1);
			D -= 0.7f * SmoothStep(0.18f, 0.0f, Noise(U, V, 56, 56, Seed + 2));
			D += 0.35f * SmoothStep(0.88f, 1.0f, Noise(U, V, 40, 40, Seed + 3));
			return FMath::Clamp(D, -1.0f, 1.0f);
		}
		case EMaterialClass::Gem:
		{
			// Facets: a coarse noise in steps.
			const float N = Noise(U, V, 5, 5, Seed);
			return FMath::FloorToFloat(N * 4.0f) / 4.0f * 2.0f - 0.75f + 0.1f * Signed(U, V, 16, 16, Seed + 1);
		}
		case EMaterialClass::Wood:
		{
			if (HasWord(Name, "_top") && (HasWord(Name, "log") || HasWord(Name, "stem") || HasWord(Name, "wood") || HasWord(Name, "hyphae")))
			{
				// The end of a log: rings round the middle, wobbling a little.
				const float R = FMath::Sqrt((U - 0.5f) * (U - 0.5f) + (V - 0.5f) * (V - 0.5f)) + 0.03f * Signed(U, V, 4, 4, Seed);
				return 0.7f * FMath::Sin(R * 2.0f * PI * 5.0f) + 0.15f * Signed(U, V, 16, 16, Seed + 1);
			}
			if (HasWord(Name, "log") || HasWord(Name, "stem") || HasWord(Name, "wood") || HasWord(Name, "hyphae") || HasWord(Name, "bamboo"))
			{
				// Bark: furrows along the trunk.
				return 0.8f * Signed(U, V, 14, 2, Seed) + 0.25f * Signed(U, V, 28, 6, Seed + 1);
			}
			// Planks: boards a quarter high with seams, and grain along them.
			const float Board = FMath::Frac(V * 4.0f);
			const float Seam = SmoothStep(0.06f, 0.0f, Board);
			return 0.55f * Signed(U + 0.31f * FMath::FloorToFloat(V * 4.0f), V, 2, 48, Seed) + 0.2f * Signed(U, V, 6, 24, Seed + 1) - 0.9f * Seam;
		}
		case EMaterialClass::Soil:
		{
			if (HasWord(Name, "gravel"))
			{
				const float Pebble = Signed(U, V, 8, 8, Seed);
				return FMath::Sign(Pebble) * FMath::Sqrt(FMath::Abs(Pebble)) * 0.9f + 0.2f * Signed(U, V, 24, 24, Seed + 1);
			}
			return 0.55f * Signed(U, V, 28, 28, Seed) + 0.35f * Signed(U, V, 14, 14, Seed + 1);
		}
		case EMaterialClass::Metal:
		{
			// Brushed: long streaks along the surface and a faint wave across them.
			return 0.4f * Signed(U, V, 2, 64, Seed) + 0.12f * Signed(U, V, 8, 3, Seed + 1);
		}
		case EMaterialClass::Leaves:
		case EMaterialClass::Plant:
		{
			// Veins: a diagonal lattice and a midrib, over a soft mottling.
			const float Lattice = FMath::Abs(FMath::Frac((U + V) * 4.0f) - 0.5f);
			const float Rib = FMath::Abs(U - 0.5f);
			float D = 0.35f * Signed(U, V, 10, 10, Seed);
			D += 0.55f * SmoothStep(0.07f, 0.0f, Lattice);
			D += 0.5f * SmoothStep(0.04f, 0.0f, Rib);
			return FMath::Clamp(D, -1.0f, 1.0f);
		}
		case EMaterialClass::Wool:
		{
			// Fibre: two crossed sets of short streaks.
			return FMath::Clamp(0.5f * Signed(U, V, 48, 8, Seed) + 0.5f * Signed(U, V, 8, 48, Seed + 1) + 0.2f * Signed(U, V, 32, 32, Seed + 2), -1.0f, 1.0f);
		}
		case EMaterialClass::Fabric:
		{
			// A weave one source pixel wide, and a little mottling.
			return 0.45f * FMath::Sin(PixelU * 2.0f * PI) * FMath::Sin(PixelV * 2.0f * PI) + 0.25f * Signed(U, V, 64, 64, Seed);
		}
		case EMaterialClass::Glass:
			return 0.0f;
		default:
			return 0.5f * Signed(U, V, 16, 16, Seed);
		}
	}

	void BoxBlur(TArray<float>& Field, int32 Width, int32 Height, int32 Radius, bool bWrap)
	{
		if (Radius <= 0)
		{
			return;
		}
		TArray<float> Pass;
		Pass.SetNumUninitialized(Field.Num());
		auto Index = [&](int32 X, int32 Y)
		{
			if (bWrap)
			{
				X = ((X % Width) + Width) % Width;
				Y = ((Y % Height) + Height) % Height;
			}
			else
			{
				X = FMath::Clamp(X, 0, Width - 1);
				Y = FMath::Clamp(Y, 0, Height - 1);
			}
			return Y * Width + X;
		};
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				float Sum = 0.0f;
				for (int32 Dx = -Radius; Dx <= Radius; ++Dx)
				{
					Sum += Field[Index(X + Dx, Y)];
				}
				Pass[Y * Width + X] = Sum / (2 * Radius + 1);
			}
		}
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				float Sum = 0.0f;
				for (int32 Dy = -Radius; Dy <= Radius; ++Dy)
				{
					Sum += Pass[Index(X, Y + Dy)];
				}
				Field[Y * Width + X] = Sum / (2 * Radius + 1);
			}
		}
	}

	uint8 ToByte(float Value)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Value * 255.0f), 0, 255));
	}

	FMaps MakeMaps(const FPicture& Base, EMaterialClass Class, uint32 Seed, bool bEmissive, bool bWrap, bool bAddDetail, const FString& Name, int32 SourceWidth, int32 SourceHeight)
	{
		const int32 Width = Base.Width;
		const int32 Height = Base.Height;
		const int32 Count = Width * Height;
		const FClassParams Params = GetParams(Class);
		const float Scale = static_cast<float>(Width) / FMath::Max(SourceWidth, 1);

		FMaps Maps;
		Maps.Albedo = Base;
		Maps.Normal.Width = Maps.Specular.Width = Width;
		Maps.Normal.Height = Maps.Specular.Height = Height;
		Maps.Normal.Pixels.SetNumUninitialized(Count);
		Maps.Specular.Pixels.SetNumUninitialized(Count);

		// What the ore's gems are: the pixels far from the stone's own colour (the commonest, as the median).
		TArray<float> Gem;
		Gem.Init(0.0f, Count);
		if (Class == EMaterialClass::Ore)
		{
			TArray<uint8> Reds, Greens, Blues;
			for (const FColor& Pixel : Base.Pixels)
			{
				if (Pixel.A > 128)
				{
					Reds.Add(Pixel.R);
					Greens.Add(Pixel.G);
					Blues.Add(Pixel.B);
				}
			}
			if (Reds.Num() > 0)
			{
				Reds.Sort();
				Greens.Sort();
				Blues.Sort();
				const FColor Median(Reds[Reds.Num() / 2], Greens[Greens.Num() / 2], Blues[Blues.Num() / 2], 255);
				for (int32 Index = 0; Index < Count; ++Index)
				{
					Gem[Index] = SmoothStep(45.0f, 85.0f, static_cast<float>(GetColourDistance(Base.Pixels[Index], Median)));
				}
				BoxBlur(Gem, Width, Height, FMath::Max(1, FMath::RoundToInt32(Scale * 0.25f)), bWrap);
			}
		}

		// The class's detail, over the unit square.
		TArray<float> Detail;
		Detail.Init(0.0f, Count);
		if (bAddDetail && Params.DetailHeight > 0.0f)
		{
			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					const float U = (X + 0.5f) / Width;
					const float V = (Y + 0.5f) / Height;
					float D = DetailAt(Class, Name, U, V, U * SourceWidth, V * SourceHeight, Seed);
					if (Class == EMaterialClass::Ore)
					{
						// The gems stand proud of the stone and take none of its pits.
						const float G = Gem[Y * Width + X];
						D = FMath::Lerp(D, 0.6f, G);
					}
					Detail[Y * Width + X] = D;
				}
			}
		}

		// Albedo: the detail darkens the hollows and brightens the ridges a little.
		if (bAddDetail && Params.AlbedoAmount > 0.0f)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const float Keep = Class == EMaterialClass::Ore ? 1.0f - Gem[Index] : 1.0f;
				const float Factor = 1.0f + Params.AlbedoAmount * Detail[Index] * Keep;
				FColor& Pixel = Maps.Albedo.Pixels[Index];
				Pixel.R = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Pixel.R * Factor), 0, 255));
				Pixel.G = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Pixel.G * Factor), 0, 255));
				Pixel.B = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Pixel.B * Factor), 0, 255));
			}
		}

		// Height: each texel is a small embossed tile (a rounded bevel towards its edges), lifted by its own brightness,
		// with the class's fine detail on top. The texel's brightness is not blurred, so the dark lines of a texture
		// (the mortar of bricks) are grooves.
		const float Bevel = Class == EMaterialClass::Glass ? 0.0f : Class == EMaterialClass::Fabric ? 0.5f
			: (Class == EMaterialClass::Leaves || Class == EMaterialClass::Plant || Class == EMaterialClass::Soil || Class == EMaterialClass::Generic || Class == EMaterialClass::Wool) ? 0.7f : 1.0f;
		// Hard things have texels at slightly different heights; soft ones (grass, dirt, leaves, wool) only catch light on the texel edges.
		const float JitterAmount = (Class == EMaterialClass::Stone || Class == EMaterialClass::Ore || Class == EMaterialClass::Metal || Class == EMaterialClass::Gem) ? 0.15f
			: Class == EMaterialClass::Wood ? 0.1f : 0.0f;
		const float TexelX = static_cast<float>(Width) / FMath::Max(SourceWidth, 1);
		const float TexelY = static_cast<float>(Height) / FMath::Max(SourceHeight, 1);
		TArray<float> Cavity;
		Cavity.SetNumUninitialized(Count);
		TArray<float> HeightField;
		HeightField.SetNumUninitialized(Count);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = Y * Width + X;
				const FColor& Pixel = Base.Pixels[Index];
				const float Fx = FMath::Frac((X + 0.5f) / TexelX);
				const float Fy = FMath::Frac((Y + 0.5f) / TexelY);
				const float Edge = FMath::Min(FMath::Min(Fx, 1.0f - Fx), FMath::Min(Fy, 1.0f - Fy)) * 2.0f;
				const float Dome = SmoothStep(0.0f, 0.55f, Edge);
				const float Lift = Pixel.A < 10 ? 0.0f : Luma(Pixel);
				// Every texel sits a little higher or lower than its neighbours (a hash of which texel it is).
				const float Jitter = (Hash01(static_cast<uint32>(X / FMath::Max(FMath::RoundToInt32(TexelX), 1)), static_cast<uint32>(Y / FMath::Max(FMath::RoundToInt32(TexelY), 1)), Seed + 9) - 0.5f) * JitterAmount;
				Cavity[Index] = Lift * 0.6f + 0.3f + Detail[Index] * 0.25f * Params.DetailHeight + Jitter;
				HeightField[Index] = FMath::Clamp(Cavity[Index] * 0.7f + Dome * 0.38f * Bevel, 0.0f, 1.0f);
			}
		}
		if (Class == EMaterialClass::Ore)
		{
			// Real relief: the stone is sunk and the gems stand well above it.
			for (int32 Index = 0; Index < Count; ++Index)
			{
				HeightField[Index] = FMath::Lerp(HeightField[Index] * 0.6f, 0.95f, Gem[Index]);
			}
		}
		NormalFromHeight(HeightField, Width, Height, Params.NormalStrength * Scale * 0.45f, Maps.Normal.Pixels);

		// Occlusion: the darker and lower parts against a few texels around them (the bevels don't count), kept gentle
		// so a face is never much darker than without it.
		TArray<float> Around = Cavity;
		BoxBlur(Around, Width, Height, FMath::Max(2, FMath::RoundToInt32(Scale * 1.5f)), bWrap);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float Open = FMath::Clamp(1.0f - (Around[Index] - Cavity[Index]) * 2.3f, 0.62f, 1.0f);
			Maps.Normal.Pixels[Index].B = ToByte(Open);
		}

		// Specular: smoothness and F0 by class, with ore's gems shiny and brightness-based emission.
		const bool bMetalGems = Name.Contains(TEXT("iron")) || Name.Contains(TEXT("gold")) || Name.Contains(TEXT("copper"));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			float Smooth = Params.Smoothness;
			float F0 = Params.F0;
			if (Class == EMaterialClass::Ore)
			{
				Smooth = FMath::Lerp(Smooth, 215.0f, Gem[Index]);
				F0 = FMath::Lerp(F0, bMetalGems ? 250.0f : 14.0f, Gem[Index] > 0.5f ? 1.0f : 0.0f);
			}
			else if (Class == EMaterialClass::Metal)
			{
				// The brushing scratches the shine.
				Smooth = FMath::Clamp(Smooth - 40.0f * FMath::Abs(Detail[Index]), 0.0f, 255.0f);
			}
			uint8 Emissive = 255;
			if (bEmissive && Base.Pixels[Index].A > 128)
			{
				Emissive = static_cast<uint8>(255 - FMath::RoundToInt32(SmoothStep(0.3f, 0.75f, Luma(Base.Pixels[Index])) * 254.0f));
			}
			Maps.Specular.Pixels[Index] = FColor(static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Smooth), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(F0), 0, 255)), 0, Emissive);
		}
		return Maps;
	}

	FString StripId(const FString& TextureId)
	{
		FString Name = TextureId.ToLower();
		int32 Colon = INDEX_NONE;
		if (Name.FindChar(TEXT(':'), Colon))
		{
			Name = Name.Mid(Colon + 1);
		}
		return Name;
	}

	// "oak_log_top" -> "oak_log": the block a texture of a face belongs to.
	FString StripFace(const FString& Name)
	{
		static const TCHAR* const Faces[] = { TEXT("_top"), TEXT("_side"), TEXT("_bottom"), TEXT("_front"), TEXT("_back"), TEXT("_end"),
			TEXT("_on"), TEXT("_off"), TEXT("_inner"), TEXT("_lit"), TEXT("_stage0"), TEXT("_stage1"), TEXT("_stage2"), TEXT("_stage3") };
		FString Result = Name;
		bool bChanged = true;
		while (bChanged)
		{
			bChanged = false;
			for (const TCHAR* Face : Faces)
			{
				if (Result.EndsWith(Face))
				{
					Result.LeftChopInline(FCString::Strlen(Face));
					bChanged = true;
				}
			}
		}
		return Result;
	}
}

// ---- Scaling ----

int32 McHd::GetColourDistance(const FColor& A, const FColor& B)
{
	return FMath::Max(FMath::Max(FMath::Abs(static_cast<int32>(A.R) - B.R), FMath::Abs(static_cast<int32>(A.G) - B.G)),
		FMath::Max(FMath::Abs(static_cast<int32>(A.B) - B.B), FMath::Abs(static_cast<int32>(A.A) - B.A)));
}

McHd::FPicture McHd::Upscale2x(const FPicture& Source, bool bWrap)
{
	FPicture Out;
	if (Source.Width <= 0 || Source.Height <= 0 || Source.Pixels.Num() != Source.Width * Source.Height)
	{
		return Out;
	}
	Out.Width = Source.Width * 2;
	Out.Height = Source.Height * 2;
	Out.Pixels.SetNumUninitialized(Out.Width * Out.Height);
	for (int32 Y = 0; Y < Source.Height; ++Y)
	{
		for (int32 X = 0; X < Source.Width; ++X)
		{
			const FColor A = GetPixel(Source, X - 1, Y - 1, bWrap);
			const FColor B = GetPixel(Source, X, Y - 1, bWrap);
			const FColor C = GetPixel(Source, X + 1, Y - 1, bWrap);
			const FColor D = GetPixel(Source, X - 1, Y, bWrap);
			const FColor E = GetPixel(Source, X, Y, bWrap);
			const FColor F = GetPixel(Source, X + 1, Y, bWrap);
			const FColor G = GetPixel(Source, X - 1, Y + 1, bWrap);
			const FColor H = GetPixel(Source, X, Y + 1, bWrap);
			const FColor I = GetPixel(Source, X + 1, Y + 1, bWrap);

			// Scale2x's rule with colours that are alike counting as equal: where two neighbours at right angles agree
			// and the opposite two don't, the corner takes their colour, which rounds a diagonal edge.
			FColor Corner[4] = { E, E, E, E };
			if (!IsSame(B, H) && !IsSame(D, F))
			{
				if (IsSame(D, B)) { Corner[0] = D; }
				if (IsSame(B, F)) { Corner[1] = F; }
				if (IsSame(D, H)) { Corner[2] = D; }
				if (IsSame(H, F)) { Corner[3] = F; }
			}
			// Then each sub-pixel blends with the neighbours towards it that are not across an edge.
			Out.Pixels[(Y * 2) * Out.Width + X * 2] = BlendCorner(Corner[0], B, D, A);
			Out.Pixels[(Y * 2) * Out.Width + X * 2 + 1] = BlendCorner(Corner[1], B, F, C);
			Out.Pixels[(Y * 2 + 1) * Out.Width + X * 2] = BlendCorner(Corner[2], H, D, G);
			Out.Pixels[(Y * 2 + 1) * Out.Width + X * 2 + 1] = BlendCorner(Corner[3], H, F, I);
		}
	}
	return Out;
}

McHd::FPicture McHd::Shrink(const FPicture& Source, int32 Width, int32 Height, bool bPlain)
{
	FPicture Out;
	Out.Width = Width;
	Out.Height = Height;
	Out.Pixels.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const int32 Y0 = Y * Source.Height / Height;
		const int32 Y1 = FMath::Max(Y0 + 1, (Y + 1) * Source.Height / Height);
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 X0 = X * Source.Width / Width;
			const int32 X1 = FMath::Max(X0 + 1, (X + 1) * Source.Width / Width);
			int64 R = 0, G = 0, B = 0, A = 0;
			int32 Count = 0;
			for (int32 Sy = Y0; Sy < Y1; ++Sy)
			{
				for (int32 Sx = X0; Sx < X1; ++Sx)
				{
					const FColor& Pixel = Source.Pixels[Sy * Source.Width + Sx];
					// Transparent pixels carry no colour of their own (unless these are maps).
					const int32 Weight = bPlain ? 1 : Pixel.A;
					R += Pixel.R * Weight;
					G += Pixel.G * Weight;
					B += Pixel.B * Weight;
					A += Pixel.A;
					++Count;
				}
			}
			FColor& Pixel = Out.Pixels[Y * Width + X];
			if (bPlain)
			{
				Pixel = FColor(static_cast<uint8>(R / Count), static_cast<uint8>(G / Count), static_cast<uint8>(B / Count), static_cast<uint8>(A / Count));
			}
			else if (A == 0)
			{
				// Opaque black would be as good as any colour; the average of the covered colours keeps edges clean.
				Pixel = FColor(0, 0, 0, 0);
			}
			else
			{
				Pixel = FColor(static_cast<uint8>(R / A), static_cast<uint8>(G / A), static_cast<uint8>(B / A), static_cast<uint8>(A / Count));
			}
		}
	}
	return Out;
}

McHd::FPicture McHd::ScaleNearest(const FPicture& Source, int32 Width, int32 Height)
{
	FPicture Out;
	if (Source.Width <= 0 || Source.Height <= 0 || Width <= 0 || Height <= 0)
	{
		return Out;
	}
	Out.Width = Width;
	Out.Height = Height;
	Out.Pixels.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const int32 Sy = FMath::Min(Y * Source.Height / Height, Source.Height - 1);
		for (int32 X = 0; X < Width; ++X)
		{
			Out.Pixels[Y * Width + X] = Source.Pixels[Sy * Source.Width + FMath::Min(X * Source.Width / Width, Source.Width - 1)];
		}
	}
	return Out;
}

McHd::FPicture McHd::ScaleTo(const FPicture& Source, int32 Width, int32 Height, bool bWrap)
{
	if (Source.Width <= 0 || Source.Height <= 0 || Width <= 0 || Height <= 0)
	{
		return FPicture();
	}
	if (Width == Source.Width && Height == Source.Height)
	{
		return Source;
	}
	if (Width < Source.Width || Height < Source.Height)
	{
		return Shrink(Source, Width, Height);
	}
	FPicture Current = Source;
	// Doubling while it stays at or under the target in both directions.
	while (Current.Width * 2 <= Width && Current.Height * 2 <= Height)
	{
		Current = Upscale2x(Current, bWrap);
	}
	if (Current.Width == Width && Current.Height == Height)
	{
		return Current;
	}
	// The rest (not a power of two): edge-aware bilinear.
	FPicture Out;
	Out.Width = Width;
	Out.Height = Height;
	Out.Pixels.SetNumUninitialized(Width * Height);
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const float Sy = (Y + 0.5f) * Current.Height / Height - 0.5f;
		const int32 Y0 = FMath::FloorToInt32(Sy);
		const float Fy = Sy - Y0;
		for (int32 X = 0; X < Width; ++X)
		{
			const float Sx = (X + 0.5f) * Current.Width / Width - 0.5f;
			const int32 X0 = FMath::FloorToInt32(Sx);
			const float Fx = Sx - X0;
			const FColor Centre = GetPixel(Current, Fx < 0.5f ? X0 : X0 + 1, Fy < 0.5f ? Y0 : Y0 + 1, bWrap);
			float Sum[4] = { 0, 0, 0, 0 };
			float Weight = 0.0f;
			for (int32 Dy = 0; Dy < 2; ++Dy)
			{
				for (int32 Dx = 0; Dx < 2; ++Dx)
				{
					FColor Pixel = GetPixel(Current, X0 + Dx, Y0 + Dy, bWrap);
					const float W = (Dx ? Fx : 1.0f - Fx) * (Dy ? Fy : 1.0f - Fy);
					if (GetColourDistance(Pixel, Centre) > BlendDistance)
					{
						Pixel = Centre;
					}
					Sum[0] += Pixel.R * W;
					Sum[1] += Pixel.G * W;
					Sum[2] += Pixel.B * W;
					Sum[3] += Pixel.A * W;
					Weight += W;
				}
			}
			Out.Pixels[Y * Width + X] = FColor(static_cast<uint8>(Sum[0] / Weight + 0.5f), static_cast<uint8>(Sum[1] / Weight + 0.5f),
				static_cast<uint8>(Sum[2] / Weight + 0.5f), static_cast<uint8>(Sum[3] / Weight + 0.5f));
		}
	}
	return Out;
}

// ---- Classes ----

McHd::EMaterialClass McHd::PickClass(const FString& TextureId, const FClassTags* Tags)
{
	const FString Name = StripId(TextureId);
	if (Name.StartsWith(TEXT("entity/")))
	{
		return EMaterialClass::Fabric;
	}
	FString Base = Name;
	int32 Slash = INDEX_NONE;
	if (Base.FindLastChar(TEXT('/'), Slash))
	{
		Base = Base.Mid(Slash + 1);
	}
	if (Tags)
	{
		for (const FString& Candidate : { Base, StripFace(Base) })
		{
			if (const EMaterialClass* Found = Tags->Find(FName(*Candidate)))
			{
				return *Found;
			}
		}
	}

	// Keywords. The order matters: ore before stone, glass before the rest.
	if (HasWord(Base, "_ore"))
	{
		return EMaterialClass::Ore;
	}
	if (HasWord(Base, "glass") || Base == TEXT("ice") || Base == TEXT("packed_ice") || Base == TEXT("blue_ice") || HasWord(Base, "frosted_ice"))
	{
		return EMaterialClass::Glass;
	}
	if (HasWord(Base, "leaves"))
	{
		return EMaterialClass::Leaves;
	}
	if (HasWord(Base, "wool") || HasWord(Base, "carpet"))
	{
		return EMaterialClass::Wool;
	}
	if (Base == TEXT("diamond_block") || Base == TEXT("emerald_block") || Base == TEXT("lapis_block") || HasWord(Base, "amethyst") || HasWord(Base, "quartz"))
	{
		return EMaterialClass::Gem;
	}
	if (Base == TEXT("iron_block") || Base == TEXT("gold_block") || Base == TEXT("netherite_block") || HasWord(Base, "copper") || HasWord(Base, "raw_iron")
		|| HasWord(Base, "raw_gold") || Base == TEXT("iron_bars") || HasWord(Base, "chain") || HasWord(Base, "anvil") || HasWord(Base, "cauldron") || HasWord(Base, "hopper"))
	{
		return EMaterialClass::Metal;
	}
	if (HasWord(Base, "planks") || HasWord(Base, "log") || HasWord(Base, "stem") || HasWord(Base, "hyphae") || HasWord(Base, "bamboo")
		|| HasWord(Base, "crafting_table") || HasWord(Base, "bookshelf") || HasWord(Base, "barrel") || HasWord(Base, "door") || HasWord(Base, "wood"))
	{
		return EMaterialClass::Wood;
	}
	if (HasWord(Base, "grass") || HasWord(Base, "fern") || HasWord(Base, "flower") || HasWord(Base, "sapling") || HasWord(Base, "vine") || HasWord(Base, "kelp")
		|| HasWord(Base, "seagrass") || HasWord(Base, "bush") || HasWord(Base, "wheat") || HasWord(Base, "carrots") || HasWord(Base, "potatoes")
		|| HasWord(Base, "beetroots") || HasWord(Base, "lily") || HasWord(Base, "moss") || HasWord(Base, "azalea") || HasWord(Base, "tulip")
		|| HasWord(Base, "poppy") || HasWord(Base, "dandelion") || HasWord(Base, "cactus") || HasWord(Base, "sugar_cane") || HasWord(Base, "roots"))
	{
		// Grass blocks themselves are ground.
		if (!HasWord(Base, "grass_block") && !HasWord(Base, "path"))
		{
			return EMaterialClass::Plant;
		}
		return EMaterialClass::Soil;
	}
	if (HasWord(Base, "dirt") || HasWord(Base, "sand") || HasWord(Base, "gravel") || HasWord(Base, "mud") || HasWord(Base, "clay") || HasWord(Base, "podzol")
		|| HasWord(Base, "mycelium") || HasWord(Base, "farmland") || HasWord(Base, "snow") || HasWord(Base, "soul_soil"))
	{
		return HasWord(Base, "sandstone") ? EMaterialClass::Stone : EMaterialClass::Soil;
	}
	if (HasWord(Base, "stone") || HasWord(Base, "cobble") || HasWord(Base, "deepslate") || HasWord(Base, "brick") || HasWord(Base, "andesite")
		|| HasWord(Base, "diorite") || HasWord(Base, "granite") || HasWord(Base, "tuff") || HasWord(Base, "basalt") || HasWord(Base, "blackstone")
		|| HasWord(Base, "terracotta") || HasWord(Base, "concrete") || HasWord(Base, "netherrack") || HasWord(Base, "obsidian") || HasWord(Base, "prismarine")
		|| HasWord(Base, "end_stone") || HasWord(Base, "purpur") || HasWord(Base, "calcite") || HasWord(Base, "bedrock") || HasWord(Base, "dripstone"))
	{
		return EMaterialClass::Stone;
	}
	return EMaterialClass::Generic;
}

bool McHd::IsEmissiveTexture(const FString& TextureId)
{
	const FString Name = StripId(TextureId);
	static const TCHAR* const Lights[] = { TEXT("glowstone"), TEXT("sea_lantern"), TEXT("shroomlight"), TEXT("froglight"), TEXT("lava"), TEXT("magma"),
		TEXT("jack_o_lantern"), TEXT("lantern"), TEXT("torch"), TEXT("fire"), TEXT("redstone_lamp_on"), TEXT("crying_obsidian"), TEXT("end_rod"),
		TEXT("beacon"), TEXT("conduit"), TEXT("ochre"), TEXT("campfire_log_lit"), TEXT("furnace_front_on"), TEXT("blast_furnace_front_on"),
		TEXT("smoker_front_on"), TEXT("lit_redstone_ore"), TEXT("redstone_torch"), TEXT("glow_lichen"), TEXT("candle_lit"), TEXT("respawn_anchor") };
	if (Name.Contains(TEXT("unlit")))
	{
		return false;
	}
	for (const TCHAR* Light : Lights)
	{
		if (Name.Contains(Light))
		{
			return true;
		}
	}
	return false;
}

uint32 McHd::GetSeed(const FString& TextureId)
{
	return GetTypeHash(StripId(TextureId));
}

// ---- Maps ----

void McHd::NormalFromHeight(TConstArrayView<float> Height, int32 Width, int32 Height_, float Strength, TArray<FColor>& OutNormal)
{
	check(Height.Num() == Width * Height_);
	OutNormal.SetNumUninitialized(Width * Height_);
	auto At = [&](int32 X, int32 Y)
	{
		X = ((X % Width) + Width) % Width;
		Y = ((Y % Height_) + Height_) % Height_;
		return Height[Y * Width + X];
	};
	for (int32 Y = 0; Y < Height_; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			// Height rises to the right: the surface tilts left (x goes negative). Rising downwards the picture it
			// tilts up the picture; LabPBR's normal maps are DirectX (Y-: green low is up the picture), so y goes negative.
			const float Dx = (At(X + 1, Y) - At(X - 1, Y)) * 0.5f * Strength;
			const float Dy = (At(X, Y + 1) - At(X, Y - 1)) * 0.5f * Strength;
			FVector3f Normal(-Dx, -Dy, 1.0f);
			Normal.Normalize();
			OutNormal[Y * Width + X] = FColor(ToByte(Normal.X * 0.5f + 0.5f), ToByte(Normal.Y * 0.5f + 0.5f), 255, ToByte(Height[Y * Width + X]));
		}
	}
}

McHd::FMaps McHd::Generate(const FString& TextureId, const FPicture& Source, int32 Scale, EMaterialClass Class, bool bEmissive, bool bWrap)
{
	// Every texel stays a hard-edged square (it is Minecraft); the detail goes inside each one.
	const FPicture Big = ScaleNearest(Source, Source.Width * Scale, Source.Height * Scale);
	return MakeMaps(Big, Class, GetSeed(TextureId), bEmissive, bWrap, true, StripId(TextureId), Source.Width, Source.Height);
}

McHd::FMaps McHd::GenerateFromAlbedo(const FString& TextureId, const FPicture& Albedo, EMaterialClass Class, bool bEmissive, bool bWrap)
{
	return MakeMaps(Albedo, Class, GetSeed(TextureId), bEmissive, bWrap, false, StripId(TextureId), 16, 16);
}
