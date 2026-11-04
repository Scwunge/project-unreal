// Llamas against vehicle_log28.txt: followers trail the llama on the lead by about 3 blocks, one behind the other; a hurt llama spits at the
// player every 40 ticks (speed 1.5, drag 0.99, gravity 0.06, 1 damage).

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McLlama.h"
#include "McMob.h"
#include "McMount.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLlamaTest, "McClient.McEntity.Llama",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcLlamaTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const FMcMobType* LlamaType = FMcMobType::Find(FName(TEXT("minecraft:llama")));
	if (!TestNotNull(TEXT("llama type"), LlamaType))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -2; Z <= 12; ++Z) { for (int32 X = -2; X <= 2; ++X) { World.AddChunk(World.MakeChunk(FIntPoint(X, Z))); } }
	for (int32 X = -30; X < 30; ++X) { for (int32 Z = -30; Z < 200; ++Z) { World.SetBlock(FIntVector(X, 0, Z), Registry.ParseState(TEXT("minecraft:stone")).Get(0)); } }
	FMcEntityManager Manager(World);
	const auto Make = [&](double X, double Z)
	{
		FMcMob* Llama = new FMcMob(*LlamaType);
		Llama->Body.Position = FVector(X, 1.0, Z);
		Manager.Add(TUniquePtr<FMcEntity>(Llama));
		return Llama;
	};
	FMcMob* A = Make(4.5, 2.5);
	FMcMob* B = Make(2.5, 3.5);
	FMcMob* C = Make(6.5, 3.5);
	A->bLeashed = true;
	A->Body.bOnGround = true;
	// The one on the lead is walked on (the lead's own tests cover how it is pulled).
	for (int32 Tick = 0; Tick < 200; ++Tick)
	{
		A->Body.Position.Z += 0.2;
		A->Body.Velocity = FVector::ZeroVector;
		A->LeashPlayerPos = A->Body.Position + FVector(0.0, 0.0, 1.0);
		Manager.PlayerPositions = { A->LeashPlayerPos };
		Manager.Tick();
	}
	const double GapB = A->Body.Position.Z - B->Body.Position.Z;
	const double GapC = B->Body.Position.Z - C->Body.Position.Z;
	TestTrue(*FString::Printf(TEXT("B trails A by about 3 blocks, got %f"), GapB), GapB > 2.6 && GapB < 3.6);
	TestTrue(*FString::Printf(TEXT("C trails B by about 3 blocks, got %f"), GapC), GapC > 2.6 && GapC < 3.6);

	// Spit.
	FMcMob* Spitter = Make(4.5, 100.5);
	Manager.PlayerPositions = { FVector(4.5, 1.0, 96.5) };
	Spitter->HurtEvents = 1;
	int32 Spits = 0;
	double Lowest = 0.0;
	for (int32 Tick = 0; Tick < 90; ++Tick)
	{
		Manager.Tick();
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			if (Entity->GetType() == FMcLlamaSpit::StaticType() && Entity->Age == 1)
			{
				++Spits;
				TestTrue(*FString::Printf(TEXT("a spit goes at 1.5 a tick, got %f"), Entity->Body.Velocity.Size()), FMath::Abs(Entity->Body.Velocity.Size() - 1.5 * 0.99) < 0.2);
				Lowest = Entity->Body.Position.Y;
			}
		}
	}
	TestTrue(*FString::Printf(TEXT("two or three spits in 90 ticks (one in 40), got %d"), Spits), Spits >= 2 && Spits <= 3);
	TestTrue(TEXT("they start 1.68 above its feet"), FMath::Abs(Lowest - 2.68) < 0.5);
	return true;
}
