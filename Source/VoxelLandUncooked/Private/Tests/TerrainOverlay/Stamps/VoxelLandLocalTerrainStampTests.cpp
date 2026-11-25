#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandTerrainSphereTestFixture.h"

#include "Async/Async.h"
#include "Data/Terrain/ChunkLocal/VoxelLandChunkLocalTerrainConfig.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"
#include <functional>

class FVoxelLandM6LocalStampFixture final
{
public:
	static FString Run();

private:
	using FFixture = FVoxelLandM6SphereTestFixture;
	using FGeneratorPtr = TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>;
	using FGridPtr = TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe>;
	using FChunkPtr = TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>;
	static constexpr int32 RightX = -700;
	static constexpr int64 SeamCm = int64(RightX) * FFixture::Cells * FFixture::VoxelSizeCm;

	static bool Setup(FVoxelLandGeneratorSettings& Settings, FVoxelLandM4UnlimitedConfig& Config, FGridPtr& Grid);
	static bool CheckOverlapAndMesh(const FVoxelLandChunkData& Left, const FVoxelLandChunkData& Right);
	static bool Mesh(const FVoxelLandChunkData& Chunk, FVoxelLandMeshingScratch& Scratch, FVoxelLandMeshData& Result);
	static bool CountPoll(int32& Polls)
	{
		++Polls;
		return false;
	}
	static bool CancelOnZero(int32& Remaining) { return --Remaining == 0; }
};

bool FVoxelLandM6LocalStampFixture::Setup(
	FVoxelLandGeneratorSettings& Settings, FVoxelLandM4UnlimitedConfig& Config, FGridPtr& Grid)
{
	FGridPtr Unused;
	if (!FFixture::CreateInputs(-8000, Settings, Unused))
	{
		return false;
	}

	FVoxelLandGeneratorSettings ControlSettings;
	FVoxelLandProgramDraft ControlDraft;
	ControlDraft.FormatVersion = 2;
	ControlDraft.RegisterCount = 1;
	ControlDraft.Constants = {0.f};
	FVoxelLandInstruction Load;
	Load.Op = EVoxelLandOpcode::LoadConstant;
	FVoxelLandInstruction Store;
	Store.Op = EVoxelLandOpcode::StoreDensity;
	ControlDraft.Instructions = {Load, Store};
	FGeneratorPtr Control;
	if (FVoxelLandProgram::Validate(ControlDraft, ControlSettings.Program)
		|| FVoxelLandGenerator::Create(ControlSettings, Control))
	{
		return false;
	}

	TArray<int32> ZeroCurve;
	ZeroCurve.Init(0, FVoxelLandM4CurveLut::SampleCount);
	TSharedPtr<const FVoxelLandM4CurveLut, ESPMode::ThreadSafe> Curve;
	FString CurveError;
	if (!FVoxelLandM4CurveLut::Create(ZeroCurve, 0, 0, Curve, CurveError))
	{
		return false;
	}
	Config.ContinentalGenerator = Control;
	Config.MountainGenerator = Control;
	Config.PeaksGenerator = Control;
	Config.ContinentalCurve = Curve;
	Config.MountainCurve = Curve;
	Config.PeaksCurve = Curve;
	Config.BaseQ8 = -8000 * 256;
	Config.SmoothOverride.Emplace();
	Config.SmoothOverride->bEnabled = true;
	Config.SmoothOverride->Strength = 0.5f;
	TArray<int32> Heights;
	Heights.Init(Config.BaseQ8, 9);
	return FVoxelLandM4HeightGrid::Create(SeamCm - 6400, -6400, 2, 2, 6400, Heights, Grid);
}

bool FVoxelLandM6LocalStampFixture::Mesh(
	const FVoxelLandChunkData& Chunk, FVoxelLandMeshingScratch& Scratch, FVoxelLandMeshData& Result)
{
	return VoxelLandMeshing::BuildSurfaceNets(
			   Chunk, FFixture::VoxelSizeCm, Scratch, Result, &FFixture::NeverCancel, true)
		== EVoxelLandMeshResult::Success;
}

