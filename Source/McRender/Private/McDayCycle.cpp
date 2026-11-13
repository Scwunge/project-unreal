#include "McDayCycle.h"
#include "McCache.h"
#include "McRender.h"
#include "Dom/JsonObject.h"

namespace
{
	// "#rrggbb" or "#aarrggbb".
	bool ParseColour(const FString& Text, FVector4d& Out)
	{
		if (!Text.StartsWith(TEXT("#")) || (Text.Len() != 7 && Text.Len() != 9))
		{
			return false;
		}
		const uint32 Bits = FParse::HexNumber(*Text.Mid(1));
		const bool bAlpha = Text.Len() == 9;
		Out = FVector4d(
			((Bits >> 16) & 0xFF) / 255.0,
			((Bits >> 8) & 0xFF) / 255.0,
			(Bits & 0xFF) / 255.0,
			bAlpha ? ((Bits >> 24) & 0xFF) / 255.0 : 1.0);
		return true;
	}

	bool ParseValue(const TSharedPtr<FJsonValue>& Value, FVector4d& Out)
	{
		double Number = 0.0;
		FString Text;
		if (Value->TryGetNumber(Number))
		{
			Out = FVector4d(Number, 0.0, 0.0, 0.0);
			return true;
		}
		return Value->TryGetString(Text) && ParseColour(Text, Out);
	}

	FLinearColor ToColor(const FVector4d& Value)
	{
		return FLinearColor(static_cast<float>(Value.X), static_cast<float>(Value.Y), static_cast<float>(Value.Z), static_cast<float>(Value.W));
	}
}

FMcDayCycle& FMcDayCycle::Get()
{
	static FMcDayCycle Instance;
	return Instance;
}

double FMcDayCycle::CubicBezier(double X1, double Y1, double X2, double Y2, double U)
{
	if (U <= 0.0 || U >= 1.0)
	{
		return FMath::Clamp(U, 0.0, 1.0);
	}
	auto Axis = [](double P1, double P2, double T)
	{
		const double Inv = 1.0 - T;
		return 3.0 * Inv * Inv * T * P1 + 3.0 * Inv * T * T * P2 + T * T * T;
	};
	// Find the curve's parameter whose x is U, then read its y.
	double Low = 0.0, High = 1.0;
	for (int32 Step = 0; Step < 50; ++Step)
	{
		const double Mid = (Low + High) * 0.5;
		(Axis(X1, X2, Mid) < U ? Low : High) = Mid;
	}
	return Axis(Y1, Y2, (Low + High) * 0.5);
}

FVector4d FMcDayCycle::FTrack::Evaluate(double AtTicks) const
{
	const int32 Num = Ticks.Num();
	if (Num == 0)
	{
		return FVector4d(0.0);
	}
	if (Num == 1)
	{
		return Values[0];
	}

	// The track repeats every Period ticks starting from its first keyframe.
	double Local = AtTicks - Ticks[0];
	Local -= FMath::FloorToDouble(Local / Period) * Period;
	const double Time = Ticks[0] + Local;

	// The segment runs from the last keyframe at or before the time to the next, wrapping round to the first.
	// Keyframes sharing a tick (the sun's 360 to 0 jump) are ordered: the later one starts the segment.
	int32 From = 0;
	for (int32 Index = 0; Index < Num; ++Index)
	{
		if (Ticks[Index] <= Time)
		{
			From = Index;
		}
	}
	const int32 To = (From + 1) % Num;
	const double Start = Ticks[From];
	const double End = To > From ? Ticks[To] : Ticks[To] + Period;
	double Progress = End > Start ? (Time - Start) / (End - Start) : 0.0;

	switch (Ease)
	{
	case EEase::Constant:
		return Values[From];
	case EEase::Bezier:
		Progress = CubicBezier(Bezier[0], Bezier[1], Bezier[2], Bezier[3], Progress);
		break;
	default:
		break;
	}
	return FMath::Lerp(Values[From], Values[To], Progress);
}

