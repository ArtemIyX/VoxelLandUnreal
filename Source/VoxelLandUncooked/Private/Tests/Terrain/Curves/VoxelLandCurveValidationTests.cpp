#if WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Data/Program/Serialization/VoxelLandCanonicalWriter.h"
#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Worlds/VoxelLandTerrainGridLease.h"
#include "Data/Terrain/Controls/VoxelLandControlSnapshot.h"
#include "Generation/Terrain/Curves/VoxelLandCurveBake.h"
#include "Hash/xxhash.h"
#include "Misc/AutomationTest.h"
#include "World/Terrain/Curves/VoxelLandCurveValidation.h"
#include "World/Terrain/Worlds/VoxelLandTerrainPreparation.h"
namespace
{
constexpr EAutomationTestFlags M4CurveValidationFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
void M4RehashCurve(FVoxelLandM4CookedCurve& Curve)
{
	FCanonicalWriter W;
	for (int32 Sample : Curve.SamplesQ8)
	{
		W.U32(uint32(Sample));
	}
	const uint64 Hash = FXxHash64::HashBuffer(W.Bytes.GetData(), W.Bytes.Num()).Hash;
	FMemory::Memcpy(&Curve.SamplesHash, &Hash, sizeof(Hash));
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVoxelLandM4CurveValidationTest, "VoxelLand.M4.Curve.CookedValidation", M4CurveValidationFlags)
bool FVoxelLandM4CurveValidationTest::RunTest(const FString& Parameters)
{
	FVoxelLandM4ControlSnapshot Channel;
	FVoxelLandM4CurveAuthoringKey First, Middle, Last;
	First.OutputQ8 = -500;
	Middle.InputQ16 = 12345;
	Middle.OutputQ8 = 765;
	Last.InputQ16 = 65535;
	Last.OutputQ8 = 100;
	Channel.CurveKeys = {First, Middle, Last};
	Channel.CookedCurve.SourceKeys = Channel.CurveKeys;
	Channel.CookedCurve.BakeVersion = 1;
	TArray<FVoxelLandM4CurveKey> Keys;
	for (const FVoxelLandM4CurveAuthoringKey& K : Channel.CurveKeys)
	{
		FVoxelLandM4CurveKey BakedKey;
		BakedKey.InputQ16 = uint16(K.InputQ16);
		BakedKey.Output = K.OutputQ8;
		Keys.Add(BakedKey);
	}
	FString Error;
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	if (!TestTrue(TEXT("reference curve bakes"),
			VoxelLandM4CurveBake::Bake(Keys, -512000000, 512000000, Channel.CookedCurve.SamplesQ8, Curve, Error)))
	{
		return false;
	}
	M4RehashCurve(Channel.CookedCurve);
	TestTrue(TEXT("valid nontrivial bake accepted"), VoxelLandM4CurveValidation::Validate(Channel, Error));
	++Channel.CookedCurve.SamplesQ8[1400];
	M4RehashCurve(Channel.CookedCurve);
	TestFalse(
		TEXT("altered sample with recomputed hash rejected"), VoxelLandM4CurveValidation::Validate(Channel, Error));
	FVoxelLandM4WorldSnapshot Snapshot;
	Snapshot.Domain.bGlobalErosion = false;
	Snapshot.Continental = Snapshot.Mountain = Snapshot.Peaks = Channel;
	const auto Lease = MakeShared<FVoxelLandM4GridLease, ESPMode::ThreadSafe>();
	bool bPreparationRejected = false;
	Async(EAsyncExecution::ThreadPool, [&] {
		TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
		FString PrepareError;
		bPreparationRejected = !VoxelLandM4Preparation::Prepare(
								   Snapshot, Lease, [] { return false; }, Generator, PrepareError)
							&& !Generator && PrepareError.Contains(TEXT("source keys"));
	}).Get();
	TestTrue(TEXT("preparation rejects mismatched bake before publication"), bPreparationRejected);
	--Channel.CookedCurve.SamplesQ8[1400];
	M4RehashCurve(Channel.CookedCurve);
	Channel.CurveKeys[1].InputQ16 = 0;
	Channel.CookedCurve.SourceKeys = Channel.CurveKeys;
	TestFalse(TEXT("duplicate key positions rejected"), VoxelLandM4CurveValidation::Validate(Channel, Error));
	Channel.CurveKeys[1] = Middle;
	Channel.CookedCurve.SourceKeys = Channel.CurveKeys;
	Channel.CookedCurve.BakeVersion = 2;
	TestFalse(TEXT("unknown bake version rejected"), VoxelLandM4CurveValidation::Validate(Channel, Error));
	Channel.CookedCurve.BakeVersion = 1;
	Channel.CurveKeys[1].InputQ16 = 65536;
	Channel.CookedCurve.SourceKeys = Channel.CurveKeys;
	TestFalse(TEXT("input narrowing cannot wrap malformed keys"), VoxelLandM4CurveValidation::Validate(Channel, Error));
	return true;
}
#endif