bool FVoxelLandM6LocalStampFixture::CheckOverlapAndMesh(
	const FVoxelLandChunkData& Left, const FVoxelLandChunkData& Right)
{
	FVoxelLandMeshData A, B;
	FVoxelLandMeshingScratch ScratchA, ScratchB;
	if (!Mesh(Left, ScratchA, A) || !Mesh(Right, ScratchB, B) || A.Indices.IsEmpty() || B.Indices.IsEmpty())
	{
		return false;
	}
	const FVector OrigA = VoxelLandMeshCoordinates::RenderOrigin(Left.Key.Coordinate, FFixture::VoxelSizeCm);
	const FVector OrigB = VoxelLandMeshCoordinates::RenderOrigin(Right.Key.Coordinate, FFixture::VoxelSizeCm);
	constexpr int32 N = FFixture::Samples;
	constexpr int32 CellAxis = N - 1;
	int32 SharedVertices = 0;
	for (int32 Z = -1; Z <= FFixture::Cells + 1; ++Z)
	{
		for (int32 Y = -1; Y <= FFixture::Cells + 1; ++Y)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				const FIntVector L(X + FFixture::Cells, Y, Z);
				const FIntVector R(X, Y, Z);
				if (Left.Density[FFixture::SampleIndex(L, N)] != Right.Density[FFixture::SampleIndex(R, N)])
				{
					return false;
				}
				if (X == 1 || Y == FFixture::Cells + 1 || Z == FFixture::Cells + 1)
				{
					continue;
				}
				const int32 VA = ScratchA.CellVertices[FFixture::SampleIndex(L, CellAxis)];
				const int32 VB = ScratchB.CellVertices[FFixture::SampleIndex(R, CellAxis)];
				if ((VA == INDEX_NONE) != (VB == INDEX_NONE))
				{
					return false;
				}
				if (VA != INDEX_NONE)
				{
					if (OrigA + FVector(A.Vertices[VA].Position) != OrigB + FVector(B.Vertices[VB].Position))
					{
						return false;
					}
					++SharedVertices;
				}
			}
		}
	}
	return SharedVertices > 0;
}

