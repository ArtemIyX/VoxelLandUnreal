#if WITH_DEV_AUTOMATION_TESTS

#include "VoxelLandM1Fixtures.h"
#include "Async/Async.h"
#include "HAL/PlatformProperties.h"
#include "Misc/AutomationTest.h"
#include <limits>

using namespace VoxelLandM1Fixtures;

namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

FResult Run(FVoxelLandProgramDraft Draft, FVoxelLandZoneProfile Profile, FVoxelLandChunkKey Key,
    FVoxelLandBackendOptions Options = {}, uint64 Seed = 0)
{
    return Async(EAsyncExecution::ThreadPool, [Draft, Profile, Key, Options, Seed]() {
        return Generate(Draft, Profile, Key, Options, Seed);
    }).Get();
}

FString Describe(const FResult& Result, const TCHAR* Fixture, uint64 Seed, float Size, FVoxelLandChunkKey Key)
{
    return FString::Printf(TEXT("%s platform=%s seed=%llu voxel=%g key=(%d,%d,%d) fingerprint=%llu vm=%d noise=%d: %s"),
        Fixture, ANSI_TO_TCHAR(FPlatformProperties::PlatformName()), Seed, Size, Key.Coordinate.X, Key.Coordinate.Y,
        Key.Coordinate.Z, Result.Generator ? Result.Generator->GetProgram().GetFingerprint() : 0ull,
        int32(Result.VM), int32(Result.NoiseBackend), *Result.Error.Message);
}

struct FSampleResult
{
    FVoxelLandError Error;
    bool bOffGameThread = false;
    EVoxelLandVMBackend VM = EVoxelLandVMBackend::Auto;
    EVoxelLandNoiseBackend NoiseBackend = EVoxelLandNoiseBackend::Auto;
    TArray<int16> Values;
};

FSampleResult Sample(FVoxelLandProgramDraft Draft, FVoxelLandBackendOptions Options, uint64 Seed, int32 Count)
{
    FSampleResult R; R.bOffGameThread = !IsInGameThread();
    TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
    R.Error = FVoxelLandProgram::Validate(Draft, Program); if (R.Error) return R;
    TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
    R.Error = FVoxelLandGenerator::Create(Program, Seed, FVoxelLandZoneProfile(), Options, Generator); if (R.Error) return R;
    TUniquePtr<FVoxelLandWorkerContext> Worker;
    R.Error = Generator->CreateWorker(Worker); if (R.Error) return R;
    R.VM = Worker->GetVMBackend(); R.NoiseBackend = Worker->GetNoiseBackend();
    TArray<FVector3f> Positions; Positions.Reserve(Count);
    for (int32 I = 0; I < Count; ++I)
        Positions.Emplace(float(I - 129) * 13.25f, float((I * 31) % 257 - 128) * 7.f, float((I * 11) % 123 - 61) * 5.f);
    R.Error = Generator->EvaluateSamples(Positions, *Worker, R.Values);
    return R;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Quantization, "VoxelLand.M1.Quantization", Flags)
