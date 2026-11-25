#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM4RuntimeTestInputs.h"
#include "VoxelLandM6SphereTestFixture.h"

#include "Async/Async.h"
#include "Data/Assets/VoxelLandM4WorldConfig.h"
#include "Data/Assets/VoxelLandM6WorldConfig.h"
#include "Data/Assets/VoxelLandTerrainGraph.h"
#include "Data/Structs/VoxelLandM4IdentityRecord.h"
#include "Data/Structs/VoxelLandM4WorldSnapshot.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "World/VoxelLandM4WorldIdentity.h"
#include "World/VoxelLandWorld.h"
#include <functional>

class FVoxelLandM6RuntimeCommand final : public IAutomationLatentCommand
{
public:
	FVoxelLandM6RuntimeCommand(FAutomationTestBase& InTest, UWorld& InWorld, AVoxelLandWorld& InActor,
		UVoxelLandM6WorldConfig& InConfig, TArray<uint8>&& InExpectedBaseline)
		: Test(InTest)
		, World(InWorld)
		, Actor(InActor)
		, Config(InConfig)
		, ExpectedBaseline(MoveTemp(InExpectedBaseline))
		, Start(FPlatformTime::Seconds())
	{
	}

	bool Update() override;
	void Cleanup();
	static bool PrepareBaselineIdentity(const UVoxelLandM4WorldConfig& Config, TArray<uint8>& Bytes, FString& Error);

private:
	using FCheck = bool (FVoxelLandM6RuntimeCommand::*)();
	static bool ReadU32(TConstArrayView<uint8> Bytes, int32& Cursor, uint32& Out);
	static bool HasVLW6Sections(TConstArrayView<uint8> Bytes, TConstArrayView<uint8> Baseline, bool bRedistance);
	bool Fail(const FString& Message);
	bool CheckFirstInitialization();
	bool CheckStampedChunks();
	bool CheckRepeatedInitialization();
	bool CheckStaleInitialization();
	bool CheckRedistanceIdentity();
	bool CheckEmptyOverlay();
	bool CheckLegacyInitialization();
	bool CheckBaseChunk();

	FAutomationTestBase& Test;
	UWorld& World;
	AVoxelLandWorld& Actor;
	UVoxelLandM6WorldConfig& Config;
	TArray<uint8> ExpectedBaseline;
	TArray<uint8> PublishedIdentity;
	TArray<uint8> FiniteIdentity;
	double Start;
	FCheck Check = &FVoxelLandM6RuntimeCommand::CheckFirstInitialization;
	bool bLocal = false;
};

bool FVoxelLandM6RuntimeCommand::PrepareBaselineIdentity(
	const UVoxelLandM4WorldConfig& Config, TArray<uint8>& Bytes, FString& Error)
{
	FVoxelLandM4WorldSnapshot Snapshot;
	FVoxelLandM4IdentityRecord Baseline;
	if (!Config.CaptureSnapshot(Snapshot, Error)
		|| !Async(EAsyncExecution::ThreadPool, std::bind_front(
			&VoxelLandM4WorldIdentity::Capture, std::cref(Snapshot), std::ref(Baseline), std::ref(Error))).Get())
	{
		return false;
	}
	Bytes = MoveTemp(Baseline.CanonicalBytes);
	return true;
}

bool FVoxelLandM6RuntimeCommand::ReadU32(TConstArrayView<uint8> Bytes, int32& Cursor, uint32& Out)
{
	if (Cursor < 0 || Cursor > Bytes.Num() - 4)
	{
		return false;
	}
	Out = uint32(Bytes[Cursor]) | (uint32(Bytes[Cursor + 1]) << 8)
		| (uint32(Bytes[Cursor + 2]) << 16) | (uint32(Bytes[Cursor + 3]) << 24);
	Cursor += 4;
	return true;
}