FString FVoxelLandM6LocalStampFixture::Run()
{
	FVoxelLandGeneratorSettings Settings;
	FVoxelLandM4UnlimitedConfig Config;
	FGridPtr Grid;
	FGeneratorPtr Empty, Disabled, Active, Finite;
	if (!Setup(Settings, Config, Grid) || FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Empty))
	{
		return TEXT("Erosion-disabled local baseline setup failed");
	}
	FVoxelLandM6Stamp Stamps[2];
	Stamps[0].Id = FGuid(0x4d36, 2, 0, 1);
	Stamps[0].Shape = EVoxelLandM6StampShape::Capsule;
	Stamps[0].OriginCm = FInt64Vector(SeamCm - 435, -1061, 1100);
	Stamps[0].SegmentEndCm = FInt64Vector(SeamCm + 615, -930, 1260);
	Stamps[0].RadiusCm = 373;
	Stamps[1].Id = FGuid(0x4d36, 2, 0, 2);
	Stamps[1].Operation = EVoxelLandM6StampOperation::Subtract;
	Stamps[1].OriginCm = FInt64Vector((int64(1) << 45) - 100, 0, 0);
	Stamps[1].RadiusCm = 100;
	Settings.M6Stamps.Stamps = MakeArrayView(Stamps);
	Settings.M6Stamps.bEnabled = false;
	if (FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Disabled))
	{
		return TEXT("Disabled local stamp setup failed");
	}
	Settings.M6Stamps.bEnabled = true;
	if (FVoxelLandGenerator::CreateM4Unlimited(Settings, Config, Active)
		|| FFixture::CreateGenerator(Settings, Grid, true, Finite))
	{
		return TEXT("Far logical stamp setup failed");
	}
	if (!Empty->GetM6StampIdentity().IsEmpty() || !Disabled->GetM6StampIdentity().IsEmpty()
		|| Active->GetM6StampIdentity().IsEmpty())
	{
		return TEXT("Local stamp identity did not follow activation");
	}

	TUniquePtr<FVoxelLandWorkerContext> EmptyWorker, DisabledWorker, ActiveWorker, FiniteWorker;
	if (Empty->CreateWorker(EmptyWorker) || Disabled->CreateWorker(DisabledWorker) || Active->CreateWorker(ActiveWorker)
		|| Finite->CreateWorker(FiniteWorker))
	{
		return TEXT("Local stamp workers could not be created");
	}
	const FVoxelLandChunkKey Keys[] = {{FIntVector(RightX - 1, -1, 0)}, {FIntVector(RightX, -1, 0)}};
	FChunkPtr Forward[2];
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FChunkPtr Baseline, Bypassed, FiniteResult;
		if (Empty->GenerateChunk(Keys[Index], *EmptyWorker, Baseline)
			|| Disabled->GenerateChunk(Keys[Index], *DisabledWorker, Bypassed)
			|| Active->GenerateChunk(Keys[Index], *ActiveWorker, Forward[Index])
			|| Finite->GenerateChunk(Keys[Index], *FiniteWorker, FiniteResult))
		{
			return TEXT("Ordinary local or finite generation returned an error");
		}
		if (Baseline->Hash != Bypassed->Hash || Baseline->Density != Bypassed->Density
			|| Forward[Index]->Hash == Baseline->Hash || Forward[Index]->Hash != FiniteResult->Hash
			|| Forward[Index]->Density != FiniteResult->Density)
		{
			return TEXT("Local stamps differ from finite composition, or disabled density changed");
		}
	}
	if (!CheckOverlapAndMesh(*Forward[0], *Forward[1]))
	{
		return TEXT("Far negative-coordinate local stamp density or CPU mesh seam differs");
	}
	for (int32 Index = 1; Index >= 0; --Index)
	{
		FChunkPtr Reverse;
		if (Active->GenerateChunk(Keys[Index], *ActiveWorker, Reverse) || Forward[Index]->Hash != Reverse->Hash
			|| Forward[Index]->Density != Reverse->Density)
		{
			return TEXT("Reversing local chunk requests changed stamped output");
		}
	}

	int32 BaselinePolls = 0;
	FChunkPtr Unused;
	if (Empty->GenerateChunkCancellable(
			Keys[0], *EmptyWorker, Unused, std::bind_front(&CountPoll, std::ref(BaselinePolls))))
	{
		return TEXT("Local cancellation baseline failed");
	}
	int32 RemainingPolls = BaselinePolls + 8;
	FChunkPtr Protected = Forward[0];
	const FVoxelLandError Cancel = Active->GenerateChunkCancellable(
		Keys[0], *ActiveWorker, Protected, std::bind_front(&CancelOnZero, std::ref(RemainingPolls)));
	if (Cancel.Code != EVoxelLandErrorCode::Cancelled || !Cancel.Message.Contains(TEXT("stamp evaluation"))
		|| Protected != Forward[0])
	{
		return TEXT("Local stamp cancellation lost prior output");
	}
	return {};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM6LocalStampTest, "VoxelLand.M6.Stamps.LocalAcrossSeam",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM6LocalStampTest::RunTest(const FString& Parameters)
{
	const FString Error = Async(EAsyncExecution::ThreadPool, &FVoxelLandM6LocalStampFixture::Run).Get();
	return TestTrue(
		Error.IsEmpty() ? TEXT("Erosion-disabled local stamps across a real far seam") : Error, Error.IsEmpty());
}
#endif