bool FMcDayCycle::EnsureLoaded()
{
	if (IsLoaded())
	{
		return true;
	}

	const TSharedPtr<FJsonObject> Timeline = McCache::LoadJsonObject(McCache::Data(TEXT("timeline/day.json")));
	if (!Timeline.IsValid())
	{
		UE_LOG(LogMcRender, Error, TEXT("Can't read timeline/day.json: the sky and day cycle stay as they are. Run tools/fetch_cache.py."));
		return false;
	}
	double Period = TicksPerDay;
	Timeline->TryGetNumberField(TEXT("period_ticks"), Period);

	const TSharedPtr<FJsonObject>* TrackObjects = nullptr;
	if (Timeline->TryGetObjectField(TEXT("tracks"), TrackObjects))
	{
		for (const auto& Entry : (*TrackObjects)->Values)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Keyframes = nullptr;
			if (!Entry.Value->TryGetObject(Object) || !(*Object)->TryGetArrayField(TEXT("keyframes"), Keyframes))
			{
				continue;
			}

			FTrack Track;
			Track.Period = Period;
			bool bOk = true;
			for (const TSharedPtr<FJsonValue>& Keyframe : *Keyframes)
			{
				const TSharedPtr<FJsonObject>* Frame = nullptr;
				FVector4d Value;
				double Ticks = 0.0;
				if (!Keyframe->TryGetObject(Frame) || !(*Frame)->TryGetNumberField(TEXT("ticks"), Ticks) || !ParseValue((*Frame)->TryGetField(TEXT("value")), Value))
				{
					bOk = false;  // booleans and the like: the audio and gameplay tracks aren't ours
					break;
				}
				Track.Ticks.Add(Ticks);
				Track.Values.Add(Value);
			}
			if (!bOk || Track.Ticks.Num() == 0)
			{
				continue;
			}

			FString EaseName;
			const TSharedPtr<FJsonObject>* EaseObject = nullptr;
			if ((*Object)->TryGetStringField(TEXT("ease"), EaseName))
			{
				Track.Ease = EaseName == TEXT("constant") ? FTrack::EEase::Constant : FTrack::EEase::Linear;
			}
			else if ((*Object)->TryGetObjectField(TEXT("ease"), EaseObject))
			{
				const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
				if ((*EaseObject)->TryGetArrayField(TEXT("cubic_bezier"), Points) && Points->Num() == 4)
				{
					Track.Ease = FTrack::EEase::Bezier;
					for (int32 Index = 0; Index < 4; ++Index)
					{
						Track.Bezier[Index] = (*Points)[Index]->AsNumber();
					}
				}
			}
			Tracks.Add(FName(*Entry.Key), MoveTemp(Track));
		}
	}

	// The base colours the tracks multiply: the dimension's, which biomes can override.
	const TSharedPtr<FJsonObject> Dimension = McCache::LoadJsonObject(McCache::Data(TEXT("dimension_type/overworld.json")));
	const TSharedPtr<FJsonObject>* Attributes = nullptr;
	if (Dimension.IsValid() && Dimension->TryGetObjectField(TEXT("attributes"), Attributes))
	{
		FString Text;
		FVector4d Colour;
		if ((*Attributes)->TryGetStringField(TEXT("minecraft:visual/sky_color"), Text) && ParseColour(Text, Colour))
		{
			BaseSky = ToColor(Colour);
		}
		if ((*Attributes)->TryGetStringField(TEXT("minecraft:visual/fog_color"), Text) && ParseColour(Text, Colour))
		{
			BaseFog = ToColor(Colour);
		}
	}

	if (!FindTrack(TEXT("minecraft:visual/sun_angle")))
	{
		UE_LOG(LogMcRender, Error, TEXT("timeline/day.json has no sun angle track; the sun won't move"));
	}
	return true;
}

const FMcDayCycle::FTrack* FMcDayCycle::FindTrack(const TCHAR* Name) const
{
	return Tracks.Find(FName(Name));
}

