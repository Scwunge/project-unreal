// Cross-checks the light tables in McCore, which come from the wiki, against the model data: a state the tables
// call opaque (dampening 15) should be a full cube with opaque textures, and the other way round.
#include "McBlockLight.h"
#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTextures.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLightOpacityTest, "McClient.McAssets.LightOpacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcLightOpacityTest::RunTest(const FString& Parameters)
{
	FMcBlockTextures& Textures = FMcBlockTextures::Get();
	FMcBlockLight& Light = FMcBlockLight::Get();
	if (!TestTrue(TEXT("textures load"), Textures.EnsureLoaded()) || !TestTrue(TEXT("light tables build"), Light.EnsureLoaded()))
	{
		return false;
	}
	const FMcBlockModels& Models = FMcBlockModels::Get();
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();

	// Blocks where the game's light behaviour differs from what the models suggest.
	static const TSet<FName> Exceptions = {
		TEXT("minecraft:tinted_glass"),       // a full cube with a translucent texture that still blocks all light
		TEXT("minecraft:chiseled_bookshelf"), // a full cube whose shelves are modelled as overlays
		TEXT("minecraft:powder_snow"),        // opaque-looking texture, but light passes
		TEXT("minecraft:piston"),             // retracted is a cube, extended is not: the shape tables cover it
		TEXT("minecraft:sticky_piston"),
	};

	TSet<FName> Mismatched;
	int32 Checked = 0;
	for (int32 Id = 0; Id < Registry.NumStates(); ++Id)
	{
		const FMcStateId State = static_cast<FMcStateId>(Id);
		const FMcBakedModel& Model = Models.GetModel(State);

		// Opaque: every one of the six sides is covered by a quad that fills the face and has no see-through pixels.
		uint32 Covered = 0;
		for (const FMcBakedQuad& Quad : Model.Quads)
		{
			if (Quad.CullFace == EMcDirection::None || Quad.bForceTranslucent)
			{
				continue;
			}
			FBox Box(ForceInit);
			for (const FVector3f& P : Quad.Positions)
			{
				Box += FVector(P);
			}
			const FVector Size = Box.GetSize();
			const bool bFills = (Size.X < 1e-4 || Size.X > 0.999) && (Size.Y < 1e-4 || Size.Y > 0.999) && (Size.Z < 1e-4 || Size.Z > 0.999)
				&& FMath::Abs(Size.X * Size.Y + Size.Y * Size.Z + Size.X * Size.Z - 1.0) < 1e-3;
			const int32 Layer = Textures.GetLayer(Quad.Texture);
			if (bFills && !Textures.HasCutoutPixels(Layer) && !Textures.HasTranslucentPixels(Layer))
			{
				Covered |= 1u << static_cast<int32>(Quad.CullFace);
			}
		}
		const bool bModelOpaque = Covered == 0x3F;
		const bool bTableOpaque = Light.GetDampening(State) == 15;
		++Checked;
		if (bModelOpaque != bTableOpaque)
		{
			const FName BlockName = Registry.GetBlockOf(State).Name;
			if (!Exceptions.Contains(BlockName) && !Mismatched.Contains(BlockName))
			{
				Mismatched.Add(BlockName);
				AddError(FString::Printf(TEXT("%s: the light table says %s, the models say %s"), *Registry.Describe(State),
					bTableOpaque ? TEXT("opaque") : TEXT("transparent"), bModelOpaque ? TEXT("opaque") : TEXT("transparent")));
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d states checked, %d blocks disagree"), Checked, Mismatched.Num()));
	return true;
}

#endif
