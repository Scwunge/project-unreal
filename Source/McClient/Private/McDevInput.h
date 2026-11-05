// Development: -McInput="hold:W:2.0;look:30,0;click:L:1.5;key:Q;wait:1;shot:after" plays a script of input as the local
// player, so a run can be driven and checked without hands. Steps are separated by ';', fields by ':':
//   hold:<keys>:<seconds>   keys held (several joined with '+') for that long; walking is hold:W:2
//   click:<L|R|M>:<seconds> the same, for a mouse button; a fourth field names a shot taken halfway through
//   key:<key>               a press and release (Q, E, One, F3, Escape, ...)
//   look:<yaw>,<pitch>      turn by that many degrees (positive pitch looks up)
//   wait:<seconds>
//   shot:<name>             save Saved/Shots/<name>.png
//   end                     quit
//   mark:<phase>            starts a phase in the -McPerf record
//   note:<text>             a line in the -McPerf record
//   chat:<line>             sends the line as the player's chat (a command with /); may hold ':'
//   press:<a,b_c>           queues menu buttons to press by name, '_' for a space
//   fly:<0|1>               stops or starts flying
//   pack:<a,b|off>          selects these resource packs and reloads, as the menu does
//   settle:<seconds>        waits until the renderer has nothing left to mesh, at most that long
// -McInputFile=<path> reads the steps from a file, one a line, with # for a comment; a script keeps going across a
// return to the title. A player who dies presses Respawn two seconds later.
// Key names are Unreal's (W, SpaceBar, LeftShift, One, F3, Escape), plus L, R, M for the mouse buttons and Shift,
// Ctrl, Space, Esc. The script starts -McInputDelay seconds (default 5) after the player is in the world.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class AMcPlayerController;

class FMcDevInput
{
public:
	// False when the command line has no -McInput.
	static bool Wanted();
	// The script of this run, read from the command line the first time.
	static FMcDevInput& Get();

	explicit FMcDevInput(const FString& Script);

	// Once a frame for the local controller that has its pawn.
	void Tick(AMcPlayerController& Controller, float DeltaSeconds);

	// A step as text and its pieces, for the tests.
	struct FStep
	{
		enum class EKind : uint8 { Hold, Look, Key, Wait, Shot, End, Mark, Note, Chat, MenuPress, Fly, Pack, Settle };
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
	float StartDelay = 5.0f;
	float DeadFor = 0.0f;
	bool bHadPawn = false;
	// A key pressed by "key:" is let go the next frame.
	TArray<FKey> ToRelease;
};
