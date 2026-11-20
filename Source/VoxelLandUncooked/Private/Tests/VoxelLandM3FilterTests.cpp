#if WITH_DEV_AUTOMATION_TESTS

#include "VoxelLandM1Fixtures.h"
#include "Async/Async.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
using namespace VoxelLandM1Fixtures;

FVoxelLandProgramDraft FilterSource(bool bFilter, float Strength)
{
    FVoxelLandProgramDraft Draft;
    Draft.FormatVersion = 2;
    Draft.RegisterCount = 9;
    Draft.Constants = {1.f / 1024.f, 32.f, 1.f, Strength};
    Draft.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::LoadY, 1),
        Op(EVoxelLandOpcode::LoadZ, 2), Op(EVoxelLandOpcode::LoadConstant, 3, 0, 0, 0, 0),
        Op(EVoxelLandOpcode::Multiply, 0, 0, 3), Op(EVoxelLandOpcode::Multiply, 1, 1, 3),
        Op(EVoxelLandOpcode::Multiply, 2, 2, 3), Op(EVoxelLandOpcode::Abs, 4, 0),
        Op(EVoxelLandOpcode::Abs, 5, 1), Op(EVoxelLandOpcode::Abs, 6, 2),
        Op(EVoxelLandOpcode::Add, 7, 4, 5), Op(EVoxelLandOpcode::Add, 7, 7, 6),
        Op(EVoxelLandOpcode::LoadConstant, 8, 0, 0, 0, 1), Op(EVoxelLandOpcode::Subtract, 7, 7, 8),
        Op(EVoxelLandOpcode::StoreDensity, 0, 7)};
    Draft.SmoothFilter.bPresent = bFilter;
    Draft.SmoothFilter.Enabled.Index = 2;
    Draft.SmoothFilter.Strength.Index = 3;
    return Draft;
}

int32 RoundReference(int64 Value, int32 Divisor)
{
    const int64 Magnitude = Value < 0 ? -Value : Value;
    const int32 Rounded = int32((Magnitude + Divisor / 2) / Divisor);
    return Value < 0 ? -Rounded : Rounded;
}

int32 SourceReference(int32 X, int32 Y, int32 Z)
{
    // The pure VM evaluates this integer-valued expression exactly at voxel size 1024.
    return FMath::Clamp(FMath::Abs(X) + FMath::Abs(Y) + FMath::Abs(Z) - 32, -32768, 32767);
}

TArray<int16> SmoothReference(const FVoxelLandChunkKey& Key, int32 FixedStrength)
{
    TArray<int16> Out;
    Out.Reserve(FVoxelLandChunkData::SampleCount);
    for (int32 Z = -1; Z <= 33; ++Z)
    {
        for (int32 Y = -1; Y <= 33; ++Y)
        {
            for (int32 X = -1; X <= 33; ++X)
            {
                const int32 GX = 32 * Key.Coordinate.X + X, GY = 32 * Key.Coordinate.Y + Y, GZ = 32 * Key.Coordinate.Z + Z;
                int32 Sum = 0;
                for (int32 DZ = -1; DZ <= 1; ++DZ)
                {
                    for (int32 DY = -1; DY <= 1; ++DY)
                    {
                        for (int32 DX = -1; DX <= 1; ++DX)
                        {
                            Sum += SourceReference(GX + DX, GY + DY, GZ + DZ);
                        }
                    }
                }
                const int32 Average = RoundReference(Sum, 27);
                const int64 Blend = int64(SourceReference(GX, GY, GZ)) * (65535 - FixedStrength) + int64(Average) * FixedStrength;
                Out.Add(int16(FMath::Clamp(RoundReference(Blend, 65535), -32768, 32767)));
            }
        }
    }
    return Out;
}

FResult RunFilter(FVoxelLandProgramDraft Draft, FVoxelLandChunkKey Key)
{
    FVoxelLandZoneProfile Profile;
    Profile.VoxelSizeCm = 1024.f;
    return Async(EAsyncExecution::ThreadPool, [Draft, Profile, Key]() {
        return Generate(Draft, Profile, Key, FVoxelLandBackendOptions());
    }).Get();
}

