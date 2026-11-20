#include "Generation/VoxelLandGenerator.h"
#include "../VoxelLandNativeNoise.h"
#include "HAL/PlatformTLS.h"
#include "Math/VectorRegister.h"
#include "Hash/xxhash.h"
#include <cfloat>

#ifndef VOXELLAND_ENABLE_VM_SIMD
#define VOXELLAND_ENABLE_VM_SIMD 1
#endif
#if VOXELLAND_ENABLE_VM_SIMD && PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS && PLATFORM_ENABLE_VECTORINTRINSICS
#define VOXELLAND_HAS_VM_SIMD 1
#else
#define VOXELLAND_HAS_VM_SIMD 0
#endif

namespace
{
constexpr int32 BatchSize = 256;
FVoxelLandError Fail(EVoxelLandErrorCode Code, const TCHAR* Message, const FString& Parameter = {})
{ FVoxelLandError E; E.Code = Code; E.Message = Message; E.Parameter = Parameter; return E; }
bool NameOf(const FString& In, FString& Out)
{
    if (In.IsEmpty()) return false;
    Out.Reset(); for (TCHAR C : In) { if (C >= 'A' && C <= 'Z') C += 'a' - 'A'; if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_')) return false; Out.AppendChar(C); } return true;
}
FVoxelLandError NativeError(const FString& Message)
{
    FVoxelLandError E;
    E.Code = Message.StartsWith(TEXT("NativeLibraryUnavailable:")) ? EVoxelLandErrorCode::NativeLibraryUnavailable :
        Message.StartsWith(TEXT("InvalidNoiseInput:")) ? EVoxelLandErrorCode::NonFiniteCoordinate : EVoxelLandErrorCode::BackendUnavailable;
    E.Message = Message; return E;
}
}

struct FVoxelLandWorkerContext::FImpl
{
    TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Owner;
    uint32 Thread = 0;
    EVoxelLandVMBackend VM = EVoxelLandVMBackend::Scalar;
    EVoxelLandNoiseBackend NoiseBackend = EVoxelLandNoiseBackend::Auto;
    TUniquePtr<VoxelLandNative::FWorker> Noise;
    TArray<float> Registers, X, Y, Z, Output, NoiseX, NoiseY, NoiseZ;
    TArray<int16> Quantized;
    TArray<uint8> HashBytes;
};

FVoxelLandWorkerContext::FVoxelLandWorkerContext() = default;
FVoxelLandWorkerContext::~FVoxelLandWorkerContext() { check(!Impl || Impl->Thread == FPlatformTLS::GetCurrentThreadId()); }
EVoxelLandVMBackend FVoxelLandWorkerContext::GetVMBackend() const { return Impl->VM; }
EVoxelLandNoiseBackend FVoxelLandWorkerContext::GetNoiseBackend() const { return Impl->NoiseBackend; }

FVoxelLandError FVoxelLandGenerator::Create(TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program,
    uint64 WorldSeed, const FVoxelLandZoneProfile& Profile, FVoxelLandBackendOptions Options,
    TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
    if (IsInGameThread()) return Fail(EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Generator setup requires a worker thread"));
    OutGenerator.Reset();
    if (!Program.IsValid()) return Fail(EVoxelLandErrorCode::InvalidProgram, TEXT("Program is null"));
    if (!FMath::IsFinite(Profile.VoxelSizeCm) || Profile.VoxelSizeCm <= 0) return Fail(EVoxelLandErrorCode::InvalidProfile, TEXT("Voxel size must be positive and finite"));
    TSharedPtr<FVoxelLandGenerator, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandGenerator);
    Result->Program = Program; Result->Profile = Profile; Result->Options = Options;
    const auto& D = Program->GetData();
    TMap<FString, int32> Slots;
    Result->FloatParameters.SetNumZeroed(D.Parameters.Num()); Result->IntParameters.SetNumZeroed(D.Parameters.Num());
    for (int32 I = 0; I < D.Parameters.Num(); ++I)
    { const auto& P = D.Parameters[I]; Slots.Add(P.Name, I); Result->FloatParameters[I] = P.FloatDefault; Result->IntParameters[I] = P.IntDefault; }
    TSet<FString> Seen;
    for (const auto& Entry : Profile.FloatOverrides)
    {
        FString Name; if (!NameOf(Entry.Key, Name) || Seen.Contains(Name)) return Fail(EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or duplicate override"), Entry.Key);
        Seen.Add(Name); const int32* Slot = Slots.Find(Name);
        if (!Slot || D.Parameters[*Slot].Type != EVoxelLandParameterType::Float || !FMath::IsFinite(Entry.Value)) return Fail(EVoxelLandErrorCode::InvalidParameter, TEXT("Unknown, wrong type, or non-finite float override"), Entry.Key);
        Result->FloatParameters[*Slot] = Entry.Value;
    }
    for (const auto& Entry : Profile.IntOverrides)
    {
        FString Name; if (!NameOf(Entry.Key, Name) || Seen.Contains(Name)) return Fail(EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or duplicate override"), Entry.Key);
        Seen.Add(Name); const int32* Slot = Slots.Find(Name);
        if (!Slot || D.Parameters[*Slot].Type != EVoxelLandParameterType::Int32) return Fail(EVoxelLandErrorCode::InvalidParameter, TEXT("Unknown or wrong type int override"), Entry.Key);
        Result->IntParameters[*Slot] = Entry.Value;
    }
    for (const auto& Seed : D.Seeds)
    {
        const uint32 Bits = uint32(VoxelLandDeterminism::DeriveNodeSeed(WorldSeed, Seed.NodeId, Seed.Salt));
        Result->Seeds.Add(Bits <= uint32(MAX_int32) ? int32(Bits) : int32(int64(Bits) - 4294967296ll));
    }
    // Validate resolved noise inputs at setup, before constructing any native nodes.
    auto FloatValue = [&](const FVoxelLandValueSource& S) { return S.bParameter ? Result->FloatParameters[S.Index] : D.Constants[S.Index]; };
    auto IntValue = [&](const FVoxelLandValueSource& S) { return S.bParameter ? Result->IntParameters[S.Index] : int32(D.Constants[S.Index]); };
    for (const auto& N : D.Noise)
    {
        const float Frequency = FloatValue(N.Frequency), Gain = FloatValue(N.Gain), Lacunarity = FloatValue(N.Lacunarity);
        const int32 Octaves = IntValue(N.Octaves);
        if (!FMath::IsFinite(Frequency) || Frequency <= 0 || !FMath::IsFinite(Gain) || Gain < 0 || Gain > 1 ||
            !FMath::IsFinite(Lacunarity) || Lacunarity < 1 || Octaves < 1 || Octaves > 16)
            return Fail(EVoxelLandErrorCode::InvalidParameter, TEXT("Noise frequency, octaves, gain or lacunarity out of range"));
    }
    OutGenerator = Result; return {};
}

FVoxelLandError FVoxelLandGenerator::CreateWorker(TUniquePtr<FVoxelLandWorkerContext>& OutWorker) const
{
    if (IsInGameThread()) return Fail(EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Worker setup requires a worker thread"));
    OutWorker.Reset();
    if (Options.VM != EVoxelLandVMBackend::Auto && Options.VM != EVoxelLandVMBackend::Scalar && Options.VM != EVoxelLandVMBackend::Simd4)
        return Fail(EVoxelLandErrorCode::BackendUnavailable, TEXT("Unknown VM backend"));
#if !VOXELLAND_HAS_VM_SIMD
    if (Options.VM == EVoxelLandVMBackend::Simd4) return Fail(EVoxelLandErrorCode::BackendUnavailable, TEXT("SIMD VM is unavailable"));
#endif
    TUniquePtr<FVoxelLandWorkerContext> Worker(new FVoxelLandWorkerContext);
    Worker->Impl = MakeUnique<FVoxelLandWorkerContext::FImpl>(); auto& W = *Worker->Impl;
    W.Owner = AsShared(); W.Thread = FPlatformTLS::GetCurrentThreadId();
#if VOXELLAND_HAS_VM_SIMD
    W.VM = Options.VM == EVoxelLandVMBackend::Scalar ? EVoxelLandVMBackend::Scalar : EVoxelLandVMBackend::Simd4;
#endif
    const auto& D = Program->GetData();
    bool bUsesNoise = false;
    for (const auto& I : D.Instructions) bUsesNoise |= I.Op == EVoxelLandOpcode::Perlin2 || I.Op == EVoxelLandOpcode::Perlin3 || I.Op == EVoxelLandOpcode::Fbm2 || I.Op == EVoxelLandOpcode::Fbm3;
    if (bUsesNoise)
    {
        auto FloatValue = [&](const FVoxelLandValueSource& S) { return S.bParameter ? FloatParameters[S.Index] : D.Constants[S.Index]; };
        auto IntValue = [&](const FVoxelLandValueSource& S) { return S.bParameter ? IntParameters[S.Index] : int32(D.Constants[S.Index]); };
        TArray<VoxelLandNative::FConfig> Configs;
        for (const auto& N : D.Noise) for (int32 Fbm = 0; Fbm < 2; ++Fbm)
        { VoxelLandNative::FConfig C; C.bFbm = Fbm != 0; C.Octaves = IntValue(N.Octaves); C.Gain = FloatValue(N.Gain); C.Lacunarity = FloatValue(N.Lacunarity); Configs.Add(C); }
        VoxelLandNative::EBackend Backend = VoxelLandNative::EBackend::Auto;
        if (Options.Noise == EVoxelLandNoiseBackend::Scalar) Backend = VoxelLandNative::EBackend::Scalar;
        else if (Options.Noise == EVoxelLandNoiseBackend::SSE2) Backend = VoxelLandNative::EBackend::SSE2;
        else if (Options.Noise != EVoxelLandNoiseBackend::Auto) return Fail(EVoxelLandErrorCode::BackendUnavailable, TEXT("Unknown noise backend"));
        FString Error; W.Noise = VoxelLandNative::FWorker::Create(Backend, Configs, Error); if (!W.Noise) return NativeError(Error);
        W.NoiseBackend = W.Noise->GetBackend() == VoxelLandNative::EBackend::SSE2 ? EVoxelLandNoiseBackend::SSE2 : EVoxelLandNoiseBackend::Scalar;
    }
    W.Registers.SetNumUninitialized(D.RegisterCount * BatchSize);
    W.X.SetNumUninitialized(BatchSize); W.Y.SetNumUninitialized(BatchSize); W.Z.SetNumUninitialized(BatchSize); W.Output.SetNumUninitialized(BatchSize);
    W.NoiseX.SetNumUninitialized(BatchSize); W.NoiseY.SetNumUninitialized(BatchSize); W.NoiseZ.SetNumUninitialized(BatchSize);
    OutWorker = MoveTemp(Worker); return {};
}

FVoxelLandError FVoxelLandGenerator::EvaluateSamples(TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker,
    TArray<int16>& OutDensity) const
{
    if (IsInGameThread()) return Fail(EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Evaluation requires a worker thread"));
    if (!Worker.Impl || Worker.Impl->Owner.Get() != this || Worker.Impl->Thread != FPlatformTLS::GetCurrentThreadId())
        return Fail(EVoxelLandErrorCode::InvalidContext, TEXT("Worker belongs to a different generator or thread"));
    if (Positions.Num() < 1 || Positions.Num() > BatchSize) return Fail(EVoxelLandErrorCode::InvalidContext, TEXT("A batch must contain 1..256 samples"));
    auto& W = *Worker.Impl; const auto& D = Program->GetData(); const int32 N = Positions.Num();
    for (int32 J = 0; J < N; ++J)
    {
        const FVector3f P = Positions[J];
        if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z)) return Fail(EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Non-finite sample coordinate"));
        W.X[J] = P.X; W.Y[J] = P.Y; W.Z[J] = P.Z;
    }
    for (const FVoxelLandInstruction& I : D.Instructions)
    {
        float* Dest = I.Op == EVoxelLandOpcode::StoreDensity ? W.Output.GetData() : W.Registers.GetData() + int32(I.Dst) * BatchSize;
        // Validation checks only operands read by each opcode. Do not form pointers from unused fields.
        const auto Reg = [&](uint16 Index) { return W.Registers.GetData() + int32(Index) * BatchSize; };
        const float* A = nullptr; const float* B = nullptr; const float* C = nullptr;
        switch (I.Op)
        {
        case EVoxelLandOpcode::Abs: case EVoxelLandOpcode::Negate: case EVoxelLandOpcode::StoreDensity: A = Reg(I.A); break;
        case EVoxelLandOpcode::Add: case EVoxelLandOpcode::Subtract: case EVoxelLandOpcode::Multiply: case EVoxelLandOpcode::Divide:
        case EVoxelLandOpcode::Min: case EVoxelLandOpcode::Max: case EVoxelLandOpcode::Union: case EVoxelLandOpcode::SubtractShape: case EVoxelLandOpcode::Intersect:
        case EVoxelLandOpcode::Perlin2: case EVoxelLandOpcode::Fbm2: A = Reg(I.A); B = Reg(I.B); break;
        case EVoxelLandOpcode::Plane: case EVoxelLandOpcode::Sphere: case EVoxelLandOpcode::Box: case EVoxelLandOpcode::Perlin3: case EVoxelLandOpcode::Fbm3: A = Reg(I.A); B = Reg(I.B); C = Reg(I.C); break;
        default: break;
        }
        if (I.Op == EVoxelLandOpcode::Perlin2 || I.Op == EVoxelLandOpcode::Perlin3 || I.Op == EVoxelLandOpcode::Fbm2 || I.Op == EVoxelLandOpcode::Fbm3)
        {
            const auto& Config = D.Noise[I.Imm]; const float Frequency = Config.Frequency.bParameter ? FloatParameters[Config.Frequency.Index] : D.Constants[Config.Frequency.Index];
            const bool b3D = I.Op == EVoxelLandOpcode::Perlin3 || I.Op == EVoxelLandOpcode::Fbm3;
            const bool bFbm = I.Op == EVoxelLandOpcode::Fbm2 || I.Op == EVoxelLandOpcode::Fbm3;
            for (int32 J = 0; J < N; ++J) { W.NoiseX[J] = A[J] * Frequency; W.NoiseY[J] = B[J] * Frequency; W.NoiseZ[J] = b3D ? C[J] * Frequency : 0.f; }
            FString Error;
            if (!W.Noise->Sample(int32(I.Imm) * 2 + (bFbm ? 1 : 0), b3D, Seeds[Config.SeedSlot], N, W.NoiseX.GetData(), W.NoiseY.GetData(), W.NoiseZ.GetData(), Dest, Error)) return NativeError(Error);
        }
        else
        {
            int32 J = 0;
#if VOXELLAND_HAS_VM_SIMD
            if (W.VM == EVoxelLandVMBackend::Simd4 && (I.Op == EVoxelLandOpcode::Add || I.Op == EVoxelLandOpcode::Subtract || I.Op == EVoxelLandOpcode::Multiply || I.Op == EVoxelLandOpcode::Divide))
            {
                for (; J + 4 <= N; J += 4)
                {
                    const VectorRegister4Float VA = VectorLoad(A + J), VB = VectorLoad(B + J);
                    VectorRegister4Float V;
                    switch (I.Op)
                    {
                    case EVoxelLandOpcode::Add: V = VectorAdd(VA, VB); break;
                    case EVoxelLandOpcode::Subtract: V = VectorSubtract(VA, VB); break;
                    case EVoxelLandOpcode::Multiply: V = VectorMultiply(VA, VB); break;
                    default: V = VectorDivide(VA, VB); break;
                    }
                    VectorStore(V, Dest + J);
                }
            }
#endif
            for (; J < N; ++J)
            {
                switch (I.Op)
                {
                case EVoxelLandOpcode::LoadX: Dest[J] = W.X[J]; break;
                case EVoxelLandOpcode::LoadY: Dest[J] = W.Y[J]; break;
                case EVoxelLandOpcode::LoadZ: Dest[J] = W.Z[J]; break;
                case EVoxelLandOpcode::LoadConstant: Dest[J] = D.Constants[I.Imm]; break;
                case EVoxelLandOpcode::LoadFloatParameter: Dest[J] = FloatParameters[I.Imm]; break;
                case EVoxelLandOpcode::Add: Dest[J] = A[J] + B[J]; break;
                case EVoxelLandOpcode::Subtract: Dest[J] = A[J] - B[J]; break;
                case EVoxelLandOpcode::Multiply: Dest[J] = A[J] * B[J]; break;
                case EVoxelLandOpcode::Divide: Dest[J] = A[J] / B[J]; break;
                case EVoxelLandOpcode::Min: case EVoxelLandOpcode::Union: Dest[J] = FMath::Min(A[J], B[J]); break;
                case EVoxelLandOpcode::Max: case EVoxelLandOpcode::Intersect: Dest[J] = FMath::Max(A[J], B[J]); break;
                case EVoxelLandOpcode::SubtractShape: Dest[J] = FMath::Max(A[J], -B[J]); break;
                case EVoxelLandOpcode::Abs: Dest[J] = FMath::Abs(A[J]); break;
                case EVoxelLandOpcode::Negate: Dest[J] = -A[J]; break;
                case EVoxelLandOpcode::Plane: Dest[J] = ((A[J] * D.Constants[I.Imm] + B[J] * D.Constants[I.Imm+1]) + C[J] * D.Constants[I.Imm+2]) + D.Constants[I.Imm+3]; break;
                case EVoxelLandOpcode::Sphere: Dest[J] = FMath::Sqrt((A[J]*A[J] + B[J]*B[J]) + C[J]*C[J]) - D.Constants[I.Imm]; break;
                case EVoxelLandOpcode::Box:
                {
                    const float X = FMath::Abs(A[J]) - D.Constants[I.Imm], Y = FMath::Abs(B[J]) - D.Constants[I.Imm+1], Z = FMath::Abs(C[J]) - D.Constants[I.Imm+2];
                    const float PX = FMath::Max(X, 0.f), PY = FMath::Max(Y, 0.f), PZ = FMath::Max(Z, 0.f);
                    Dest[J] = FMath::Sqrt((PX*PX + PY*PY) + PZ*PZ) + FMath::Min(FMath::Max(X, FMath::Max(Y, Z)), 0.f); break;
                }
                case EVoxelLandOpcode::StoreDensity: Dest[J] = A[J]; break;
                default: return Fail(EVoxelLandErrorCode::InvalidProgram, TEXT("Unsupported opcode during evaluation"));
                }
            }
        }
        for (int32 J = 0; J < N; ++J) if (!FMath::IsFinite(Dest[J])) return Fail(EVoxelLandErrorCode::NonFiniteDensity, TEXT("Non-finite VM result"));
    }
    W.Quantized.SetNumUninitialized(N);
    for (int32 J = 0; J < N; ++J) if (FVoxelLandError E = VoxelLandDeterminism::Quantize(W.Output[J], Profile.VoxelSizeCm, W.Quantized[J])) return E;
    OutDensity = W.Quantized; return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateChunk(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
    TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk) const
{
    if (IsInGameThread()) return Fail(EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Chunk generation requires a worker thread"));
    if (!Worker.Impl || Worker.Impl->Owner.Get() != this || Worker.Impl->Thread != FPlatformTLS::GetCurrentThreadId()) return Fail(EVoxelLandErrorCode::InvalidContext, TEXT("Worker belongs to a different generator or thread"));
    OutChunk.Reset();
    if (Key.LOD != 0) return Fail(EVoxelLandErrorCode::UnsupportedLod, TEXT("M1 supports only LOD 0"));
    TSharedPtr<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>(); Chunk->Key = Key; Chunk->Density.Reserve(FVoxelLandChunkData::SampleCount);
    FVector3f Positions[BatchSize]; TArray<int16> Batch; Batch.Reserve(BatchSize);
    for (int32 Start = 0; Start < FVoxelLandChunkData::SampleCount; Start += BatchSize)
    {
        const int32 Count = FMath::Min(BatchSize, FVoxelLandChunkData::SampleCount - Start);
        for (int32 J = 0; J < Count; ++J)
        {
            const int32 Index = Start + J, X = Index % 35, Y = (Index / 35) % 35, Z = Index / (35 * 35);
            const double PX = double(int64(Key.Coordinate.X) * 32 + X - 1) * double(Profile.VoxelSizeCm);
            const double PY = double(int64(Key.Coordinate.Y) * 32 + Y - 1) * double(Profile.VoxelSizeCm);
            const double PZ = double(int64(Key.Coordinate.Z) * 32 + Z - 1) * double(Profile.VoxelSizeCm);
            if (FMath::Abs(PX) > double(FLT_MAX) || FMath::Abs(PY) > double(FLT_MAX) || FMath::Abs(PZ) > double(FLT_MAX)) return Fail(EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Chunk coordinate cannot be represented"));
            Positions[J] = FVector3f(float(PX), float(PY), float(PZ));
            if (!FMath::IsFinite(Positions[J].X) || !FMath::IsFinite(Positions[J].Y) || !FMath::IsFinite(Positions[J].Z)) return Fail(EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Chunk coordinate cannot be represented"));
        }
        if (FVoxelLandError E = EvaluateSamples(MakeArrayView(Positions, Count), Worker, Batch)) return E;
        Chunk->Density.Append(Batch);
    }
    auto& Bytes = Worker.Impl->HashBytes; Bytes.Reset(); Bytes.Reserve(Chunk->Density.Num() * 2);
    for (int16 Value : Chunk->Density) { const uint16 Bits = uint16(Value); Bytes.Add(uint8(Bits)); Bytes.Add(uint8(Bits >> 8)); }
    Chunk->Hash = FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash; OutChunk = Chunk; return {};
}