double FMcDayCycle::Scalar(const TCHAR* Name, double Ticks, double Default) const
{
	const FTrack* Track = FindTrack(Name);
	return Track ? Track->Evaluate(Ticks).X : Default;
}

FLinearColor FMcDayCycle::Colour(const TCHAR* Name, double Ticks, const FLinearColor& Default) const
{
	const FTrack* Track = FindTrack(Name);
	return Track ? ToColor(Track->Evaluate(Ticks)) : Default;
}

float FMcDayCycle::GetSunAngle(double Ticks) const
{
	return static_cast<float>(FMath::Fmod(Scalar(TEXT("minecraft:visual/sun_angle"), Ticks, 0.0), 360.0));
}

float FMcDayCycle::GetMoonAngle(double Ticks) const
{
	return static_cast<float>(FMath::Fmod(Scalar(TEXT("minecraft:visual/moon_angle"), Ticks, 180.0), 360.0));
}

float FMcDayCycle::GetStarAngle(double Ticks) const
{
	return static_cast<float>(FMath::Fmod(Scalar(TEXT("minecraft:visual/star_angle"), Ticks, 0.0), 360.0));
}

FVector FMcDayCycle::AngleToDirection(float AngleDegrees)
{
	const double Radians = FMath::DegreesToRadians(static_cast<double>(AngleDegrees));
	return FVector(-FMath::Sin(Radians), FMath::Cos(Radians), 0.0);
}

float FMcDayCycle::GetSkyLightLevel(double Ticks) const
{
	return static_cast<float>(Scalar(TEXT("minecraft:gameplay/sky_light_level"), Ticks, 1.0));
}

int32 FMcDayCycle::GetSkyDarken(double Ticks) const
{
	return FMath::RoundToInt32(MaxSkyLight * (1.0f - GetSkyLightLevel(Ticks)));
}

float FMcDayCycle::GetSkyLightFactor(double Ticks) const
{
	return static_cast<float>(Scalar(TEXT("minecraft:visual/sky_light_factor"), Ticks, 1.0));
}

FLinearColor FMcDayCycle::GetSkyColor(double Ticks) const
{
	const FLinearColor Track = Colour(TEXT("minecraft:visual/sky_color"), Ticks, FLinearColor::White);
	return FLinearColor(BaseSky.R * Track.R, BaseSky.G * Track.G, BaseSky.B * Track.B, 1.0f);
}

FLinearColor FMcDayCycle::GetFogColor(double Ticks) const
{
	const FLinearColor Track = Colour(TEXT("minecraft:visual/fog_color"), Ticks, FLinearColor::White);
	return FLinearColor(BaseFog.R * Track.R, BaseFog.G * Track.G, BaseFog.B * Track.B, 1.0f);
}

FLinearColor FMcDayCycle::GetSunriseColor(double Ticks) const
{
	return Colour(TEXT("minecraft:visual/sunrise_sunset_color"), Ticks, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
}

float FMcDayCycle::GetStarBrightness(double Ticks) const
{
	return static_cast<float>(Scalar(TEXT("minecraft:visual/star_brightness"), Ticks, 0.0));
}

void FMcDayCycle::SetBaseColors(const FLinearColor& Sky, const FLinearColor& Fog)
{
	BaseSky = Sky;
	BaseFog = Fog;
}

int32 FMcDayCycle::GetMoonPhase(int64 Ticks)
{
	const int64 Day = FMath::FloorToInt64(static_cast<double>(Ticks) / TicksPerDay);
	return static_cast<int32>(((Day % MoonPhases) + MoonPhases) % MoonPhases);
}

FName FMcDayCycle::GetMoonPhaseName(int32 Phase)
{
	static const TCHAR* const Names[MoonPhases] = {
		TEXT("full_moon"), TEXT("waning_gibbous"), TEXT("third_quarter"), TEXT("waning_crescent"),
		TEXT("new_moon"), TEXT("waxing_crescent"), TEXT("first_quarter"), TEXT("waxing_gibbous"),
	};
	return FName(Names[((Phase % MoonPhases) + MoonPhases) % MoonPhases]);
}