bool FVoxelLandM1Quantization::RunTest(const FString& Parameters)
{
    struct FCase { float Value; int16 Expected; };
    const FCase Cases[] = {{0.f, 0}, {0.5f, 1}, {-0.5f, -1}, {1.5f, 2}, {-1.5f, -2},
        {32767.f, 32767}, {32768.f, 32767}, {-32768.f, -32768}, {-32769.f, -32768}};
    for (const FCase& C : Cases)
    {
        int16 Out = 0;
        const FVoxelLandError E = VoxelLandDeterminism::Quantize(C.Value, 1024.f, Out);
        TestFalse(TEXT("finite quantization succeeds"), bool(E));
        TestEqual(FString::Printf(TEXT("round/saturate %g"), C.Value), int32(Out), int32(C.Expected));
    }
    int16 Out = 0;
    TestTrue(TEXT("NaN rejected"), bool(VoxelLandDeterminism::Quantize(std::numeric_limits<float>::quiet_NaN(), 100.f, Out)));
    TestTrue(TEXT("infinity rejected"), bool(VoxelLandDeterminism::Quantize(std::numeric_limits<float>::infinity(), 100.f, Out)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1PlaneGoldens, "VoxelLand.M1.AnalyticPlaneAndPadding", Flags)
bool FVoxelLandM1PlaneGoldens::RunTest(const FString& Parameters)
{
    struct FGolden { int32 Size; FIntVector Key; };
    const FGolden Cases[] = {{100, FIntVector(0, 0, 0)}, {50, FIntVector(-1, 0, -1)},
        {100, FIntVector(-2, -1, 1)}, {50, FIntVector(1, -1, 0)}};
    for (const FGolden& C : Cases)
    {
        FVoxelLandZoneProfile Profile; Profile.VoxelSizeCm = float(C.Size);
        FVoxelLandChunkKey Key; Key.Coordinate = C.Key;
        const FResult R = Run(Plane(), Profile, Key);
        TestTrue(TEXT("generation ran on a worker"), R.bOffGameThread);
        if (R.Error || !R.Chunk) { AddError(Describe(R, TEXT("plane"), 0, Profile.VoxelSizeCm, Key)); continue; }
        const TArray<int16> Expected = PlaneReference(Key, C.Size);
        const uint64 ExpectedHash = ReferenceHash(Expected);
        if (Expected != R.Chunk->Density || ExpectedHash != R.Chunk->Hash)
            AddError(FString::Printf(TEXT("%s expected hash=%llu actual hash=%llu first=%s"),
                *Describe(R, TEXT("plane"), 0, Profile.VoxelSizeCm, Key), ExpectedHash, R.Chunk->Hash,
                *FirstDifference(Expected, R.Chunk->Density, Key)));
        TestEqual(TEXT("chunk sample count"), R.Chunk->Density.Num(), FVoxelLandChunkData::SampleCount);
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            FVoxelLandChunkKey Adjacent = Key;
            if (Axis == 0) ++Adjacent.Coordinate.X; else if (Axis == 1) ++Adjacent.Coordinate.Y; else ++Adjacent.Coordinate.Z;
            const FResult Other = Run(Plane(), Profile, Adjacent);
            if (Other.Error || !Other.Chunk) { AddError(Describe(Other, TEXT("adjacent"), 0, Profile.VoxelSizeCm, Adjacent)); continue; }
            bool bMatch = true;
            for (int32 A = 0; A < 35 && bMatch; ++A) for (int32 B = 0; B < 35 && bMatch; ++B) for (int32 P = 0; P < 3; ++P)
            {
                int32 LX = A, LY = B, LZ = 32 + P, RX = A, RY = B, RZ = P;
                if (Axis == 0) { LX = 32 + P; LY = A; LZ = B; RX = P; RY = A; RZ = B; }
                if (Axis == 1) { LX = A; LY = 32 + P; LZ = B; RX = A; RY = P; RZ = B; }
                if (R.Chunk->Density[LX + 35 * (LY + 35 * LZ)] != Other.Chunk->Density[RX + 35 * (RY + 35 * RZ)]) { bMatch = false; break; }
            }
            TestTrue(FString::Printf(TEXT("overlap axis %d key %s voxel %d"), Axis, *Key.Coordinate.ToString(), C.Size), bMatch);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Validation, "VoxelLand.M1.Validation", Flags)
bool FVoxelLandM1Validation::RunTest(const FString& Parameters)
{
    TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> P;
    if (IsInGameThread())
    {
        TestEqual(TEXT("game-thread validation rejected"), int32(FVoxelLandProgram::Validate(Plane(), P).Code), int32(EVoxelLandErrorCode::GameThreadNotAllowed));
        const auto ValidProgram = Async(EAsyncExecution::ThreadPool, []() {
            TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Result;
            FVoxelLandProgram::Validate(Plane(), Result);
            return Result;
        }).Get();
        TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator;
        TestEqual(TEXT("game-thread generator setup rejected"),
            int32(FVoxelLandGenerator::Create(ValidProgram, 0, {}, {}, Generator).Code), int32(EVoxelLandErrorCode::GameThreadNotAllowed));
    }
    const auto Validate = [](FVoxelLandProgramDraft Draft) {
        return Async(EAsyncExecution::ThreadPool, [Draft]() {
            TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
            return FVoxelLandProgram::Validate(Draft, Program);
        }).Get();
    };
    FVoxelLandProgramDraft Bad = Plane(); Bad.FormatVersion = 999;
    TestEqual(TEXT("version rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
    Bad = Plane(); Bad.Instructions[0].Op = static_cast<EVoxelLandOpcode>(65535);
    TestEqual(TEXT("opcode rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
    Bad = Plane(); Bad.Instructions[2].A = 2;
    TestEqual(TEXT("uninitialized register rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
    Bad = Plane(); Bad.Instructions[2].B = 300;
    TestEqual(TEXT("register bounds rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
    Bad = Plane(); Bad.Constants[0] = std::numeric_limits<float>::infinity();
    TestEqual(TEXT("non-finite constant rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
    Bad = Plane(); Bad.Instructions.Pop();
    TestEqual(TEXT("missing store rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
    Bad = Plane(); Bad.Instructions.Add(Op(EVoxelLandOpcode::StoreDensity, 0, 2));
    TestEqual(TEXT("duplicate store rejected"), int32(Validate(Bad).Code), int32(EVoxelLandErrorCode::InvalidProgram));
    FVoxelLandZoneProfile Profile; Profile.VoxelSizeCm = 0.f;
    TestEqual(TEXT("zero profile rejected"), int32(Run(Plane(), Profile, {}).Error.Code), int32(EVoxelLandErrorCode::InvalidProfile));
    Profile.VoxelSizeCm = 100.f; Profile.FloatOverrides.Add(TEXT("unknown"), 1.f);
    TestEqual(TEXT("unknown override rejected"), int32(Run(Plane(), Profile, {}).Error.Code), int32(EVoxelLandErrorCode::InvalidParameter));
    Profile.FloatOverrides.Empty(); FVoxelLandChunkKey Key; Key.LOD = 1;
    TestEqual(TEXT("LOD rejected"), int32(Run(Plane(), Profile, Key).Error.Code), int32(EVoxelLandErrorCode::UnsupportedLod));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Backends, "VoxelLand.M1.SimdAndNativeParity", Flags)
bool FVoxelLandM1Backends::RunTest(const FString& Parameters)
{
    const EVoxelLandOpcode Ops[] = {EVoxelLandOpcode::Perlin2, EVoxelLandOpcode::Perlin3, EVoxelLandOpcode::Fbm2, EVoxelLandOpcode::Fbm3};
    const int32 Counts[] = {1, 2, 3, 256};
    const uint64 Seeds[] = {0ull, 0xffffffffffffffffull, 0x8000000000000000ull};
    for (EVoxelLandOpcode OpCode : Ops) for (uint64 Seed : Seeds) for (int32 Count : Counts)
    {
        FVoxelLandBackendOptions Scalar; Scalar.VM = EVoxelLandVMBackend::Scalar;
        FVoxelLandBackendOptions Auto;
        const FSampleResult A = Async(EAsyncExecution::ThreadPool, [OpCode, Scalar, Seed, Count]() { return Sample(Noise(OpCode), Scalar, Seed, Count); }).Get();
        const FSampleResult B = Async(EAsyncExecution::ThreadPool, [OpCode, Auto, Seed, Count]() { return Sample(Noise(OpCode), Auto, Seed, Count); }).Get();
        const FString Context = FString::Printf(TEXT("platform=%s opcode=%d seed=%llu count=%d scalar-noise=%d auto-noise=%d"),
            ANSI_TO_TCHAR(FPlatformProperties::PlatformName()), int32(OpCode), Seed, Count, int32(A.NoiseBackend), int32(B.NoiseBackend));
        if (A.Error || B.Error) { AddError(FString::Printf(TEXT("%s: scalar=%s auto=%s"), *Context, *A.Error.Message, *B.Error.Message)); continue; }
        TestTrue(Context + TEXT(" worker thread"), A.bOffGameThread && B.bOffGameThread);
        TestEqual(Context + TEXT(" scalar selected"), int32(A.VM), int32(EVoxelLandVMBackend::Scalar));
        if (A.Values != B.Values)
        {
            for (int32 I = 0; I < FMath::Min(A.Values.Num(), B.Values.Num()); ++I)
                if (A.Values[I] != B.Values[I]) { AddError(FString::Printf(TEXT("%s sample=%d expected=%d actual=%d"), *Context, I, A.Values[I], B.Values[I])); break; }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Arithmetic, "VoxelLand.M1.AnalyticArithmetic", Flags)
bool FVoxelLandM1Arithmetic::RunTest(const FString& Parameters)
{
    FVoxelLandProgramDraft P; P.RegisterCount = 8; P.Constants = {20.f, 2.f, 30.f};
    P.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::Abs, 1, 0),
        Op(EVoxelLandOpcode::LoadConstant, 2, 0, 0, 0, 0), Op(EVoxelLandOpcode::Add, 3, 1, 2),
        Op(EVoxelLandOpcode::LoadConstant, 4, 0, 0, 0, 1), Op(EVoxelLandOpcode::Divide, 3, 3, 4),
        Op(EVoxelLandOpcode::LoadConstant, 5, 0, 0, 0, 2), Op(EVoxelLandOpcode::Subtract, 3, 3, 5),
        Op(EVoxelLandOpcode::Negate, 3, 3), Op(EVoxelLandOpcode::Min, 6, 3, 0),
        Op(EVoxelLandOpcode::Max, 7, 3, 0), Op(EVoxelLandOpcode::SubtractShape, 6, 6, 7),
        Op(EVoxelLandOpcode::StoreDensity, 0, 6)};
    for (int32 Count : {1, 2, 3, 256})
    {
        FVoxelLandBackendOptions Scalar; Scalar.VM = EVoxelLandVMBackend::Scalar;
        for (FVoxelLandBackendOptions Options : {Scalar, FVoxelLandBackendOptions()})
        {
            const FSampleResult R = Async(EAsyncExecution::ThreadPool, [P, Options, Count]() { return Sample(P, Options, 0, Count); }).Get();
            if (R.Error) { AddError(FString::Printf(TEXT("arithmetic count=%d: %s"), Count, *R.Error.Message)); continue; }
            TestEqual(TEXT("sample output count"), R.Values.Num(), Count);
            for (int32 I = 0; I < R.Values.Num(); ++I)
            {
                const float X = float(I - 129) * 13.25f;
                const float A = -((FMath::Abs(X) + 20.f) / 2.f - 30.f);
                const double Scaled = double(FMath::Max(FMath::Min(A, X), -FMath::Max(A, X))) * 1024.0 / 100.0;
                const double Rounded = Scaled >= 0 ? FMath::FloorToDouble(Scaled + 0.5) : FMath::CeilToDouble(Scaled - 0.5);
                const int32 Expected = int32(FMath::Clamp(Rounded, -32768.0, 32767.0));
                if (R.Values[I] != Expected)
                {
                    AddError(FString::Printf(TEXT("arithmetic backend=%d count=%d position=%g expected=%d actual=%d"), int32(R.VM), Count, X, Expected, int32(R.Values[I])));
                    break;
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Shapes, "VoxelLand.M1.AnalyticShapes", Flags)
bool FVoxelLandM1Shapes::RunTest(const FString& Parameters)
{
    const FResult R = Run(Shapes(), {}, {});
    if (R.Error || !R.Chunk) { AddError(Describe(R, TEXT("shape union/intersection"), 0, 100.f, {})); return false; }
    const auto At = [&R](int32 X, int32 Y, int32 Z) { return R.Chunk->Density[X + 35 * (Y + 35 * Z)]; };
    TestEqual(TEXT("shape at origin intersects plane"), int32(At(1, 1, 1)), 0);
    TestEqual(TEXT("interior at z=-100"), int32(At(1, 1, 0)), -1024);
    TestEqual(TEXT("plane cuts shape at z=100"), int32(At(1, 1, 2)), 1024);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Parameters, "VoxelLand.M1.ParametersAndSnapshots", Flags)
bool FVoxelLandM1Parameters::RunTest(const FString& Parameters)
{
    FVoxelLandProgramDraft Draft = Plane();
    FVoxelLandParameterSlot Height; Height.Name = TEXT("Height"); Height.FloatDefault = 500.f;
    Draft.Parameters.Add(Height);
    Draft.Instructions[1] = Op(EVoxelLandOpcode::LoadFloatParameter, 1, 0, 0, 0, 0);
    FVoxelLandZoneProfile Profile; Profile.FloatOverrides.Add(TEXT("HEIGHT"), 100.f);
    const FResult R = Run(Draft, Profile, {});
    if (R.Error || !R.Chunk || !R.Generator) { AddError(Describe(R, TEXT("parameter"), 0, 100.f, {})); return false; }
    TestEqual(TEXT("float override reaches surface"), int32(R.Chunk->Density[1 + 35 * (1 + 35 * 2)]), 0);
    TestEqual(TEXT("snapshot retains profile size"), R.Generator->GetProfile().VoxelSizeCm, 100.f);
    Profile.VoxelSizeCm = 50.f; Profile.FloatOverrides[TEXT("HEIGHT")] = 200.f;
    Draft.Parameters[0].FloatDefault = 999.f;
    TestEqual(TEXT("snapshot unaffected by caller mutation"), R.Generator->GetProfile().VoxelSizeCm, 100.f);
    TestEqual(TEXT("program unaffected by draft mutation"), R.Generator->GetProgram().GetData().Parameters[0].FloatDefault, 500.f);

    FVoxelLandZoneProfile Wrong; Wrong.IntOverrides.Add(TEXT("height"), 2);
    TestEqual(TEXT("wrong override type rejected"), int32(Run(Draft, Wrong, {}).Error.Code), int32(EVoxelLandErrorCode::InvalidParameter));
    Wrong.FloatOverrides.Add(TEXT("HEIGHT"), 2.f);
    TestEqual(TEXT("both override maps rejected"), int32(Run(Draft, Wrong, {}).Error.Code), int32(EVoxelLandErrorCode::InvalidParameter));
    Wrong.IntOverrides.Empty(); Wrong.FloatOverrides[TEXT("HEIGHT")] = std::numeric_limits<float>::infinity();
    TestEqual(TEXT("non-finite override rejected"), int32(Run(Draft, Wrong, {}).Error.Code), int32(EVoxelLandErrorCode::InvalidParameter));

    FVoxelLandProgramDraft NoiseDraft = Noise(EVoxelLandOpcode::Fbm3);
    FVoxelLandParameterSlot Octaves; Octaves.Name = TEXT("Octaves"); Octaves.Type = EVoxelLandParameterType::Int32; Octaves.IntDefault = 1;
    NoiseDraft.Parameters.Add(Octaves); NoiseDraft.Noise[0].Octaves.bParameter = true; NoiseDraft.Noise[0].Octaves.Index = 0;
    FVoxelLandZoneProfile One, Four; Four.IntOverrides.Add(TEXT("octaves"), 4);
    const FResult A = Run(NoiseDraft, One, {}, {}, 123);
    const FResult B = Run(NoiseDraft, Four, {}, {}, 123);
    if (A.Error || B.Error || !A.Chunk || !B.Chunk) AddError(FString::Printf(TEXT("octave override setup: %s; %s"), *A.Error.Message, *B.Error.Message));
    else TestTrue(TEXT("int32 octave override changes fBm"), A.Chunk->Density != B.Chunk->Density);
    Four.IntOverrides[TEXT("octaves")] = 0;
    TestTrue(TEXT("invalid octave override rejected"), bool(Run(NoiseDraft, Four, {}).Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1Concurrency, "VoxelLand.M1.SeedsAndWorkers", Flags)
bool FVoxelLandM1Concurrency::RunTest(const FString& Parameters)
{
    FVoxelLandChunkKey Key; Key.Coordinate = FIntVector(-1, 2, -1);
    constexpr uint64 Seed = 0xabcdef0123456789ull;
    const FResult First = Run(Noise(EVoxelLandOpcode::Fbm3), {}, Key, {}, Seed);
    if (First.Error || !First.Chunk || !First.Generator) { AddError(Describe(First, TEXT("fbm3"), Seed, 100.f, Key)); return false; }
    TArray<TFuture<FResult>> Futures;
    for (int32 I = 0; I < 4; ++I)
    {
        const auto Shared = First.Generator;
        Futures.Add(Async(EAsyncExecution::ThreadPool, [Shared, Key]() { return GenerateShared(Shared, Key); }));
    }
    for (TFuture<FResult>& Future : Futures)
    {
        const FResult Other = Future.Get();
        if (Other.Error || !Other.Chunk) { AddError(Describe(Other, TEXT("concurrent fbm3"), Seed, 100.f, Key)); continue; }
        TestTrue(TEXT("worker ran off game thread"), Other.bOffGameThread);
        if (First.Chunk->Density != Other.Chunk->Density || First.Chunk->Hash != Other.Chunk->Hash)
            AddError(FString::Printf(TEXT("shared generator expected=%llu actual=%llu first=%s"), First.Chunk->Hash,
                Other.Chunk->Hash, *FirstDifference(First.Chunk->Density, Other.Chunk->Density, Key)));
    }
    const FIntVector Keys[] = {FIntVector(0, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, -1)};
    TArray<FResult> Serial;
    for (FIntVector Coordinate : Keys)
    {
        FVoxelLandChunkKey K; K.Coordinate = Coordinate;
        const auto Shared = First.Generator;
        Serial.Add(Async(EAsyncExecution::ThreadPool, [Shared, K]() { return GenerateShared(Shared, K); }).Get());
    }
    TArray<TFuture<FResult>> Shuffled;
    for (int32 I = 2; I >= 0; --I)
    {
        FVoxelLandChunkKey K; K.Coordinate = Keys[I];
        const auto Shared = First.Generator;
        Shuffled.Add(Async(EAsyncExecution::ThreadPool, [Shared, K]() { return GenerateShared(Shared, K); }));
    }
    for (int32 I = 0; I < Shuffled.Num(); ++I)
    {
        const FResult Other = Shuffled[I].Get(); const FResult& Expected = Serial[2 - I];
        if (Expected.Error || Other.Error || !Expected.Chunk || !Other.Chunk)
            AddError(FString::Printf(TEXT("ordered generation: %s; %s"), *Expected.Error.Message, *Other.Error.Message));
        else TestTrue(TEXT("order and concurrency preserve chunk bytes and hash"),
            Expected.Chunk->Hash == Other.Chunk->Hash && Expected.Chunk->Density == Other.Chunk->Density);
    }
    FVoxelLandProgramDraft Extended = Noise(EVoxelLandOpcode::Fbm3);
    FVoxelLandSeedSlot Unused; Unused.NodeId = FGuid(11, 22, 33, 44); Unused.Salt = 55; Extended.Seeds.Add(Unused);
    const FResult Appended = Run(Extended, {}, Key, {}, Seed);
    if (Appended.Error || !Appended.Chunk) AddError(Describe(Appended, TEXT("unused seed"), Seed, 100.f, Key));
    else TestTrue(TEXT("unrelated seed does not change existing noise"), First.Chunk->Density == Appended.Chunk->Density);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM1ScalarNative, "VoxelLand.M1.NativeScalarRequired", Flags)
bool FVoxelLandM1ScalarNative::RunTest(const FString& Parameters)
{
    // This is intentionally an acceptance failure if the installed native library has no scalar backend.
    const EVoxelLandOpcode Ops[] = {EVoxelLandOpcode::Perlin2, EVoxelLandOpcode::Perlin3, EVoxelLandOpcode::Fbm2, EVoxelLandOpcode::Fbm3};
    for (EVoxelLandOpcode OpCode : Ops)
    {
        FVoxelLandBackendOptions Scalar; Scalar.VM = EVoxelLandVMBackend::Scalar; Scalar.Noise = EVoxelLandNoiseBackend::Scalar;
        FVoxelLandBackendOptions SSE; SSE.VM = EVoxelLandVMBackend::Scalar; SSE.Noise = EVoxelLandNoiseBackend::SSE2;
        const FSampleResult A = Async(EAsyncExecution::ThreadPool, [OpCode, Scalar]() { return Sample(Noise(OpCode), Scalar, 0xffffffffffffffffull, 256); }).Get();
        const FSampleResult B = Async(EAsyncExecution::ThreadPool, [OpCode, SSE]() { return Sample(Noise(OpCode), SSE, 0xffffffffffffffffull, 256); }).Get();
        if (A.Error || B.Error)
        {
            AddError(FString::Printf(TEXT("platform=%s opcode=%d scalar=%s SSE2=%s; a scalar-enabled native dependency is required for this gate"),
                ANSI_TO_TCHAR(FPlatformProperties::PlatformName()), int32(OpCode), *A.Error.Message, *B.Error.Message));
            continue;
        }
        TestEqual(TEXT("native scalar selected"), int32(A.NoiseBackend), int32(EVoxelLandNoiseBackend::Scalar));
        TestEqual(TEXT("native SSE2 selected"), int32(B.NoiseBackend), int32(EVoxelLandNoiseBackend::SSE2));
        TestTrue(FString::Printf(TEXT("native quantized parity opcode=%d"), int32(OpCode)), A.Values == B.Values);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
