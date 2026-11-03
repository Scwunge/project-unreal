#include "McSoundRegistry.h"
#include "McResourcePacks.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

// Packs' sounds.json files, end to end through the pack stack: the game's events first, then each pack's over them, lowest
// pack first. An event the pack lists adds its sounds to the game's; "replace": true swaps them; a new event is added; and a
// sound file of the pack's wins over the game's for the same name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackSoundsTest, "McClient.McAudio.PackSounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackSoundsTest::RunTest(const FString& Parameters)
{
	McPacks::SetStack(MakeShared<FMcPackStack>());
	FMcSoundRegistry Plain;
	if (!Plain.EnsureLoaded())
	{
		AddInfo(TEXT("no sounds.json in the cache: skipped"));
		return true;
	}
	const TArray<FMcSoundVariant>* Vanilla = Plain.Find(FName("block.stone.break"));
	const TArray<FMcSoundVariant>* VanillaStep = Plain.Find(FName("block.stone.step"));
	if (!TestNotNull(TEXT("the game has block.stone.break"), Vanilla) || !TestNotNull(TEXT("and block.stone.step"), VanillaStep))
	{
		return false;
	}
	const int32 BreakCount = Vanilla->Num();
	const int32 StepCount = VanillaStep->Num();

	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackSoundsTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const auto MakePack = [&Root](const TCHAR* Name, const FString& SoundsJson)
	{
		const FString Dir = Root / Name;
		FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97,\"description\":\"sounds test\"}}"), *(Dir / TEXT("pack.mcmeta")));
		FFileHelper::SaveStringToFile(SoundsJson, *(Dir / TEXT("assets/minecraft/sounds.json")));
		return Dir;
	};
	// Low: extends block.stone.break, adds a new event. High: replaces block.stone.step, extends the new event again.
	const FString LowDir = MakePack(TEXT("low"),
		TEXT("{\"block.stone.break\":{\"sounds\":[\"zz_low_break\"]},\"zz.pack.event\":{\"sounds\":[\"zz_low_event\"]}}"));
	const FString HighDir = MakePack(TEXT("high"),
		TEXT("{\"block.stone.step\":{\"replace\":true,\"sounds\":[{\"name\":\"zz_high_step\",\"volume\":0.25}]},"
			"\"zz.pack.event\":{\"sounds\":[\"zz_high_event\"]}}"));
	FFileHelper::SaveStringToFile(TEXT("ogg"), *(HighDir / TEXT("assets/minecraft/sounds/zz_high_step.ogg")));

	FString Error;
	const TSharedPtr<FMcResourcePack> Low = FMcResourcePack::Open(LowDir, &Error);
	const TSharedPtr<FMcResourcePack> High = FMcResourcePack::Open(HighDir, &Error);
	if (!TestTrue(TEXT("the packs open: ") + Error, Low.IsValid() && High.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Low, High })));
	FMcSoundRegistry Registry;
	if (TestTrue(TEXT("loads with the packs"), Registry.EnsureLoaded()))
	{
		const TArray<FMcSoundVariant>* Break = Registry.Find(FName("block.stone.break"));
		if (TestNotNull(TEXT("break"), Break) && TestEqual(TEXT("a listed event gets the pack's sound added to the game's"), Break->Num(), BreakCount + 1))
		{
			TestEqual(TEXT("after the game's"), Break->Last().Name, FString(TEXT("zz_low_break")));
		}
		const TArray<FMcSoundVariant>* Step = Registry.Find(FName("block.stone.step"));
		if (TestNotNull(TEXT("step"), Step) && TestEqual(TEXT("replace swaps the game's list"), Step->Num(), 1))
		{
			TestEqual(TEXT("for the pack's sound"), (*Step)[0].Name, FString(TEXT("zz_high_step")));
			TestEqual(TEXT("with its volume"), (*Step)[0].Volume, 0.25f);
		}
		const TArray<FMcSoundVariant>* Event = Registry.Find(FName("zz.pack.event"));
		if (TestNotNull(TEXT("a new event is added"), Event) && TestEqual(TEXT("both packs' sounds"), Event->Num(), 2))
		{
			TestEqual(TEXT("the lower pack's first"), (*Event)[0].Name, FString(TEXT("zz_low_event")));
			TestEqual(TEXT("then the higher's"), (*Event)[1].Name, FString(TEXT("zz_high_event")));
		}
		FMcSoundVariant Picked;
		TestTrue(TEXT("a pack's event can be played"), Registry.Pick(FName("zz.pack.event"), 0.99f, Picked) && Picked.Name == TEXT("zz_high_event"));
	}
	TestEqual(TEXT("the pack's own sound file is used"), FMcSoundRegistry::ResolveSoundFile(TEXT("zz_high_step")),
		HighDir / TEXT("assets/minecraft/sounds/zz_high_step.ogg"));
	TestTrue(TEXT("a sound the packs lack is the game's"),
		FMcSoundRegistry::ResolveSoundFile(TEXT("dig/stone1")) == FPaths::Combine(FMcSoundRegistry::GetSoundsDir(), TEXT("dig/stone1.ogg")));

	// The packs go away: the game's sounds are back in a registry loaded afterwards.
	McPacks::SetStack(MakeShared<FMcPackStack>());
	FMcSoundRegistry After;
	if (TestTrue(TEXT("loads without the packs"), After.EnsureLoaded()))
	{
		TestNull(TEXT("the pack's event is gone"), After.Find(FName("zz.pack.event")));
		const TArray<FMcSoundVariant>* Step = After.Find(FName("block.stone.step"));
		TestTrue(TEXT("and the replaced event is the game's"), Step && Step->Num() == StepCount);
	}
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