bool FVoxelLandM6RuntimeCommand::HasVLW6Sections(
	TConstArrayView<uint8> Bytes, TConstArrayView<uint8> Baseline, bool bRedistance)
{
	constexpr uint32 Tags[] = {0x5453364d, 0x4c46364d, 0x4452364d}; // M6ST, M6FL, M6RD.
	int32 Cursor = 0;
	uint32 Tag = 0, Version = 0, Length = 0;
	if (!ReadU32(Bytes, Cursor, Tag) || Tag != 0x36574c56 || !ReadU32(Bytes, Cursor, Version) || Version != 1
		|| !ReadU32(Bytes, Cursor, Length) || Length != uint32(Baseline.Num()) || Length > uint32(Bytes.Num() - Cursor)
		|| FMemory::Memcmp(Bytes.GetData() + Cursor, Baseline.GetData(), Length) != 0)
	{
		return false;
	}
	Cursor += int32(Length);
	for (int32 Section = 0; Section < UE_ARRAY_COUNT(Tags); ++Section)
	{
		if (!ReadU32(Bytes, Cursor, Length) || Length > uint32(Bytes.Num() - Cursor))
		{
			return false;
		}
		if (Section == 2 && !bRedistance)
		{
			if (Length != 0)
			{
				return false;
			}
			continue;
		}
		int32 SectionCursor = Cursor;
		if (Length < 4 || !ReadU32(Bytes, SectionCursor, Tag) || Tag != Tags[Section])
		{
			return false;
		}
		Cursor += int32(Length);
	}
	return Cursor == Bytes.Num();
}

bool FVoxelLandM6RuntimeCommand::Update()
{
	if (FPlatformTime::Seconds() - Start > 45.0)
	{
		return Fail(TEXT("M6 data-only actor fixture timed out"));
	}
	Actor.Tick(0.016f);
	return Actor.bInitializing ? false : (this->*Check)();
}

bool FVoxelLandM6RuntimeCommand::CheckFirstInitialization()
{
	if (!Actor.LastInitializationError.IsEmpty() || !Actor.GetM6WorldIdentity(PublishedIdentity)
		|| !HasVLW6Sections(PublishedIdentity, ExpectedBaseline, false)
		|| (bLocal && PublishedIdentity == FiniteIdentity))
	{
		return Fail(FString::Printf(TEXT("M6 authored identity or initialization mismatch: %s"), *Actor.LastInitializationError));
	}
	if (!Actor.RequestDataChunk(FIntVector(-1, 0, 0)))
	{
		return Fail(TEXT("M6 data-only actor rejected adjacent demand"));
	}
	Check = &FVoxelLandM6RuntimeCommand::CheckStampedChunks;
	return false;
}

bool FVoxelLandM6RuntimeCommand::CheckStampedChunks()
{
	const FVoxelLandChunkKey Origin;
	const FVoxelLandChunkKey Neighbor{FIntVector(-1, 0, 0)};
	const TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> First = Actor.FindChunk(Origin);
	const TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Second = Actor.FindChunk(Neighbor);
	if (!First || !Second)
	{
		FString Error;
		if (Actor.GetDataChunkError(Origin.Coordinate, Error) || Actor.GetDataChunkError(Neighbor.Coordinate, Error))
		{
			return Fail(Error);
		}
		return false;
	}

	using FFixture = FVoxelLandM6SphereTestFixture;
	const int32 Fill = FFixture::SampleIndex(FIntVector(0, 6, 2), FFixture::Samples);
	const int32 SharedFill = FFixture::SampleIndex(FIntVector(32, 6, 2), FFixture::Samples);
	const int32 Above = FFixture::SampleIndex(FIntVector(0, 6, 3), FFixture::Samples);
	if (First->Density[Fill] != -819 || First->Density[Above] != 205 || Second->Density[SharedFill] != -819
		|| TActorIterator<AVoxelLandRegion>(&World))
	{
		return Fail(TEXT("Authored foundation density or data-only rendering differs across the real seam"));
	}

	if (bLocal)
	{
		Config.Overlay.Redistance.bEnabled = true;
		Check = &FVoxelLandM6RuntimeCommand::CheckRedistanceIdentity;
	}
	else
	{
		FiniteIdentity = PublishedIdentity;
		Check = &FVoxelLandM6RuntimeCommand::CheckRepeatedInitialization;
	}
	if (!Actor.InitializeM6FromSettings(&Config, FIntVector::ZeroValue))
	{
		return Fail(Actor.LastInitializationError);
	}
	return false;
}

bool FVoxelLandM6RuntimeCommand::CheckRepeatedInitialization()
{
	TArray<uint8> Identity;
	if (!Actor.LastInitializationError.IsEmpty() || !Actor.GetM6WorldIdentity(Identity) || Identity != FiniteIdentity
		|| !Actor.InitializeM6FromSettings(&Config, FIntVector::ZeroValue))
	{
		return Fail(TEXT("Identical authored inputs changed identity or a refresh failed"));
	}
	++Config.Overlay.Stamps[0].HalfExtentCm.X;
	Check = &FVoxelLandM6RuntimeCommand::CheckStaleInitialization;
	return false;
}

