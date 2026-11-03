#include "McSign.h"
#include "McBlockEntity.h"
#include "McBlockRegistry.h"

namespace
{
	struct FDye
	{
		const TCHAR* Name;
		uint32 Text;
	};
	// minecraft.wiki, "Dye": the colour each dye gives a sign's text.
	constexpr FDye Dyes[] = {
		{ TEXT("white"), 0xFFFFFF }, { TEXT("orange"), 0xFF681F }, { TEXT("magenta"), 0xFF00FF }, { TEXT("light_blue"), 0x9AC0CD },
		{ TEXT("yellow"), 0xFFFF00 }, { TEXT("lime"), 0xBFFF00 }, { TEXT("pink"), 0xFF69B4 }, { TEXT("gray"), 0x808080 },
		{ TEXT("light_gray"), 0xD3D3D3 }, { TEXT("cyan"), 0x00FFFF }, { TEXT("purple"), 0xA020F0 }, { TEXT("blue"), 0x0000FF },
		{ TEXT("brown"), 0x8B4513 }, { TEXT("green"), 0x00FF00 }, { TEXT("red"), 0xFF0000 }, { TEXT("black"), 0x000000 },
	};

	const FDye* FindDye(FName Dye)
	{
		for (const FDye& Each : Dyes)
		{
			if (Dye == Each.Name)
			{
				return &Each;
			}
		}
		return nullptr;
	}

	uint32 Darken(uint32 Rgb)
	{
		const auto Channel = [Rgb](int32 Shift) { return static_cast<uint32>(((Rgb >> Shift) & 0xFF) * 0.4) << Shift; };
		return Channel(16) | Channel(8) | Channel(0);
	}
}

McSign::EKind McSign::GetKind(FName Block)
{
	const FString Name = Block.ToString();
	if (!Name.EndsWith(TEXT("_sign")))
	{
		return EKind::None;
	}
	if (Name.EndsWith(TEXT("_wall_hanging_sign")))
	{
		return EKind::WallHanging;
	}
	if (Name.EndsWith(TEXT("_hanging_sign")))
	{
		return EKind::Hanging;
	}
	return Name.EndsWith(TEXT("_wall_sign")) ? EKind::Wall : EKind::Standing;
}

McSign::EKind McSign::GetKind(FMcStateId State)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	return Registry.IsValidState(State) ? GetKind(Registry.GetBlockOf(State).Name) : EKind::None;
}

float McSign::GetYaw(FMcStateId State)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const EKind Kind = GetKind(State);
	if (Kind == EKind::Standing || Kind == EKind::Hanging)
	{
		const FName Rotation = Registry.GetPropertyValue(State, TEXT("rotation"));
		return Rotation.IsNone() ? 0.0f : FCString::Atoi(*Rotation.ToString()) * 22.5f;
	}
	const FName Facing = Registry.GetPropertyValue(State, TEXT("facing"));
	return Facing == TEXT("west") ? 90.0f : Facing == TEXT("north") ? 180.0f : Facing == TEXT("east") ? 270.0f : 0.0f;
}

FVector McSign::GetBoardCentre(EKind Kind)
{
	switch (Kind)
	{
	case EKind::Wall:
		return FVector(0.5, 8.33333 / 16.0, 1.0 / 16.0);
	case EKind::Hanging:
	case EKind::WallHanging:
		return FVector(0.5, 5.0 / 16.0, 0.5);
	default:
		return FVector(0.5, 13.33333 / 16.0, 0.5);
	}
}

double McSign::GetHalfThickness(EKind Kind)
{
	return IsHanging(Kind) ? 1.0 / 16.0 : 0.66667 / 16.0;
}

FVector McSign::Turn(const FVector& Local, float Yaw, const FIntVector& Pos)
{
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Yaw));
	const double Cos = FMath::Cos(Radians);
	const double Sin = FMath::Sin(Radians);
	// South (0, 0, 1) turns to (-sin, 0, cos): at 90 degrees the front faces west.
	const double X = Local.X - 0.5;
	const double Z = Local.Z - 0.5;
	return FVector(Pos) + FVector(0.5 + X * Cos - Z * Sin, Local.Y, 0.5 + X * Sin + Z * Cos);
}

bool McSign::IsFacingFront(FMcStateId State, const FIntVector& Pos, const FVector& Where)
{
	const EKind Kind = GetKind(State);
	const float Yaw = GetYaw(State);
	const FVector Centre = Turn(GetBoardCentre(Kind), Yaw, Pos);
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Yaw));
	const FVector2D Front(-FMath::Sin(Radians), FMath::Cos(Radians));
	return FVector2D::DotProduct(FVector2D(Where.X - Centre.X, Where.Z - Centre.Z), Front) >= 0.0;
}

int32 McSign::GetMaxLineWidth(EKind Kind)
{
	return IsHanging(Kind) ? 60 : 90;
}

int32 McSign::GetLineHeight(EKind Kind)
{
	return IsHanging(Kind) ? 9 : 10;
}

double McSign::GetTextScale(EKind Kind)
{
	// A font pixel is a 64th of a block, drawn at the sign's scale: two thirds on a sign, nine tenths on a hanging one.
	return (IsHanging(Kind) ? 0.9 : 2.0 / 3.0) / 64.0;
}

uint32 McSign::GetTextColour(FName Dye)
{
	const FDye* Found = FindDye(Dye);
	return Found ? Found->Text : 0x000000;
}

bool McSign::IsDye(FName Dye)
{
	return FindDye(Dye) != nullptr;
}

FName McSign::GetDyeOfItem(FName Item)
{
	FString Name = Item.ToString();
	if (!Name.RemoveFromStart(TEXT("minecraft:")) || !Name.RemoveFromEnd(TEXT("_dye")))
	{
		return NAME_None;
	}
	const FName Dye(*Name);
	return IsDye(Dye) ? Dye : NAME_None;
}

uint32 McSign::GetDrawColour(const FMcSignSide& Side)
{
	const uint32 Text = GetTextColour(Side.Color);
	return Side.bGlowing ? Text : Darken(Text);
}

uint32 McSign::GetOutlineColour(const FMcSignSide& Side)
{
	const uint32 Text = GetTextColour(Side.Color);
	return Text == 0x000000 && Side.bGlowing ? 0xF0EBCC : Darken(Text);
}

bool McSign::IsOutlineVisible(const FMcSignSide& Side, double Distance)
{
	return Side.bGlowing && (GetTextColour(Side.Color) == 0x000000 || Distance <= 16.0);
}