bool MatchOverlap(const FVoxelLandChunkData& A, const FVoxelLandChunkData& B, int32 Axis)
{
    for (int32 U = 0; U < 35; ++U)
    {
        for (int32 V = 0; V < 35; ++V)
        {
            for (int32 P = 0; P < 3; ++P)
            {
                int32 AX = U, AY = V, AZ = 32 + P, BX = U, BY = V, BZ = P;
                if (Axis == 0)
                {
                    AX = 32 + P; AY = U; AZ = V; BX = P; BY = U; BZ = V;
                }
                else if (Axis == 1)
                {
                    AX = U; AY = 32 + P; AZ = V; BX = U; BY = P; BZ = V;
                }
                if (A.Density[AX + 35 * (AY + 35 * AZ)] != B.Density[BX + 35 * (BY + 35 * BZ)])
                {
                    return false;
                }
            }
        }
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3FilterReference, "VoxelLand.M3.FilterReferenceAndSeams", Flags)
bool FVoxelLandM3FilterReference::RunTest(const FString& Parameters)
{
    FVoxelLandChunkKey Key;
    Key.Coordinate = FIntVector(-1, -1, -1);
    const FResult Original = RunFilter(FilterSource(false, 0.f), Key);
    const FResult Zero = RunFilter(FilterSource(true, 0.f), Key);
    FVoxelLandProgramDraft DisabledDraft = FilterSource(true, 0.5f);
    DisabledDraft.Constants[2] = 0.f;
    const FResult Disabled = RunFilter(DisabledDraft, Key);
    const FResult Filtered = RunFilter(FilterSource(true, 0.5f), Key);
    if (Original.Error || Zero.Error || Disabled.Error || Filtered.Error || !Original.Chunk || !Zero.Chunk || !Disabled.Chunk || !Filtered.Chunk)
    {
        AddError(TEXT("Could not generate filter fixtures"));
        return false;
    }
    TestTrue(TEXT("Zero strength preserves original bytes and hash"), Original.Chunk->Density == Zero.Chunk->Density &&
        Original.Chunk->Hash == Zero.Chunk->Hash);
    TestTrue(TEXT("Disabled filter preserves original bytes and hash"), Original.Chunk->Density == Disabled.Chunk->Density &&
        Original.Chunk->Hash == Disabled.Chunk->Hash);
    TestTrue(TEXT("Nonuniform input is changed by smoothing"), Original.Chunk->Density != Filtered.Chunk->Density);
    const TArray<int16> Expected = SmoothReference(Key, 32768);
    TestTrue(TEXT("Filter matches independent integer reference"), Filtered.Chunk->Density == Expected);
    TestEqual(TEXT("Filtered hash matches canonical reference"), Filtered.Chunk->Hash, ReferenceHash(Expected));
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        FVoxelLandChunkKey Neighbor = Key;
        if (Axis == 0) { ++Neighbor.Coordinate.X; }
        else if (Axis == 1) { ++Neighbor.Coordinate.Y; }
        else { ++Neighbor.Coordinate.Z; }
        const FResult Adjacent = RunFilter(FilterSource(true, 0.5f), Neighbor);
        TestTrue(FString::Printf(TEXT("Three overlap planes match on axis %d"), Axis),
            !Adjacent.Error && Adjacent.Chunk && MatchOverlap(*Filtered.Chunk, *Adjacent.Chunk, Axis));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3FilterConcurrent, "VoxelLand.M3.FilterConcurrent", Flags)
bool FVoxelLandM3FilterConcurrent::RunTest(const FString& Parameters)
{
    FVoxelLandChunkKey Left;
    Left.Coordinate = FIntVector(-1, 0, -1);
    FVoxelLandChunkKey Right = Left;
    ++Right.Coordinate.X;
    const FResult Original = RunFilter(FilterSource(true, 0.75f), Left);
    if (Original.Error || !Original.Generator || !Original.Chunk)
    {
        AddError(TEXT("Failed to create a shared filtered generator"));
        return false;
    }
    const FResult ExpectedRight = Async(EAsyncExecution::ThreadPool, [Generator = Original.Generator, Right] {
        return GenerateShared(Generator, Right);
    }).Get();
    TFuture<FResult> FutureRight = Async(EAsyncExecution::ThreadPool, [Generator = Original.Generator, Right] {
        return GenerateShared(Generator, Right);
    });
    TFuture<FResult> FutureLeft = Async(EAsyncExecution::ThreadPool, [Generator = Original.Generator, Left] {
        return GenerateShared(Generator, Left);
    });
    const FResult ActualRight = FutureRight.Get();
    const FResult ActualLeft = FutureLeft.Get();
    TestTrue(TEXT("Concurrent left chunk matches serial output"), !ActualLeft.Error && ActualLeft.Chunk &&
        ActualLeft.Chunk->Density == Original.Chunk->Density && ActualLeft.Chunk->Hash == Original.Chunk->Hash);
    TestTrue(TEXT("Concurrent right chunk matches serial output"), !ExpectedRight.Error && ExpectedRight.Chunk &&
        !ActualRight.Error && ActualRight.Chunk && ActualRight.Chunk->Density == ExpectedRight.Chunk->Density &&
        ActualRight.Chunk->Hash == ExpectedRight.Chunk->Hash);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3NoiseSeams, "VoxelLand.M3.NoiseSeams", Flags)
bool FVoxelLandM3NoiseSeams::RunTest(const FString& Parameters)
{
    const EVoxelLandOpcode Families[] = {EVoxelLandOpcode::Perlin2, EVoxelLandOpcode::Perlin3,
        EVoxelLandOpcode::Fbm2, EVoxelLandOpcode::Fbm3, EVoxelLandOpcode::Value2, EVoxelLandOpcode::Value3,
        EVoxelLandOpcode::Simplex2, EVoxelLandOpcode::Simplex3};
    FVoxelLandChunkKey Left;
    Left.Coordinate = FIntVector(-1, -1, -1);
    FVoxelLandChunkKey Right = Left;
    ++Right.Coordinate.X;
    for (EVoxelLandOpcode Family : Families)
    {
        FVoxelLandProgramDraft Draft = Noise(Family);
        Draft.FormatVersion = 2;
        const FResult A = RunFilter(Draft, Left);
        const FResult B = RunFilter(Draft, Right);
        if (A.Error.Code == EVoxelLandErrorCode::BackendUnavailable || A.Error.Code == EVoxelLandErrorCode::NativeLibraryUnavailable)
        {
#if PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS
            AddError(FString::Printf(TEXT("Native noise family %d could not initialize: %s"), int32(Family), *A.Error.Message));
#else
            AddWarning(FString::Printf(TEXT("Native noise family %d unavailable; seams unverified"), int32(Family)));
#endif
            continue;
        }
        TestTrue(FString::Printf(TEXT("Noise family %d has matching negative-coordinate overlaps"), int32(Family)),
            !A.Error && !B.Error && A.Chunk && B.Chunk && MatchOverlap(*A.Chunk, *B.Chunk, 0));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM3FilterCancellation, "VoxelLand.M3.FilterCancellation", Flags)
bool FVoxelLandM3FilterCancellation::RunTest(const FString& Parameters)
{
    const bool bCancelled = Async(EAsyncExecution::ThreadPool, [] {
        FVoxelLandProgramDraft Draft = FilterSource(true, 1.f);
        TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
        if (FVoxelLandProgram::Validate(Draft, Program)) { return false; }
        FVoxelLandGeneratorSettings Settings;
        Settings.Program = Program;
        TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
        if (FVoxelLandGenerator::Create(Settings, Generator)) { return false; }
        TUniquePtr<FVoxelLandWorkerContext> Worker;
        if (Generator->CreateWorker(Worker)) { return false; }
        TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
        int32 Calls = 0;
        const FVoxelLandError Error = Generator->GenerateChunkCancellable(FVoxelLandChunkKey(), *Worker, Chunk,
            [&Calls] { return ++Calls >= 4; });
        return Error.Code == EVoxelLandErrorCode::Cancelled && !Chunk;
    }).Get();
    TestTrue(TEXT("Mid-generation cancellation discards partial chunk"), bCancelled);
    return true;
}
#endif
