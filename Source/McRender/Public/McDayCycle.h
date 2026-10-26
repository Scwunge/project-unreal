// The day and night cycle's numbers: where the sun, moon and stars are, how dark the sky light is, and the sky, fog and
// sunrise colours. 26.x keeps them as keyframed tracks in the jar's data/minecraft/timeline/day.json (and the biome and
// dimension JSON for the base colours), so this reads those and evaluates them; there are no numbers of ours in here.
//
// Time is in game ticks (20 a second, 24000 a day); noon is 6000, sunset 12000, midnight 18000. Colours are display
// (sRGB) values from 0 to 1.

#pragma once

#include "CoreMinimal.h"

class MCRENDER_API FMcDayCycle
{
public:
	static constexpr int32 TicksPerDay = 24000;
	static constexpr int32 MoonPhases = 8;
	// Sky light goes from 15 down by this many levels on the darkest night.
	static constexpr int32 MaxSkyLight = 15;

	static FMcDayCycle& Get();

	// Reads timeline/day.json and the overworld's base colours from cache/. Does nothing if already loaded.
	bool EnsureLoaded();
	bool IsLoaded() const { return Tracks.Num() > 0; }

	// Angles in degrees around the east-west axis, 0 at the zenith, 90 on the western horizon (setting), 180 at
	// the nadir, 270 on the eastern horizon (rising). They move slowly near noon and midnight and fast at the
	// horizon, as the game's celestial angle does.
	float GetSunAngle(double Ticks) const;
	float GetMoonAngle(double Ticks) const;
	float GetStarAngle(double Ticks) const;

	// Unit vectors towards the sun and the moon, in Minecraft axes (x east, y up, z south).
	static FVector AngleToDirection(float AngleDegrees);

	// How much of the sky's light (the 0-15 stored in the chunks) gets through: 1 by day, about 0.27 at night. The
	// renderer multiplies it in; the stored light never changes.
	float GetSkyLightLevel(double Ticks) const;
	// The same as how many of the 15 sky light levels the dark takes away: 0 by day, 11 at midnight.
	int32 GetSkyDarken(double Ticks) const;
	// The brightness curve's sky factor: 1 by day, 0.24 at night.
	float GetSkyLightFactor(double Ticks) const;

	// The base sky and fog colours times the day's track: the plains' blue and pale blue by day, black and near
	// black by night.
	FLinearColor GetSkyColor(double Ticks) const;
	FLinearColor GetFogColor(double Ticks) const;
	// The glow at the horizon around dawn and dusk, with its strength in A (0 by day and night).
	FLinearColor GetSunriseColor(double Ticks) const;
	// 0 by day, up to 0.5 at night.
	float GetStarBrightness(double Ticks) const;

	// The base colours the tracks are multiplied with; the overworld's, until a biome says otherwise.
	void SetBaseColors(const FLinearColor& Sky, const FLinearColor& Fog);
	const FLinearColor& GetBaseSky() const { return BaseSky; }
	const FLinearColor& GetBaseFog() const { return BaseFog; }

	// The moon's phase for the day the time falls in: 0 full, 1 waning gibbous, 2 third quarter, 3 waning crescent, 4
	// new, 5 waxing crescent, 6 first quarter, 7 waxing gibbous, repeating every eight days.
	static int32 GetMoonPhase(int64 Ticks);
	// The jar's name for a phase, also its texture's name under textures/environment/celestial/moon/.
	static FName GetMoonPhaseName(int32 Phase);

	// A CSS cubic-bezier easing: the progress through a segment for the progress in time. Exposed for tests.
	static double CubicBezier(double X1, double Y1, double X2, double Y2, double U);

private:
	struct FTrack
	{
		enum class EEase : uint8 { Linear, Constant, Bezier };

		TArray<double> Ticks;
		TArray<FVector4d> Values;  // colours as rgba, numbers in X
		EEase Ease = EEase::Linear;
		double Bezier[4] = { 0.0, 0.0, 1.0, 1.0 };
		double Period = TicksPerDay;

		FVector4d Evaluate(double AtTicks) const;
	};

	const FTrack* FindTrack(const TCHAR* Name) const;
	double Scalar(const TCHAR* Name, double Ticks, double Default) const;
	FLinearColor Colour(const TCHAR* Name, double Ticks, const FLinearColor& Default) const;

	TMap<FName, FTrack> Tracks;
	FLinearColor BaseSky = FLinearColor(0.47f, 0.655f, 1.0f, 1.0f);
	FLinearColor BaseFog = FLinearColor(0.753f, 0.847f, 1.0f, 1.0f);
};