bool FVoxelLandM6RuntimeCommand::CheckStaleInitialization()
{
	TArray<uint8> Identity;
	if (!Actor.LastInitializationError.Contains(TEXT("superseded")) || !Actor.GetM6WorldIdentity(Identity)
		|| Identity != FiniteIdentity)
	{
		return Fail(TEXT("A stale authored overlay replaced the previous identity"));
	}
	FVoxelLandM6Stamp& Clear = Config.Overlay.Stamps[0];
	Clear.HalfExtentCm.X = 0;
	if (Actor.InitializeM6FromSettings(&Config, FIntVector::ZeroValue)
		|| !Actor.LastInitializationError.Contains(TEXT("half extent"))
		|| !Actor.GetM6WorldIdentity(Identity) || Identity != FiniteIdentity)
	{
		return Fail(TEXT("Invalid overlay admission did not preserve the published world"));
	}

	Clear.HalfExtentCm.X = 500;
	Config.Baseline->bUnlimited = true;
	FString Error;
	if (!PrepareBaselineIdentity(*Config.Baseline, ExpectedBaseline, Error)
		|| !Actor.InitializeM6FromSettings(&Config, FIntVector::ZeroValue))
	{
		return Fail(Error.IsEmpty() ? Actor.LastInitializationError : Error);
	}
	bLocal = true;
	Check = &FVoxelLandM6RuntimeCommand::CheckFirstInitialization;
	return false;
}

bool FVoxelLandM6RuntimeCommand::CheckRedistanceIdentity()
{
	TArray<uint8> Identity;
	if (!Actor.LastInitializationError.IsEmpty() || !Actor.GetM6WorldIdentity(Identity) || Identity == PublishedIdentity
		|| !HasVLW6Sections(Identity, ExpectedBaseline, true))
	{
		return Fail(TEXT("Enabled re-distance was absent from the authored identity"));
	}
	Config.Overlay.bStampsEnabled = false;
	Config.Overlay.bFiltersEnabled = false;
	Config.Overlay.Redistance.bEnabled = false;
	Check = &FVoxelLandM6RuntimeCommand::CheckEmptyOverlay;
	if (!Actor.InitializeM6FromSettings(&Config, FIntVector::ZeroValue))
	{
		return Fail(Actor.LastInitializationError);
	}
	return false;
}

bool FVoxelLandM6RuntimeCommand::CheckBaseChunk()
{
	TArray<uint8> Identity;
	const FVoxelLandChunkKey Key;
	const TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = Actor.FindChunk(Key);
	if (!Actor.LastInitializationError.IsEmpty() || Actor.GetM6WorldIdentity(Identity) || !Identity.IsEmpty())
	{
		Fail(TEXT("Baseline initialization retained an M6 identity or failed"));
		return false;
	}
	if (!Chunk)
	{
		return false;
	}
	using FFixture = FVoxelLandM6SphereTestFixture;
	const int32 Fill = FFixture::SampleIndex(FIntVector(0, 6, 2), FFixture::Samples);
	if (Chunk->Density[Fill] != -1024 || TActorIterator<AVoxelLandRegion>(&World))
	{
		Fail(TEXT("Baseline density or data-only behavior changed"));
		return false;
	}
	return true;
}

bool FVoxelLandM6RuntimeCommand::CheckEmptyOverlay()
{
	if (!CheckBaseChunk())
	{
		return Check == nullptr;
	}
	Check = &FVoxelLandM6RuntimeCommand::CheckLegacyInitialization;
	if (!Actor.InitializeM4FromSettings(Config.Baseline.Get(), FIntVector::ZeroValue))
	{
		return Fail(Actor.LastInitializationError);
	}
	return false;
}

bool FVoxelLandM6RuntimeCommand::CheckLegacyInitialization()
{
	if (!CheckBaseChunk())
	{
		return Check == nullptr;
	}
	Cleanup();
	return true;
}

bool FVoxelLandM6RuntimeCommand::Fail(const FString& Message)
{
	Test.AddError(Message.IsEmpty() ? TEXT("M6 actor initialization failed") : Message);
	Cleanup();
	Check = nullptr;
	return true;
}

