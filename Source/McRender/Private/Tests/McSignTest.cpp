#include "McBlockEntity.h"
#include "McBlockRegistry.h"
#include "McChunk.h"
#include "McSign.h"
#include "McSignComponent.h"
#include "McSignMenu.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSignTest, "McClient.McRender.Signs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSignTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()))
	{
		return false;
	}
	auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	using McSign::EKind;

	// ---- Kinds and which way they face ----
	TestTrue(TEXT("a standing sign"), McSign::GetKind(FName("minecraft:oak_sign")) == EKind::Standing);
	TestTrue(TEXT("a wall sign"), McSign::GetKind(FName("minecraft:spruce_wall_sign")) == EKind::Wall);
	TestTrue(TEXT("a hanging sign"), McSign::GetKind(FName("minecraft:cherry_hanging_sign")) == EKind::Hanging);
	TestTrue(TEXT("a wall hanging sign"), McSign::GetKind(FName("minecraft:bamboo_wall_hanging_sign")) == EKind::WallHanging);
	TestTrue(TEXT("stone is no sign"), McSign::GetKind(FName("minecraft:stone")) == EKind::None);
	TestEqual(TEXT("rotation 4 faces west"), McSign::GetYaw(State(TEXT("minecraft:oak_sign[rotation=4]"))), 90.0f);
	TestEqual(TEXT("a wall sign facing north"), McSign::GetYaw(State(TEXT("minecraft:oak_wall_sign[facing=north]"))), 180.0f);
	TestEqual(TEXT("a wall sign facing east"), McSign::GetYaw(State(TEXT("minecraft:oak_wall_sign[facing=east]"))), 270.0f);
	const FMcStateId South = State(TEXT("minecraft:oak_sign[rotation=0]"));
	TestTrue(TEXT("south of a sign facing south is its front"), McSign::IsFacingFront(South, FIntVector(10, 64, 10), FVector(10.5, 65.6, 14.0)));
	TestFalse(TEXT("north of it is its back"), McSign::IsFacingFront(South, FIntVector(10, 64, 10), FVector(10.5, 65.6, 7.0)));
	const FMcStateId WallNorth = State(TEXT("minecraft:oak_wall_sign[facing=north]"));
	TestTrue(TEXT("a wall sign facing north is read from the north"), McSign::IsFacingFront(WallNorth, FIntVector(0, 0, 0), FVector(0.5, 0.5, -3.0)));
	TestTrue(TEXT("its board is against the wall south of it"), McSign::Turn(McSign::GetBoardCentre(EKind::Wall), 180.0f, FIntVector(0, 0, 0)).Z > 0.9);

	// ---- Colours ----
	TestEqual(TEXT("light blue's text colour"), static_cast<int32>(McSign::GetTextColour(FName("light_blue"))), static_cast<int32>(0x9AC0CD));
	TestEqual(TEXT("an unknown dye is black"), static_cast<int32>(McSign::GetTextColour(FName("plaid"))), static_cast<int32>(0x000000));
	TestEqual(TEXT("a dye item's dye"), McSign::GetDyeOfItem(FName("minecraft:light_blue_dye")), FName("light_blue"));
	TestEqual(TEXT("an ink sac is no dye"), McSign::GetDyeOfItem(FName("minecraft:ink_sac")), FName());
	FMcSignSide White;
	White.Lines[0] = TEXT("Hi");
	White.Color = FName("white");
	TestEqual(TEXT("white text is drawn grey"), static_cast<int32>(McSign::GetDrawColour(White)), static_cast<int32>(0x666666));
	White.bGlowing = true;
	TestEqual(TEXT("glowing white is white"), static_cast<int32>(McSign::GetDrawColour(White)), static_cast<int32>(0xFFFFFF));
	TestEqual(TEXT("its outline is the grey"), static_cast<int32>(McSign::GetOutlineColour(White)), static_cast<int32>(0x666666));
	TestTrue(TEXT("the outline shows close up"), McSign::IsOutlineVisible(White, 10.0));
	TestFalse(TEXT("but not far off"), McSign::IsOutlineVisible(White, 20.0));
	FMcSignSide Black;
	Black.Lines[1] = TEXT("Hi");
	Black.bGlowing = true;
	TestEqual(TEXT("glowing black has a cream outline"), static_cast<int32>(McSign::GetOutlineColour(Black)), static_cast<int32>(0xF0EBCC));
	TestTrue(TEXT("seen from any distance"), McSign::IsOutlineVisible(Black, 100.0));

	// ---- The editor's lines ----
	const TArray<FString> Split = FMcSignMenu::Split(TEXT("one\ntw§o\n\u0001three"));
	TestEqual(TEXT("there are always four lines"), Split.Num(), 4);
	TestEqual(TEXT("the first"), Split[0], FString(TEXT("one")));
	TestEqual(TEXT("formatting codes are dropped"), Split[1], FString(TEXT("two")));
	TestEqual(TEXT("so are control characters"), Split[2], FString(TEXT("three")));
	TestEqual(TEXT("the last is empty"), Split[3], FString());
	TestEqual(TEXT("a fifth line is ignored"), FMcSignMenu::Split(TEXT("a\nb\nc\nd\ne"))[3], FString(TEXT("d")));
	TestEqual(TEXT("a line is cut at its length"), FMcSignMenu::Split(FString::ChrN(200, TEXT('i')))[0].Len(), FMcSignMenu::MaxLineLength);
	TestTrue(TEXT("joining and splitting gives the lines back"), FMcSignMenu::Split(FMcSignMenu::Join(Split)) == Split);

	// ---- Saving: the text, dye, glow and wax come back, and the old kinds' bytes are as they were ----
	FMcChunk Chunk(FIntPoint(0, 0), -64, 24);
	Chunk.SetBlock(3, 70, 4, South);
	FMcBlockEntity Sign = FMcBlockEntity::MakeSign();
	Sign.SignSides[0].Lines[0] = TEXT("Hello");
	Sign.SignSides[0].Lines[3] = TEXT("world");
	Sign.SignSides[0].Color = FName("light_blue");
	Sign.SignSides[1].Lines[1] = TEXT("A B");
	Sign.SignSides[1].bGlowing = true;
	Sign.bWaxed = true;
	Chunk.SetBlockEntity(3, 70, 4, Sign);
	FMcChunk Plain(FIntPoint(0, 0), -64, 24);
	const TArray<uint8> NoSigns = Plain.EncodeBlockEntities();
	const TArray<uint8> Bytes = Chunk.EncodeBlockEntities();
	FMcChunk Loaded(FIntPoint(0, 0), -64, 24);
	Loaded.SetBlock(3, 70, 4, South);
	TestTrue(TEXT("a sign's bytes decode"), Loaded.DecodeBlockEntities(Bytes));
	const FMcBlockEntity* Back = Loaded.FindBlockEntity(3, 70, 4);
	TestTrue(TEXT("the sign comes back as it was"), Back && *Back == Sign);
	TestEqual(TEXT("saving again gives the same bytes"), Loaded.EncodeBlockEntities(), Bytes);
	TestEqual(TEXT("a chunk without signs saves as before"), NoSigns.Num(), 4);
	FMcChunk Cut(FIntPoint(0, 0), -64, 24);
	TestFalse(TEXT("a sign cut short is refused"), Cut.DecodeBlockEntities(TConstArrayView<uint8>(Bytes).Left(Bytes.Num() - 2)));

	// ---- The text on the board ----
	UMcSignComponent::FTextMesh Mesh;
	UMcSignComponent::BuildText(Sign, South, Mesh);
	// "Hello" and "world" on the front, "A B" (the space draws nothing) on the back, four corners a glyph.
	TestEqual(TEXT("the front's ten glyphs"), Mesh.Plain.Positions.Num(), 10 * 4);
	TestEqual(TEXT("the back's two glowing ones"), Mesh.Glowing.Positions.Num(), 2 * 4);
	TestEqual(TEXT("black glowing text is outlined eight ways"), Mesh.BlackOutline.Positions.Num(), 2 * 4 * 8);
	TestEqual(TEXT("and has no near outline"), Mesh.NearOutline.Positions.Num(), 0);
	TestEqual(TEXT("two triangles a glyph"), Mesh.Plain.Triangles.Num(), 10 * 6);
	const double Front = McCoords::BlockToUnreal(FVector(0.0, 0.0, McSign::GetBoardCentre(EKind::Standing).Z + McSign::GetHalfThickness(EKind::Standing))).Y;
	bool bInFront = true, bBehind = true;
	double MinX = TNumericLimits<double>::Max(), MaxX = TNumericLimits<double>::Lowest();
	for (int32 Index = 0; Index < 5 * 4; ++Index)
	{
		const FVector& Corner = Mesh.Plain.Positions[Index];
		bInFront &= Corner.Y > Front;
		MinX = FMath::Min(MinX, Corner.X);
		MaxX = FMath::Max(MaxX, Corner.X);
	}
	for (const FVector& Corner : Mesh.Glowing.Positions)
	{
		bBehind &= Corner.Y < McCoords::BlockToUnreal(FVector(0.0, 0.0, 0.5 - McSign::GetHalfThickness(EKind::Standing))).Y;
	}
	TestTrue(TEXT("the front's text is in front of the board"), bInFront);
	TestTrue(TEXT("the back's is behind it"), bBehind);
	TestTrue(TEXT("the first line is about the middle"), FMath::Abs((MinX + MaxX) / 2.0 - McCoords::UnitsPerBlock / 2.0) < McCoords::UnitsPerBlock * 0.05);
	const FVector Normal = Mesh.Plain.Normals.Num() > 0 ? Mesh.Plain.Normals[0] : FVector::ZeroVector;
	TestTrue(TEXT("the front faces south (Unreal +Y)"), Normal.Equals(FVector(0, 1, 0), 1e-6));

	// Turned half round, the front's text is on the north side.
	UMcSignComponent::FTextMesh Turned;
	UMcSignComponent::BuildText(Sign, State(TEXT("minecraft:oak_sign[rotation=8]")), Turned);
	bool bNorth = Turned.Plain.Positions.Num() > 0;
	for (const FVector& Corner : Turned.Plain.Positions)
	{
		bNorth &= Corner.Y < McCoords::UnitsPerBlock / 2.0;
	}
	TestTrue(TEXT("turned round, the front's text is to the north"), bNorth);

	// Glowing white text close up gets the near outline; a hanging sign's text is the hanging size.
	FMcBlockEntity Hanging = FMcBlockEntity::MakeSign();
	Hanging.SignSides[0] = White;
	UMcSignComponent::FTextMesh HangingMesh;
	UMcSignComponent::BuildText(Hanging, State(TEXT("minecraft:oak_hanging_sign[rotation=0,attached=false]")), HangingMesh);
	TestEqual(TEXT("white glowing text's outline is the near one"), HangingMesh.NearOutline.Positions.Num(), 2 * 4 * 8);
	TestEqual(TEXT("and none of it is black's"), HangingMesh.BlackOutline.Positions.Num(), 0);
	UMcSignComponent::FTextMesh Empty;
	UMcSignComponent::BuildText(FMcBlockEntity::MakeSign(), South, Empty);
	TestEqual(TEXT("an empty sign draws nothing"), Empty.Plain.Positions.Num() + Empty.Glowing.Positions.Num(), 0);
	UMcSignComponent::BuildText(Sign, State(TEXT("minecraft:stone")), Empty);
	TestEqual(TEXT("nor does one on a block that is no sign"), Empty.Plain.Positions.Num(), 0);
	return true;
}

#endif
