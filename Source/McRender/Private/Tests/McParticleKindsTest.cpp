#include "McParticleKinds.h"
#include "McParticleDefs.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcParticleKindsTest, "Mc.Particle.Kinds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMcParticleKindsTest::RunTest(const FString&)
{
	TestTrue(TEXT("the game defines many particles"), McParticleDefs::GetNames().Num() > 100);
	for (const McParticleKinds::FKind& Kind : McParticleKinds::GetAll())
	{
		TestTrue(*FString::Printf(TEXT("%s has pictures"), *Kind.Name.ToString()), !McParticleDefs::GetTextures(Kind.Name).IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s lives"), *Kind.Name.ToString()), Kind.LifeMin >= 1 && Kind.LifeMax >= Kind.LifeMin);
		TestTrue(*FString::Printf(TEXT("%s has a size"), *Kind.Name.ToString()), Kind.Size > 0.0f && Kind.Size < 2.0f);
		TestTrue(*FString::Printf(TEXT("%s loses less than it has"), *Kind.Name.ToString()), Kind.Grow > -1.0f);
	}
	// What the vanilla runs showed: a heart rises about two thirds of a block over 17 ticks; a crit shows 6 and falls.
	float Y = 0.0f, Velocity = 0.0f;
	const McParticleKinds::FKind& Heart = McParticleKinds::Find(TEXT("heart"));
	Velocity = Heart.Rise;
	for (int32 Tick = 0; Tick < Heart.LifeMin - 1; ++Tick)
	{
		Velocity -= Heart.Gravity;
		Y += Velocity;
		Velocity *= Heart.Friction;
	}
	TestTrue(TEXT("a heart rises about 0.66 blocks"), Y > 0.55f && Y < 0.8f);
	TestEqual(TEXT("a crit lives 7 ticks"), McParticleKinds::Find(TEXT("minecraft:crit")).LifeMin, 7);
	TestTrue(TEXT("a name with no row still gets a default"), McParticleKinds::Find(TEXT("minecraft:ash")).LifeMin >= 1);
	TestTrue(TEXT("the same row both ways"), &McParticleKinds::Find(TEXT("heart")) == &McParticleKinds::Find(TEXT("minecraft:heart")));
	// A dripstone drop hangs, then falls where it hung and goes when it lands.
	const McParticleKinds::FKind& Hang = McParticleKinds::Find(TEXT("dripping_dripstone_water"));
	TestEqual(TEXT("a hanging drop becomes a falling one"), Hang.Then, FName("minecraft:falling_dripstone_water"));
	const McParticleKinds::FKind& Fall = McParticleKinds::Find(Hang.Then);
	TestTrue(TEXT("which falls and stops on landing"), Fall.Gravity > 0.0f && Fall.bPhysics && Fall.bDieOnLand);
	return true;
}
