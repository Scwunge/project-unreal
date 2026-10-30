// Development: -McInput="hold:W:2.0;look:30,0;click:L:1.5;key:Q;wait:1;shot:after" plays a script of input as the local
// player, so a run can be driven and checked without hands. Steps are separated by ';', fields by ':':
//   hold:<keys>:<seconds>   keys held (several joined with '+') for that long; walking is hold:W:2
//   click:<L|R|M>:<seconds> the same, for a mouse button; a fourth field names a shot taken halfway through
//   key:<key>               a press and release (Q, E, One, F3, Escape, ...)
//   look:<yaw>,<pitch>      turn by that many degrees (positive pitch looks up)
//   wait:<seconds>
//   shot:<name>             save Saved/Shots/<name>.png
//   end                     quit
// Key names are Unreal's (W, SpaceBar, LeftShift, One, F3, Escape), plus L, R, M for the mouse buttons and Shift,
// Ctrl, Space, Esc. The script starts -McInputDelay seconds (default 5) after the player is in the world.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class AMcPlayerController;

#if !UE_BUILD_SHIPPING

class FMcDevInput
{
public:
	// False when the command line has no -McInput.
	static bool Wanted();

	explicit FMcDevInput(const FString& Script);

	// Once a frame for the local controller that has its pawn.
	void Tick(AMcPlayerController& Controller, float DeltaSeconds);

	// A step as text and its pieces, for the tests.
	struct FStep
	{
		enum class EKind : uint8 { Hold, Look, Key, Wait, Shot, End };
		EKind Kind = EKind::Wait;
		TArray<FKey> Keys;
		float Seconds = 0.0f;
		float A = 0.0f;
		float B = 0.0f;
		FString Name;
	};
	static bool ParseStep(const FString& Text, FStep& Out);
	static FKey ParseKey(const FString& Name);
	const TArray<FStep>& GetSteps() const { return Steps; }

private:
	void Press(AMcPlayerController& Controller, const FKey& Key, bool bDown);
	static void TakeShot(const FString& Name);

	TArray<FStep> Steps;
	int32 Next = 0;
	bool bRunning = false;
	bool bHoldShot = false;
	FStep Current;
	float Left = 0.0f;
	float Waited = 0.0f;
	// A key pressed by "key:" is let go the next frame.
	TArray<FKey> ToRelease;
};

#endif