void FVoxelLandM6RuntimeCommand::Cleanup()
{
	Actor.Shutdown();
	World.DestroyWorld(false);
	GEngine->DestroyWorldContext(&World);
	World.RemoveFromRoot();
	Config.RemoveFromRoot();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM6DataOnlyRuntimeTest, "VoxelLand.M6.Runtime.AuthoredDataOnly",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM6DataOnlyRuntimeTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("engine available"), GEngine))
	{
		return false;
	}
	UVoxelLandM6WorldConfig* M6 = NewObject<UVoxelLandM6WorldConfig>();
	M6->AddToRoot();
	UVoxelLandM4WorldConfig* Baseline = NewObject<UVoxelLandM4WorldConfig>(M6);
	M6->Baseline = Baseline;
	UVoxelLandTerrainGraph* Graph = NewObject<UVoxelLandTerrainGraph>(Baseline);
	FString Error;
	if (!FVoxelLandM4RuntimeTestInputs::MakeProgram(*Graph, Error))
	{
		M6->RemoveFromRoot();
		AddError(Error);
		return false;
	}

	Baseline->Continental.Graph = Baseline->Mountain.Graph = Baseline->Peaks.Graph = Graph;
	FVoxelLandM4RuntimeTestInputs::SetCurve(Baseline->Continental);
	FVoxelLandM4RuntimeTestInputs::SetCurve(Baseline->Mountain);
	FVoxelLandM4RuntimeTestInputs::SetCurve(Baseline->Peaks);
	TArray<int32> Samples;
	Samples.Init(0, 4097);
	Baseline->BaseHeightQ8 = 300 * 256;
	Baseline->CellsX = Baseline->CellsY = 8;
	Baseline->OriginXCm = Baseline->OriginYCm = -25600;
	Baseline->GridSpacingCm = 6400;
	Baseline->HorizontalRadius = Baseline->VerticalRadius = 1;
	Baseline->bGlobalErosion = false;
	TArray<uint8> ExpectedBaseline;
	if (!Baseline->SetBakedCurves(Samples, Samples, Samples, 1, Error)
		|| !FVoxelLandM6RuntimeCommand::PrepareBaselineIdentity(*Baseline, ExpectedBaseline, Error))
	{
		M6->RemoveFromRoot();
		AddError(Error);
		return false;
	}

	FVoxelLandM6Stamp Clear;
	Clear.Id = FGuid(0x4d360005, 0, 0, 1);
	Clear.Shape = EVoxelLandM6StampShape::Box;
	Clear.Operation = EVoxelLandM6StampOperation::Subtract;
	Clear.OriginCm = FInt64Vector(0, 600, 360);
	Clear.HalfExtentCm = FIntVector(500, 500, 300);
	FVoxelLandM6Stamp Fill = Clear;
	Fill.Id = FGuid(0x4d360005, 0, 0, 2);
	Fill.Operation = EVoxelLandM6StampOperation::Add;
	Fill.OriginCm = FInt64Vector(0, 600, 140);
	Fill.HalfExtentCm = FIntVector(400, 400, 140);
	M6->Overlay.Stamps = {Clear, Fill};
	FVoxelLandM6FilterStage& Smooth = M6->Overlay.Filters.AddDefaulted_GetRef();
	Smooth.Id = FGuid(0x4d360005, 0, 0, 3);
	Smooth.MinCm = FInt64Vector(500, 500, 100);
	Smooth.MaxCm = FInt64Vector(1450, 1450, 300);
	Smooth.Strength = 32768;

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("M6 world created"), World))
	{
		M6->RemoveFromRoot();
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVoxelLandWorld* Actor = World->SpawnActor<AVoxelLandWorld>();
	if (!TestNotNull(TEXT("M6 actor created"), Actor))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
		M6->RemoveFromRoot();
		return false;
	}
	Actor->bDataOnly = true;
	Actor->DispatchBeginPlay();
	const TSharedPtr<FVoxelLandM6RuntimeCommand> Command =
		MakeShared<FVoxelLandM6RuntimeCommand>(*this, *World, *Actor, *M6, MoveTemp(ExpectedBaseline));
	if (!Actor->InitializeM6FromSettings(M6, FIntVector::ZeroValue))
	{
		AddError(Actor->LastInitializationError);
		Command->Cleanup();
		return false;
	}
	FAutomationTestFramework::Get().EnqueueLatentCommand(Command);
	return true;
}
#endif
