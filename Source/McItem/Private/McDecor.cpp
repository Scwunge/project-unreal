#include "McDecor.h"

namespace
{
	const TCHAR* const SlotKeys[] = { TEXT("head"), TEXT("chest"), TEXT("legs"), TEXT("feet"), TEXT("main"), TEXT("off") };
	const TCHAR* const PartKeys[] = { TEXT("p_head"), TEXT("p_body"), TEXT("p_larm"), TEXT("p_rarm"), TEXT("p_lleg"), TEXT("p_rleg") };
	const TCHAR* const KindNames[] = { TEXT("frame"), TEXT("glowframe"), TEXT("painting"), TEXT("stand") };

	FString FormatFloat(float Value)
	{
		// Whole degrees stay short; the line has to fit in a name.
		return FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value), 0.001f) ? FString::FromInt(FMath::RoundToInt(Value)) : FString::SanitizeFloat(Value);
	}
}

McDecor::FDecor::FDecor()
{
	for (int32 Part = 0; Part < static_cast<int32>(EPart::Count); ++Part)
	{
		Pose[Part] = GetDefaultPose(static_cast<EPart>(Part));
	}
	for (int32& Colour : Dye)
	{
		Colour = -1;
	}
}

FVector3f McDecor::GetDefaultPose(EPart Part)
{
	// The wiki gives the arms' defaults ("Armor Stand": left [350, 0, 350], right [345, 0, 10]); the other parts stand straight.
	switch (Part)
	{
	case EPart::LeftArm: return FVector3f(350.0f, 0.0f, 350.0f);
	case EPart::RightArm: return FVector3f(345.0f, 0.0f, 10.0f);
	default: return FVector3f::ZeroVector;
	}
}

FString McDecor::Encode(const FDecor& Decor)
{
	FString Line = KindNames[static_cast<int32>(Decor.Kind)];
	if (Decor.Kind == EKind::Painting)
	{
		Line += FString::Printf(TEXT(";f=%d;v=%s"), Decor.Facing, *Decor.Variant);
	}
	else if (Decor.Kind == EKind::ArmorStand)
	{
		Line += FString::Printf(TEXT(";y=%s;small=%d;arms=%d;base=%d;invis=%d"), *FormatFloat(Decor.Yaw), Decor.bSmall, Decor.bArms, Decor.bBasePlate, Decor.bInvisible);
		for (int32 Part = 0; Part < static_cast<int32>(EPart::Count); ++Part)
		{
			if (Decor.Pose[Part] != GetDefaultPose(static_cast<EPart>(Part)))
			{
				Line += FString::Printf(TEXT(";%s=%s,%s,%s"), PartKeys[Part], *FormatFloat(Decor.Pose[Part].X), *FormatFloat(Decor.Pose[Part].Y), *FormatFloat(Decor.Pose[Part].Z));
			}
		}
		for (int32 Slot = 0; Slot < static_cast<int32>(ESlot::Count); ++Slot)
		{
			if (!Decor.Equipment[Slot].IsEmpty())
			{
				Line += FString::Printf(TEXT(";%s=%s"), SlotKeys[Slot], *Decor.Equipment[Slot]);
				if (Decor.Dye[Slot] >= 0)
				{
					Line += FString::Printf(TEXT(";dye%s=%d"), SlotKeys[Slot], Decor.Dye[Slot]);
				}
			}
		}
	}
	else
	{
		Line += FString::Printf(TEXT(";f=%d;rot=%d;invis=%d"), Decor.Facing, Decor.ItemRotation, Decor.bInvisible);
		if (!Decor.Item.IsEmpty())
		{
			Line += TEXT(";item=") + Decor.Item;
		}
	}
	return Line.ToLower();
}

bool McDecor::Decode(const FString& Line, FDecor& Out)
{
	TArray<FString> Fields;
	Line.ParseIntoArray(Fields, TEXT(";"), true);
	if (Fields.IsEmpty())
	{
		return false;
	}
	Out = FDecor();
	bool bKnown = false;
	for (int32 Kind = 0; Kind < UE_ARRAY_COUNT(KindNames); ++Kind)
	{
		if (Fields[0] == KindNames[Kind])
		{
			Out.Kind = static_cast<EKind>(Kind);
			bKnown = true;
		}
	}
	if (!bKnown)
	{
		return false;
	}
	for (int32 Index = 1; Index < Fields.Num(); ++Index)
	{
		FString Key, Value;
		if (!Fields[Index].Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		if (Key == TEXT("f")) { Out.Facing = FCString::Atoi(*Value); }
		else if (Key == TEXT("v")) { Out.Variant = Value; }
		else if (Key == TEXT("item")) { Out.Item = Value; }
		else if (Key == TEXT("rot")) { Out.ItemRotation = FCString::Atoi(*Value); }
		else if (Key == TEXT("invis")) { Out.bInvisible = Value == TEXT("1"); }
		else if (Key == TEXT("y")) { Out.Yaw = FCString::Atof(*Value); }
		else if (Key == TEXT("small")) { Out.bSmall = Value == TEXT("1"); }
		else if (Key == TEXT("arms")) { Out.bArms = Value == TEXT("1"); }
		else if (Key == TEXT("base")) { Out.bBasePlate = Value == TEXT("1"); }
		else
		{
			for (int32 Part = 0; Part < static_cast<int32>(EPart::Count); ++Part)
			{
				TArray<FString> Angles;
				if (Key == PartKeys[Part] && Value.ParseIntoArray(Angles, TEXT(","), true) == 3)
				{
					Out.Pose[Part] = FVector3f(FCString::Atof(*Angles[0]), FCString::Atof(*Angles[1]), FCString::Atof(*Angles[2]));
				}
			}
			for (int32 Slot = 0; Slot < static_cast<int32>(ESlot::Count); ++Slot)
			{
				if (Key == SlotKeys[Slot])
				{
					Out.Equipment[Slot] = Value;
				}
				else if (Key == FString(TEXT("dye")) + SlotKeys[Slot])
				{
					Out.Dye[Slot] = FCString::Atoi(*Value);
				}
			}
		}
	}
	return true;
}
