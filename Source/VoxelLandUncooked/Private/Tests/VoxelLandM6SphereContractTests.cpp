#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM6SphereTestFixture.h"

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include <functional>

class FVoxelLandM6SphereContractFixture final
{
public:
	static FString Run();

private:
	static bool RejectsInvalidSettings(FVoxelLandGeneratorSettings Settings,
		const TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& Grid,
		const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& Prior);
	static bool TestSubtractSaturation();
	static bool CountPoll(int32& Polls)
	{
		++Polls;
		return false;
	}
	static bool CancelOnZero(int32& Remaining) { return --Remaining == 0; }
};

bool FVoxelLandM6SphereContractFixture::RejectsInvalidSettings(FVoxelLandGeneratorSettings Settings,
	const TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>& Grid,
	const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& Prior)
{
	using FFixture = FVoxelLandM6SphereTestFixture;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Output = Prior;
	FVoxelLandM6SphereStamp Stamp = FFixture::Stamp(1, EVoxelLandM6SphereOperation::Add, FIntVector::ZeroValue, 200);
	TArray<FVoxelLandM6SphereStamp> Stamps;
	Stamps.Init(Stamp, FVoxelLandM6SphereSettings::MaxStamps + 1);
	Settings.M6Spheres.Stamps = Stamps;
	if (!FFixture::CreateGenerator(Settings, Grid, false, Output) || Output != Prior)
	{
		return false;
	}
	Stamps.SetNum(2);
	Settings.M6Spheres.Stamps = Stamps;
	if (!FFixture::CreateGenerator(Settings, Grid, false, Output) || Output != Prior)
	{
		return false;
	}
	Settings.M6Spheres.Stamps = MakeArrayView(&Stamp, 1);
	++Settings.M6Spheres.Version;
	if (!FFixture::CreateGenerator(Settings, Grid, false, Output) || Output != Prior)
	{
		return false;
	}
	Settings.M6Spheres.Version = FVoxelLandM6SphereSettings::CurrentVersion;
	if (!FVoxelLandGenerator::Create(Settings, Output) || Output != Prior)
	{
		return false;
	}
	Stamp.Id.Invalidate();
	if (!FFixture::CreateGenerator(Settings, Grid, false, Output) || Output != Prior)
	{
		return false;
	}
	Stamp.Id = FGuid(0x4d36, 0, 0, 1);
	Stamp.Operation = EVoxelLandM6SphereOperation(255);
	if (!FFixture::CreateGenerator(Settings, Grid, false, Output) || Output != Prior)
	{
		return false;
	}
	Stamp.Operation = EVoxelLandM6SphereOperation::Add;
	Stamp.RadiusCm = 0;
	if (!FFixture::CreateGenerator(Settings, Grid, false, Output) || Output != Prior)
	{
		return false;
	}
	Stamp.RadiusCm = 200;
	Stamp.CenterCm = FIntVector(1999801, 0, 0);
	return bool(FFixture::CreateGenerator(Settings, Grid, false, Output)) && Output == Prior;
}

bool FVoxelLandM6SphereContractFixture::TestSubtractSaturation()
{
	using FFixture = FVoxelLandM6SphereTestFixture;
	FVoxelLandGeneratorSettings Settings;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	const FVoxelLandM6SphereStamp Stamp =
		FFixture::Stamp(1, EVoxelLandM6SphereOperation::Subtract, FIntVector::ZeroValue, 200);
	Settings.M6Spheres.Stamps = MakeArrayView(&Stamp, 1);
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
	return FFixture::CreateInputs(12000, Settings, Grid)
		&& !FFixture::CreateGenerator(Settings, Grid, false, Generator)
		&& !Generator->CreateWorker(Worker)
		&& !Generator->GenerateChunk({FIntVector(1, 0, 0)}, *Worker, Chunk)
		&& Chunk->Density[FFixture::SampleIndex(FIntVector(10, 0, 0), FFixture::Samples)] == MIN_int16;
}

