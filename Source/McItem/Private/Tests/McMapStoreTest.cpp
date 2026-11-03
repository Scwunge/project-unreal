#include "Misc/AutomationTest.h"
#include "McMapStore.h"
#include "McRecipe.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcMapStoreTest, "Mc.Map.Store", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcMapStoreTest::RunTest(const FString&)
{
	McMapStore::Reset();
	const int32 A = McMapStore::Create(0, 3040, 0, TEXT("minecraft:overworld"));
	FMcMapData* Map = McMapStore::Find(A);
	if (!TestNotNull(TEXT("made"), Map))
	{
		return false;
	}
	TestEqual(TEXT("centre x"), Map->CenterX, 3072);
	TestEqual(TEXT("centre z"), Map->CenterZ, 0);
	FMcItemStack Item(TEXT("minecraft:filled_map"), 1);
	McMapStore::SetId(Item, A);
	TestEqual(TEXT("id round trip"), McMapStore::GetId(Item), A);
	TestTrue(TEXT("can zoom"), McMapStore::CanZoom(Item));

	const int32 B = McMapStore::Zoom(A);
	TestNotEqual(TEXT("zoom makes a new map"), B, A);
	TestEqual(TEXT("zoom twice is one map"), McMapStore::Zoom(A), B);
	TestEqual(TEXT("zoom scale"), McMapStore::Find(B)->Scale, 1);

	Map->Colors[5] = 45;
	McMapStore::Lock(B);
	const FString Json = McMapStore::ToJson();
	McMapStore::Reset();
	TestEqual(TEXT("reset"), McMapStore::Num(), 0);
	TestTrue(TEXT("load"), McMapStore::FromJson(Json));
	TestEqual(TEXT("two maps back"), McMapStore::Num(), 2);
	TestEqual(TEXT("colours kept"), (int32)McMapStore::Find(A)->Colors[5], 45);
	TestTrue(TEXT("lock kept"), McMapStore::Find(B)->bLocked);
	TestEqual(TEXT("zoom cache kept"), McMapStore::Zoom(A), B);
	McMapStore::Reset();
	return true;
}
