#include "McDevInput.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDevInputScriptTest, "McClient.McClient.DevInputScript",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcDevInputScriptTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("cannot read the step"), EAutomationExpectedErrorFlags::Contains, 1);
	FMcDevInput Script(TEXT("hold:W+Shift:2.5;look:30,-10;click:L:1.5;key:Q;key:F3;wait:1;shot:after;end;bogus:1"));
	const TArray<FMcDevInput::FStep>& Steps = Script.GetSteps();
	// The unreadable last step is dropped, the rest kept in order.
	if (!TestEqual(TEXT("eight steps"), Steps.Num(), 8))
	{
		return false;
	}
	using EKind = FMcDevInput::FStep::EKind;
	TestTrue(TEXT("a walk"), Steps[0].Kind == EKind::Hold && Steps[0].Keys.Num() == 2 && Steps[0].Keys[0] == EKeys::W && Steps[0].Keys[1] == EKeys::LeftShift);
	TestEqual(TEXT("for 2.5 seconds"), Steps[0].Seconds, 2.5f);
	TestTrue(TEXT("a turn"), Steps[1].Kind == EKind::Look && Steps[1].A == 30.0f && Steps[1].B == -10.0f);
	TestTrue(TEXT("a click is a held mouse button"), Steps[2].Kind == EKind::Hold && Steps[2].Keys[0] == EKeys::LeftMouseButton);
	TestTrue(TEXT("a key"), Steps[3].Kind == EKind::Key && Steps[3].Keys[0] == EKeys::Q);
	TestTrue(TEXT("function keys"), Steps[4].Keys[0] == EKeys::F3);
	TestTrue(TEXT("a wait"), Steps[5].Kind == EKind::Wait && Steps[5].Seconds == 1.0f);
	TestTrue(TEXT("a shot"), Steps[6].Kind == EKind::Shot && Steps[6].Name == TEXT("after"));
	TestTrue(TEXT("the end"), Steps[7].Kind == EKind::End);
	FMcDevInput::FStep Shooting;
	TestTrue(TEXT("a hold with a shot"), FMcDevInput::ParseStep(TEXT("click:L:1:mid"), Shooting) && Shooting.Name == TEXT("mid") && Shooting.Seconds == 1.0f);
	TestTrue(TEXT("aliases"), FMcDevInput::ParseKey(TEXT("Esc")) == EKeys::Escape && FMcDevInput::ParseKey(TEXT("Space")) == EKeys::SpaceBar && FMcDevInput::ParseKey(TEXT("R")) == EKeys::RightMouseButton);
	return true;
}

#endif