FString FVoxelLandM6SphereContractFixture::Run()
{
	using FFixture = FVoxelLandM6SphereTestFixture;
	FVoxelLandGeneratorSettings Settings;
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> Grid;
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Empty, Disabled, Add, CutLast, AddLast, Tie;
	if (!FFixture::CreateInputs(-8000, Settings, Grid) || FFixture::CreateGenerator(Settings, Grid, false, Empty))
	{
		return TEXT("Could not create the real finite M4 baseline");
	}
	const FVoxelLandM6SphereStamp AddStamp =
		FFixture::Stamp(1, EVoxelLandM6SphereOperation::Add, FIntVector::ZeroValue, 200);
	const FVoxelLandM6SphereStamp CutStamp =
		FFixture::Stamp(2, EVoxelLandM6SphereOperation::Subtract, FIntVector::ZeroValue, 200);
	TArray<FVoxelLandM6SphereStamp> Stamps = {AddStamp, CutStamp};
	Settings.M6Spheres.Stamps = Stamps;
	Settings.M6Spheres.bEnabled = false;
	if (FFixture::CreateGenerator(Settings, Grid, false, Disabled))
	{
		return TEXT("Disabled stamps failed setup");
	}
	Settings.M6Spheres.bEnabled = true;
	if (FFixture::CreateGenerator(Settings, Grid, false, CutLast))
	{
		return TEXT("Ordered add/subtract setup failed");
	}
	Swap(Stamps[0], Stamps[1]);
	if (FFixture::CreateGenerator(Settings, Grid, false, AddLast))
	{
		return TEXT("Reverse add/subtract setup failed");
	}
	Settings.M6Spheres.Stamps = MakeArrayView(&AddStamp, 1);
	if (FFixture::CreateGenerator(Settings, Grid, false, Add))
	{
		return TEXT("Single add sphere setup failed");
	}
	const FVoxelLandM6SphereStamp TieStamp =
		FFixture::Stamp(1, EVoxelLandM6SphereOperation::Add, FIntVector::ZeroValue, 1);
	Settings.M6Spheres.Stamps = MakeArrayView(&TieStamp, 1);
	Settings.Profile.VoxelSizeCm = 2048.f;
	if (FFixture::CreateGenerator(Settings, Grid, false, Tie))
	{
		return TEXT("Half-unit rounding fixture setup failed");
	}
	Settings.Profile.VoxelSizeCm = FFixture::VoxelSizeCm;

	// Caller mutation after setup must not change either frozen stamp program.
	Stamps[0].RadiusCm = 500;
	const TConstArrayView<uint8> CutIdentity = CutLast->GetM6SphereIdentity();
	const TConstArrayView<uint8> AddIdentity = AddLast->GetM6SphereIdentity();
	constexpr int32 FirstOperationOffset = 12 + 16; // Three u32 header fields, then one 16-byte GUID.
	if (!Empty->GetM6SphereIdentity().IsEmpty() || !Disabled->GetM6SphereIdentity().IsEmpty()
		|| CutIdentity.Num() != 78 || AddIdentity.Num() != 78
		|| CutIdentity[0] != 'M' || CutIdentity[4] != 1 || CutIdentity[8] != 2
		|| CutIdentity[FirstOperationOffset] != uint8(EVoxelLandM6SphereOperation::Add)
		|| AddIdentity[FirstOperationOffset] != uint8(EVoxelLandM6SphereOperation::Subtract))
	{
		return TEXT("Missing or incorrectly ordered versioned stamp identity");
	}

	using FChunkPtr = TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>;
	const FVoxelLandChunkKey Key;
	FChunkPtr Chunks[6];
	const FVoxelLandGenerator* Generators[] = {Empty.Get(), Disabled.Get(), Add.Get(), CutLast.Get(), AddLast.Get(), Tie.Get()};
	TUniquePtr<FVoxelLandWorkerContext> Workers[6];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Generators); ++Index)
	{
		if (Generators[Index]->CreateWorker(Workers[Index])
			|| Generators[Index]->GenerateChunk(Key, *Workers[Index], Chunks[Index]))
		{
			return TEXT("Ordinary finite chunk generation failed");
		}
	}
	const int32 Center = FFixture::SampleIndex(FIntVector::ZeroValue, FFixture::Samples);
	const int32 Surface = FFixture::SampleIndex(FIntVector(2, 0, 0), FFixture::Samples);
	const int32 Outside = FFixture::SampleIndex(FIntVector(3, 0, 0), FFixture::Samples);
	if (Chunks[0]->Hash != Chunks[1]->Hash || Chunks[0]->Density != Chunks[1]->Density || Chunks[0]->Hash == Chunks[2]->Hash)
	{
		return TEXT("Disabled density/hash differs from baseline or active hash did not change");
	}
	if (Chunks[2]->Density[Center] != -2048 || Chunks[2]->Density[Surface] != 0
		|| Chunks[2]->Density[Outside] != 1024 || Chunks[3]->Density[Center] != 2048
		|| Chunks[4]->Density[Center] != -2048 || Chunks[5]->Density[Center] != -1
		|| Chunks[5]->Density[FFixture::SampleIndex(FIntVector(1, 0, 0), FFixture::Samples)] != 1024
		|| !TestSubtractSaturation())
	{
		return TEXT("Analytic CSG, boundary, rounding, order, or saturation mismatch");
	}

	const FVoxelLandChunkKey BeyondSphereDomain{FIntVector(625, 0, 0)};
	FChunkPtr Protected = Chunks[2];
	if (Empty->ValidateM4ChunkDemand(BeyondSphereDomain)
		|| Add->ValidateM4ChunkDemand(BeyondSphereDomain).Code != EVoxelLandErrorCode::NonFiniteCoordinate
		|| Add->GenerateChunk(BeyondSphereDomain, *Workers[2], Protected).Code != EVoxelLandErrorCode::NonFiniteCoordinate
		|| Protected != Chunks[2])
	{
		return TEXT("Stored footprint rejection did not preserve its prior chunk");
	}
	// Count baseline polls, then cancel inside the additional stamp loop.
	int32 BaselinePolls = 0;
	FChunkPtr Unused;
	if (Empty->GenerateChunkCancellable(
			Key, *Workers[0], Unused, std::bind_front(&CountPoll, std::ref(BaselinePolls))))
	{
		return TEXT("Baseline cancellation fixture did not finish");
	}
	int32 Remaining = BaselinePolls + 8;
	const FVoxelLandError Cancel = Add->GenerateChunkCancellable(
		Key, *Workers[2], Protected, std::bind_front(&CancelOnZero, std::ref(Remaining)));
	if (Cancel.Code != EVoxelLandErrorCode::Cancelled || Protected != Chunks[2]
		|| !Cancel.Message.Contains(TEXT("stamp evaluation")))
	{
		return TEXT("Stamp-phase cancellation did not preserve its prior chunk");
	}
	if (!RejectsInvalidSettings(Settings, Grid, Add))
	{
		return TEXT("Invalid settings were accepted or invalid setup replaced the prior generator");
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM6SphereContractTest, "VoxelLand.M6.Sphere.Contract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM6SphereContractTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM6SphereContractFixture::Run).Get();
	return TestTrue(Error.IsEmpty() ? TEXT("Finite generation-time sphere contract") : Error, Error.IsEmpty());
}
#endif
