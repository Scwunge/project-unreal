#include "Misc/AutomationTest.h"
#include "Dom/JsonValue.h"
#include "McBundle.h"
#include "McContainer.h"

// The bundle's rules from minecraft.wiki "Bundle": weights by max stack size, one whole in all, newest first.
namespace
{
	// A stack with the max stack size the item data gives it.
	FMcItemStack Make(const TCHAR* Id, int32 Count, int32 MaxStack)
	{
		FMcItemStack Stack(FName(Id), Count);
		Stack.Set(TEXT("minecraft:max_stack_size"), MakeShared<FJsonValueNumber>(MaxStack));
		return Stack;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBundleTest, "Mc.Bundle.Rules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcBundleTest::RunTest(const FString&)
{
	FMcItemStack Bundle(TEXT("minecraft:bundle"), 1);
	TestTrue(TEXT("is a bundle"), McBundle::IsBundle(TEXT("minecraft:red_bundle")));
	TestFalse(TEXT("not a stick"), McBundle::IsBundle(TEXT("minecraft:stick")));
	TestEqual(TEXT("empty"), McBundle::GetFullness(Bundle), 0.0);

	// 32 sticks (1/64 each) and 8 ender pearls (1/16 each) make 1/2 + 1/2.
	FMcItemStack Sticks = Make(TEXT("minecraft:stick"), 32, 64);
	TestEqual(TEXT("32 sticks go in"), McBundle::Insert(Bundle, Sticks), 32);
	TestTrue(TEXT("none left"), Sticks.IsEmpty());
	FMcItemStack Pearls = Make(TEXT("minecraft:ender_pearl"), 8, 16);
	TestEqual(TEXT("8 pearls go in"), McBundle::Insert(Bundle, Pearls), 8);
	TestEqual(TEXT("full"), McBundle::GetFullness(Bundle), 1.0, 1.0e-9);
	FMcItemStack One = Make(TEXT("minecraft:stick"), 1, 64);
	TestEqual(TEXT("nothing more fits"), McBundle::Insert(Bundle, One), 0);
	TestEqual(TEXT("the pearls are the newest"), McBundle::GetContents(Bundle)[0].Item, FName(TEXT("minecraft:ender_pearl")));

	const FMcItemStack Out = McBundle::RemoveNewest(Bundle);
	TestEqual(TEXT("newest out"), Out.Item, FName(TEXT("minecraft:ender_pearl")));
	TestEqual(TEXT("half full"), McBundle::GetFullness(Bundle), 0.5, 1.0e-9);

	// A tool that does not stack fills the whole bundle; a shulker box cannot go in; a bundle takes 1/16 plus its contents.
	FMcItemStack Empty(TEXT("minecraft:bundle"), 1);
	FMcItemStack Sword = Make(TEXT("minecraft:diamond_sword"), 1, 1);
	TestEqual(TEXT("a sword fills it"), McBundle::Insert(Empty, Sword), 1);
	TestEqual(TEXT("full of one sword"), McBundle::GetFullness(Empty), 1.0, 1.0e-9);
	FMcItemStack Fresh(TEXT("minecraft:bundle"), 1);
	FMcItemStack Box(TEXT("minecraft:shulker_box"), 1);
	TestEqual(TEXT("no shulker box"), McBundle::Insert(Fresh, Box), 0);
	FMcItemStack Inner(TEXT("minecraft:bundle"), 1);
	FMcItemStack Dirt = Make(TEXT("minecraft:dirt"), 16, 64);
	McBundle::Insert(Inner, Dirt);
	TestEqual(TEXT("a bundle weighs 1/16 plus its dirt (16/64)"), McBundle::GetItemWeight(Inner), 1.0 / 16.0 + 0.25, 1.0e-9);
	return true;
}

// The clicks: left click with items on the cursor onto a bundle, right click on a bundle for the newest stack.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBundleClickTest, "Mc.Bundle.Clicks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcBundleClickTest::RunTest(const FString&)
{
	const TSharedPtr<FMcContainer> Box = MakeShared<FMcContainer>(9);
	Box->Set(0, FMcItemStack(TEXT("minecraft:bundle"), 1));
	Box->Set(1, Make(TEXT("minecraft:cobblestone"), 64, 64));
	FMcContainerMenu Menu(Box, MakeShared<FMcContainer>(41));
	TArray<FMcItemStack> Dropped;

	// Items on the cursor go into the bundle with a left click.
	Menu.Click(1, 0, EMcClickType::Pickup, Dropped);
	TestEqual(TEXT("cobble on the cursor"), Menu.GetCarried().Count, 64);
	Menu.Click(0, 0, EMcClickType::Pickup, Dropped);
	TestTrue(TEXT("all 64 went in (64 x 1/64 is a whole)"), Menu.GetCarried().IsEmpty());
	TestEqual(TEXT("bundle is full"), McBundle::GetFullness(Menu.GetStack(0)), 1.0, 1.0e-9);

	// A right click with an empty cursor takes the newest stack out.
	Menu.Click(0, 1, EMcClickType::Pickup, Dropped);
	TestEqual(TEXT("cobble back on the cursor"), Menu.GetCarried().Count, 64);
	TestEqual(TEXT("bundle empty again"), McBundle::GetFullness(Menu.GetStack(0)), 0.0, 1.0e-9);

	// A stack picked with the wheel is the one a right click takes (index 1 of the list, not the newest).
	FMcItemStack Three(TEXT("minecraft:bundle"), 1);
	FMcItemStack A = Make(TEXT("minecraft:stick"), 2, 64);
	FMcItemStack B = Make(TEXT("minecraft:dirt"), 3, 64);
	McBundle::Insert(Three, A);
	McBundle::Insert(Three, B);
	TestEqual(TEXT("dirt is the newest"), McBundle::GetContents(Three)[0].Item, FName(TEXT("minecraft:dirt")));
	const FMcItemStack Picked = McBundle::RemoveAt(Three, 1);
	TestEqual(TEXT("the picked stick came out"), Picked.Item, FName(TEXT("minecraft:stick")));
	TestEqual(TEXT("the dirt stayed"), McBundle::GetContents(Three)[0].Item, FName(TEXT("minecraft:dirt")));
	TestEqual(TEXT("an index out of range takes the newest"), McBundle::RemoveAt(Three, 7).Item, FName(TEXT("minecraft:dirt")));
	return true;
}
