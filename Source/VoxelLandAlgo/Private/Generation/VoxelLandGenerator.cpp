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
{
	FVoxelLandError E;
	E.Code = Code;
	E.Message = Message;
	E.Parameter = Parameter;
	return E;
}

bool NameOf(const FString& In, FString& Out)
{
	if (In.IsEmpty())
		return false;
	Out.Reset();
	for (TCHAR C : In)
	{
		if (C >= 'A' && C <= 'Z')
			C += 'a' - 'A';
		if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_'))
			return false;
		Out.AppendChar(C);
	}
	return true;
}

FVoxelLandError NativeError(const FString& Message)
{
	FVoxelLandError E;
	E.Code = Message.StartsWith(TEXT("NativeLibraryUnavailable:")) ? EVoxelLandErrorCode::NativeLibraryUnavailable
		   : Message.StartsWith(TEXT("InvalidNoiseInput:"))        ? EVoxelLandErrorCode::NonFiniteCoordinate
																   : EVoxelLandErrorCode::BackendUnavailable;
	E.Message = Message;
	return E;
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

	FVoxelLandError InitializeNoise(const FVoxelLandProgramDraft& Program, const FVoxelLandBackendOptions& Options,
		const TArray<float>& FloatParameters, const TArray<int32>& IntParameters);
	void AllocateScratch(int32 RegisterCount);
	uint64 HashChunkDensity(const TArray<int16>& Density);

	FVoxelLandError LoadPositions(TConstArrayView<FVector3f> Positions);
	FVoxelLandError EvaluateNoise(const FVoxelLandInstruction& I, const FVoxelLandProgramDraft& D,
		const TArray<float>& FloatParameters, const TArray<int32>& Seeds, int32 N, const float* A, const float* B,
		const float* C, float* Dest);
	void ResolveOperands(const FVoxelLandInstruction& I, const float*& A, const float*& B, const float*& C);
#if VOXELLAND_HAS_VM_SIMD
	int32 ExecuteSimd(EVoxelLandOpcode Op, int32 N, const float* A, const float* B, float* Dest) const;
#endif
	FVoxelLandError ExecuteScalar(const FVoxelLandInstruction& I, const FVoxelLandProgramDraft& D,
		const TArray<float>& FloatParameters, int32 Start, int32 N, const float* A, const float* B, const float* C,
		float* Dest);
	FVoxelLandError ExecuteInstruction(const FVoxelLandInstruction& I, const FVoxelLandProgramDraft& D,
		const TArray<float>& FloatParameters, const TArray<int32>& Seeds, int32 N);
	FVoxelLandError QuantizeOutput(int32 N, float VoxelSizeCm, TArray<int16>& OutDensity);
};

FVoxelLandWorkerContext::FVoxelLandWorkerContext() = default;

FVoxelLandWorkerContext::~FVoxelLandWorkerContext()
{
	check(!Impl || Impl->Thread == FPlatformTLS::GetCurrentThreadId());
}

EVoxelLandVMBackend FVoxelLandWorkerContext::GetVMBackend() const
{
	return Impl->VM;
}
EVoxelLandNoiseBackend FVoxelLandWorkerContext::GetNoiseBackend() const
{
	return Impl->NoiseBackend;
}

FVoxelLandError FVoxelLandGenerator::Create(const FVoxelLandGeneratorSettings& Settings,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
	if (IsInGameThread())
		return Fail(EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Generator setup requires a worker thread"));
	OutGenerator.Reset();
	const TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& Program = Settings.Program;
	const FVoxelLandZoneProfile& Profile = Settings.Profile;
	const FVoxelLandBackendOptions& Options = Settings.Backends;
	const uint64 WorldSeed = Settings.WorldSeed;
	if (!Program.IsValid())
		return Fail(EVoxelLandErrorCode::InvalidProgram, TEXT("Program is null"));
	if (!FMath::IsFinite(Profile.VoxelSizeCm) || Profile.VoxelSizeCm <= 0)
		return Fail(EVoxelLandErrorCode::InvalidProfile, TEXT("Voxel size must be positive and finite"));
	TSharedPtr<FVoxelLandGenerator, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandGenerator);
	Result->Program = Program;
	Result->Profile = Profile;
	Result->Options = Options;
	const FVoxelLandProgramDraft& D = Program->GetData();
	TMap<FString, int32> Slots;
	Result->FloatParameters.SetNumZeroed(D.Parameters.Num());
	Result->IntParameters.SetNumZeroed(D.Parameters.Num());
	for (int32 ParameterIndex = 0; ParameterIndex < D.Parameters.Num(); ++ParameterIndex)
	{
		const FVoxelLandParameterSlot& P = D.Parameters[ParameterIndex];
		Slots.Add(P.Name, ParameterIndex);
		Result->FloatParameters[ParameterIndex] = P.FloatDefault;
		Result->IntParameters[ParameterIndex] = P.IntDefault;
	}
	TSet<FString> Seen;
	for (const TPair<FString, float>& Entry : Profile.FloatOverrides)
	{
		FString Name;
		if (!NameOf(Entry.Key, Name) || Seen.Contains(Name))
			return Fail(EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or duplicate override"), Entry.Key);
		Seen.Add(Name);
		const int32* Slot = Slots.Find(Name);
		if (!Slot || D.Parameters[*Slot].Type != EVoxelLandParameterType::Float || !FMath::IsFinite(Entry.Value))
			return Fail(EVoxelLandErrorCode::InvalidParameter,
				TEXT("Unknown, wrong type, or non-finite float override"), Entry.Key);
		Result->FloatParameters[*Slot] = Entry.Value;
	}
	for (const TPair<FString, int32>& Entry : Profile.IntOverrides)
	{
		FString Name;
		if (!NameOf(Entry.Key, Name) || Seen.Contains(Name))
			return Fail(EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or duplicate override"), Entry.Key);
		Seen.Add(Name);
		const int32* Slot = Slots.Find(Name);
		if (!Slot || D.Parameters[*Slot].Type != EVoxelLandParameterType::Int32)
			return Fail(EVoxelLandErrorCode::InvalidParameter, TEXT("Unknown or wrong type int override"), Entry.Key);
		Result->IntParameters[*Slot] = Entry.Value;
	}
	for (const FVoxelLandSeedSlot& Seed : D.Seeds)
	{
		const uint32 Bits = uint32(VoxelLandDeterminism::DeriveNodeSeed(WorldSeed, Seed.NodeId, Seed.Salt));
		Result->Seeds.Add(Bits <= uint32(MAX_int32) ? int32(Bits) : int32(int64(Bits) - 4294967296ll));
	}
	// Validate resolved noise inputs at setup, before constructing any native nodes.
	TFunction<float(const FVoxelLandValueSource&)> FloatValue = [&](const FVoxelLandValueSource& S) {
		return S.bParameter ? Result->FloatParameters[S.Index] : D.Constants[S.Index];
	};
	TFunction<int32(const FVoxelLandValueSource&)> IntValue = [&](const FVoxelLandValueSource& S) {
		return S.bParameter ? Result->IntParameters[S.Index] : int32(D.Constants[S.Index]);
	};
	for (const FVoxelLandNoiseConfig& N : D.Noise)
	{
		const float Frequency = FloatValue(N.Frequency), Gain = FloatValue(N.Gain),
					Lacunarity = FloatValue(N.Lacunarity);
		const int32 Octaves = IntValue(N.Octaves);
		if (!FMath::IsFinite(Frequency) || Frequency <= 0 || !FMath::IsFinite(Gain) || Gain < 0 || Gain > 1
			|| !FMath::IsFinite(Lacunarity) || Lacunarity < 1 || Octaves < 1 || Octaves > 16)
			return Fail(EVoxelLandErrorCode::InvalidParameter,
				TEXT("Noise frequency, octaves, gain or lacunarity out of range"));
	}
	OutGenerator = Result;
	return {};
}

namespace
{
FVoxelLandError SelectVMBackend(EVoxelLandVMBackend Requested, EVoxelLandVMBackend& Selected)
{
	if (Requested != EVoxelLandVMBackend::Auto && Requested != EVoxelLandVMBackend::Scalar
		&& Requested != EVoxelLandVMBackend::Simd4)
		return Fail(EVoxelLandErrorCode::BackendUnavailable, TEXT("Unknown VM backend"));
#if VOXELLAND_HAS_VM_SIMD
	Selected = Requested == EVoxelLandVMBackend::Scalar ? EVoxelLandVMBackend::Scalar : EVoxelLandVMBackend::Simd4;
#else
	if (Requested == EVoxelLandVMBackend::Simd4)
		return Fail(EVoxelLandErrorCode::BackendUnavailable, TEXT("SIMD VM is unavailable"));
	Selected = EVoxelLandVMBackend::Scalar;
#endif
	return {};
}
}

FVoxelLandError FVoxelLandWorkerContext::FImpl::InitializeNoise(const FVoxelLandProgramDraft& Program,
	const FVoxelLandBackendOptions& Options, const TArray<float>& FloatParameters, const TArray<int32>& IntParameters)
{
	bool bUsesNoise = false;
	for (const FVoxelLandInstruction& Instruction : Program.Instructions)
		bUsesNoise |= Instruction.Op == EVoxelLandOpcode::Perlin2 || Instruction.Op == EVoxelLandOpcode::Perlin3
				   || Instruction.Op == EVoxelLandOpcode::Fbm2 || Instruction.Op == EVoxelLandOpcode::Fbm3;
	if (!bUsesNoise)
		return {};

	TArray<VoxelLandNative::FConfig> Configs;
	for (const FVoxelLandNoiseConfig& NoiseConfig : Program.Noise)
	{
		for (int32 VariantIndex = 0; VariantIndex < 2; ++VariantIndex)
		{
			VoxelLandNative::FConfig Config;
			Config.bFbm = VariantIndex != 0;
			Config.Octaves = NoiseConfig.Octaves.bParameter ? IntParameters[NoiseConfig.Octaves.Index]
															: int32(Program.Constants[NoiseConfig.Octaves.Index]);
			Config.Gain = NoiseConfig.Gain.bParameter ? FloatParameters[NoiseConfig.Gain.Index]
													  : Program.Constants[NoiseConfig.Gain.Index];
			Config.Lacunarity = NoiseConfig.Lacunarity.bParameter ? FloatParameters[NoiseConfig.Lacunarity.Index]
																  : Program.Constants[NoiseConfig.Lacunarity.Index];
			Configs.Add(Config);
		}
	}

	VoxelLandNative::EBackend Backend = VoxelLandNative::EBackend::Auto;
	if (Options.Noise == EVoxelLandNoiseBackend::Scalar)
		Backend = VoxelLandNative::EBackend::Scalar;
	else if (Options.Noise == EVoxelLandNoiseBackend::SSE2)
		Backend = VoxelLandNative::EBackend::SSE2;
	else if (Options.Noise != EVoxelLandNoiseBackend::Auto)
		return Fail(EVoxelLandErrorCode::BackendUnavailable, TEXT("Unknown noise backend"));
	FString Error;
	Noise = VoxelLandNative::FWorker::Create(Backend, Configs, Error);
	if (!Noise)
		return NativeError(Error);
	NoiseBackend = Noise->GetBackend() == VoxelLandNative::EBackend::SSE2 ? EVoxelLandNoiseBackend::SSE2
																		  : EVoxelLandNoiseBackend::Scalar;
	return {};
}

void FVoxelLandWorkerContext::FImpl::AllocateScratch(int32 RegisterCount)
{
	Registers.SetNumUninitialized(RegisterCount * BatchSize);
	X.SetNumUninitialized(BatchSize);
	Y.SetNumUninitialized(BatchSize);
	Z.SetNumUninitialized(BatchSize);
	Output.SetNumUninitialized(BatchSize);
	NoiseX.SetNumUninitialized(BatchSize);
	NoiseY.SetNumUninitialized(BatchSize);
	NoiseZ.SetNumUninitialized(BatchSize);
}

FVoxelLandError FVoxelLandGenerator::CreateWorker(TUniquePtr<FVoxelLandWorkerContext>& OutWorker) const
{
	if (IsInGameThread())
		return Fail(EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Worker setup requires a worker thread"));
	OutWorker.Reset();
	EVoxelLandVMBackend SelectedVM = EVoxelLandVMBackend::Scalar;
	if (FVoxelLandError Error = SelectVMBackend(Options.VM, SelectedVM))
		return Error;

	TUniquePtr<FVoxelLandWorkerContext> Worker(new FVoxelLandWorkerContext);
	Worker->Impl = MakeUnique<FVoxelLandWorkerContext::FImpl>();
	FVoxelLandWorkerContext::FImpl& W = *Worker->Impl;
	W.Owner = AsShared();
	W.Thread = FPlatformTLS::GetCurrentThreadId();
	W.VM = SelectedVM;
	const FVoxelLandProgramDraft& ProgramData = Program->GetData();
	if (FVoxelLandError Error = W.InitializeNoise(ProgramData, Options, FloatParameters, IntParameters))
		return Error;
	W.AllocateScratch(ProgramData.RegisterCount);
	OutWorker = MoveTemp(Worker);
	return {};
}

FVoxelLandError FVoxelLandWorkerContext::FImpl::LoadPositions(TConstArrayView<FVector3f> Positions)
{
	FImpl& W = *this;
	const int32 N = Positions.Num();
	for (int32 SampleIndex = 0; SampleIndex < N; ++SampleIndex)
	{
		const FVector3f P = Positions[SampleIndex];
		if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z))
			return Fail(EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Non-finite sample coordinate"));
		W.X[SampleIndex] = P.X;
		W.Y[SampleIndex] = P.Y;
		W.Z[SampleIndex] = P.Z;
	}
	return {};
}

FVoxelLandError FVoxelLandWorkerContext::FImpl::EvaluateNoise(const FVoxelLandInstruction& I,
	const FVoxelLandProgramDraft& D, const TArray<float>& FloatParameters, const TArray<int32>& Seeds, int32 N,
	const float* A, const float* B, const float* C, float* Dest)
{
	FImpl& W = *this;
	const FVoxelLandNoiseConfig& Config = D.Noise[I.Imm];
	const float Frequency =
		Config.Frequency.bParameter ? FloatParameters[Config.Frequency.Index] : D.Constants[Config.Frequency.Index];
	const bool b3D = I.Op == EVoxelLandOpcode::Perlin3 || I.Op == EVoxelLandOpcode::Fbm3;
	const bool bFbm = I.Op == EVoxelLandOpcode::Fbm2 || I.Op == EVoxelLandOpcode::Fbm3;
	for (int32 SampleIndex = 0; SampleIndex < N; ++SampleIndex)
	{
		W.NoiseX[SampleIndex] = A[SampleIndex] * Frequency;
		W.NoiseY[SampleIndex] = B[SampleIndex] * Frequency;
		W.NoiseZ[SampleIndex] = b3D ? C[SampleIndex] * Frequency : 0.f;
	}
	FString Error;
	if (!W.Noise->Sample(int32(I.Imm) * 2 + (bFbm ? 1 : 0), b3D, Seeds[Config.SeedSlot], N, W.NoiseX.GetData(),
			W.NoiseY.GetData(), W.NoiseZ.GetData(), Dest, Error))
		return NativeError(Error);
	return {};
}

void FVoxelLandWorkerContext::FImpl::ResolveOperands(
	const FVoxelLandInstruction& I, const float*& A, const float*& B, const float*& C)
{
	FImpl& W = *this;
	// Validation checks only operands read by each opcode. Do not form pointers from unused fields.
	switch (I.Op)
	{
		case EVoxelLandOpcode::Abs:
		case EVoxelLandOpcode::Negate:
		case EVoxelLandOpcode::StoreDensity:
			A = W.Registers.GetData() + int32(I.A) * BatchSize;
			break;
		case EVoxelLandOpcode::Add:
		case EVoxelLandOpcode::Subtract:
		case EVoxelLandOpcode::Multiply:
		case EVoxelLandOpcode::Divide:
		case EVoxelLandOpcode::Min:
		case EVoxelLandOpcode::Max:
		case EVoxelLandOpcode::Union:
		case EVoxelLandOpcode::SubtractShape:
		case EVoxelLandOpcode::Intersect:
		case EVoxelLandOpcode::Perlin2:
		case EVoxelLandOpcode::Fbm2:
			A = W.Registers.GetData() + int32(I.A) * BatchSize;
			B = W.Registers.GetData() + int32(I.B) * BatchSize;
			break;
		case EVoxelLandOpcode::Plane:
		case EVoxelLandOpcode::Sphere:
		case EVoxelLandOpcode::Box:
		case EVoxelLandOpcode::Perlin3:
		case EVoxelLandOpcode::Fbm3:
			A = W.Registers.GetData() + int32(I.A) * BatchSize;
			B = W.Registers.GetData() + int32(I.B) * BatchSize;
			C = W.Registers.GetData() + int32(I.C) * BatchSize;
			break;
		default:
			break;
	}
}

#if VOXELLAND_HAS_VM_SIMD
int32 FVoxelLandWorkerContext::FImpl::ExecuteSimd(
	EVoxelLandOpcode Op, int32 N, const float* A, const float* B, float* Dest) const
{
	int32 SampleIndex = 0;
	if (VM == EVoxelLandVMBackend::Simd4
		&& (Op == EVoxelLandOpcode::Add || Op == EVoxelLandOpcode::Subtract || Op == EVoxelLandOpcode::Multiply
			|| Op == EVoxelLandOpcode::Divide))
	{
		for (; SampleIndex + 4 <= N; SampleIndex += 4)
		{
			const VectorRegister4Float VA = VectorLoad(A + SampleIndex), VB = VectorLoad(B + SampleIndex);
			VectorRegister4Float V;
			switch (Op)
			{
				case EVoxelLandOpcode::Add:
					V = VectorAdd(VA, VB);
					break;
				case EVoxelLandOpcode::Subtract:
					V = VectorSubtract(VA, VB);
					break;
				case EVoxelLandOpcode::Multiply:
					V = VectorMultiply(VA, VB);
					break;
				default:
					V = VectorDivide(VA, VB);
					break;
			}
			VectorStore(V, Dest + SampleIndex);
		}
	}
	return SampleIndex;
}
#endif

FVoxelLandError FVoxelLandWorkerContext::FImpl::ExecuteScalar(const FVoxelLandInstruction& I,
	const FVoxelLandProgramDraft& D, const TArray<float>& FloatParameters, int32 Start, int32 N, const float* A,
	const float* B, const float* C, float* Dest)
{
	FImpl& W = *this;
	int32 SampleIndex = Start;
	for (; SampleIndex < N; ++SampleIndex)
	{
		switch (I.Op)
		{
			case EVoxelLandOpcode::LoadX:
				Dest[SampleIndex] = W.X[SampleIndex];
				break;
			case EVoxelLandOpcode::LoadY:
				Dest[SampleIndex] = W.Y[SampleIndex];
				break;
			case EVoxelLandOpcode::LoadZ:
				Dest[SampleIndex] = W.Z[SampleIndex];
				break;
			case EVoxelLandOpcode::LoadConstant:
				Dest[SampleIndex] = D.Constants[I.Imm];
				break;
			case EVoxelLandOpcode::LoadFloatParameter:
				Dest[SampleIndex] = FloatParameters[I.Imm];
				break;
			case EVoxelLandOpcode::Add:
				Dest[SampleIndex] = A[SampleIndex] + B[SampleIndex];
				break;
			case EVoxelLandOpcode::Subtract:
				Dest[SampleIndex] = A[SampleIndex] - B[SampleIndex];
				break;
			case EVoxelLandOpcode::Multiply:
				Dest[SampleIndex] = A[SampleIndex] * B[SampleIndex];
				break;
			case EVoxelLandOpcode::Divide:
				Dest[SampleIndex] = A[SampleIndex] / B[SampleIndex];
				break;
			case EVoxelLandOpcode::Min:
			case EVoxelLandOpcode::Union:
				Dest[SampleIndex] = FMath::Min(A[SampleIndex], B[SampleIndex]);
				break;
			case EVoxelLandOpcode::Max:
			case EVoxelLandOpcode::Intersect:
				Dest[SampleIndex] = FMath::Max(A[SampleIndex], B[SampleIndex]);
				break;
			case EVoxelLandOpcode::SubtractShape:
				Dest[SampleIndex] = FMath::Max(A[SampleIndex], -B[SampleIndex]);
				break;
			case EVoxelLandOpcode::Abs:
				Dest[SampleIndex] = FMath::Abs(A[SampleIndex]);
				break;
			case EVoxelLandOpcode::Negate:
				Dest[SampleIndex] = -A[SampleIndex];
				break;
			case EVoxelLandOpcode::Plane:
				Dest[SampleIndex] = ((A[SampleIndex] * D.Constants[I.Imm] + B[SampleIndex] * D.Constants[I.Imm + 1])
										+ C[SampleIndex] * D.Constants[I.Imm + 2])
								  + D.Constants[I.Imm + 3];
				break;
			case EVoxelLandOpcode::Sphere:
				Dest[SampleIndex] = FMath::Sqrt((A[SampleIndex] * A[SampleIndex] + B[SampleIndex] * B[SampleIndex])
												+ C[SampleIndex] * C[SampleIndex])
								  - D.Constants[I.Imm];
				break;
			case EVoxelLandOpcode::Box:
			{
				const float DistanceX = FMath::Abs(A[SampleIndex]) - D.Constants[I.Imm],
							DistanceY = FMath::Abs(B[SampleIndex]) - D.Constants[I.Imm + 1],
							DistanceZ = FMath::Abs(C[SampleIndex]) - D.Constants[I.Imm + 2];
				const float PX = FMath::Max(DistanceX, 0.f), PY = FMath::Max(DistanceY, 0.f),
							PZ = FMath::Max(DistanceZ, 0.f);
				Dest[SampleIndex] = FMath::Sqrt((PX * PX + PY * PY) + PZ * PZ)
								  + FMath::Min(FMath::Max(DistanceX, FMath::Max(DistanceY, DistanceZ)), 0.f);
				break;
			}
			case EVoxelLandOpcode::StoreDensity:
				Dest[SampleIndex] = A[SampleIndex];
				break;
			default:
				return Fail(EVoxelLandErrorCode::InvalidProgram, TEXT("Unsupported opcode during evaluation"));
		}
	}
	return {};
}

FVoxelLandError FVoxelLandWorkerContext::FImpl::ExecuteInstruction(const FVoxelLandInstruction& I,
	const FVoxelLandProgramDraft& D, const TArray<float>& FloatParameters, const TArray<int32>& Seeds, int32 N)
{
	float* Dest =
		I.Op == EVoxelLandOpcode::StoreDensity ? Output.GetData() : Registers.GetData() + int32(I.Dst) * BatchSize;
	const float* A = nullptr;
	const float* B = nullptr;
	const float* C = nullptr;
	ResolveOperands(I, A, B, C);
	if (I.Op == EVoxelLandOpcode::Perlin2 || I.Op == EVoxelLandOpcode::Perlin3 || I.Op == EVoxelLandOpcode::Fbm2
		|| I.Op == EVoxelLandOpcode::Fbm3)
	{
		if (FVoxelLandError Error = EvaluateNoise(I, D, FloatParameters, Seeds, N, A, B, C, Dest))
			return Error;
	}
	else
	{
		int32 Start = 0;
#if VOXELLAND_HAS_VM_SIMD
		Start = ExecuteSimd(I.Op, N, A, B, Dest);
#endif
		if (FVoxelLandError Error = ExecuteScalar(I, D, FloatParameters, Start, N, A, B, C, Dest))
			return Error;
	}
	for (int32 SampleIndex = 0; SampleIndex < N; ++SampleIndex)
		if (!FMath::IsFinite(Dest[SampleIndex]))
			return Fail(EVoxelLandErrorCode::NonFiniteDensity, TEXT("Non-finite VM result"));
	return {};
}

FVoxelLandError FVoxelLandWorkerContext::FImpl::QuantizeOutput(int32 N, float VoxelSizeCm, TArray<int16>& OutDensity)
{
	FImpl& W = *this;
	W.Quantized.SetNumUninitialized(N);
	for (int32 SampleIndex = 0; SampleIndex < N; ++SampleIndex)
		if (FVoxelLandError E =
				VoxelLandDeterminism::Quantize(W.Output[SampleIndex], VoxelSizeCm, W.Quantized[SampleIndex]))
			return E;
	OutDensity = W.Quantized;
	return {};
}

FVoxelLandError FVoxelLandGenerator::EvaluateSamples(
	TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity) const
{
	if (IsInGameThread())
		return Fail(EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Evaluation requires a worker thread"));
	if (!Worker.Impl || Worker.Impl->Owner.Get() != this || Worker.Impl->Thread != FPlatformTLS::GetCurrentThreadId())
		return Fail(EVoxelLandErrorCode::InvalidContext, TEXT("Worker belongs to a different generator or thread"));
	if (Positions.Num() < 1 || Positions.Num() > BatchSize)
		return Fail(EVoxelLandErrorCode::InvalidContext, TEXT("A batch must contain 1..256 samples"));

	FVoxelLandWorkerContext::FImpl& W = *Worker.Impl;
	if (FVoxelLandError Error = W.LoadPositions(Positions))
		return Error;
	const FVoxelLandProgramDraft& D = Program->GetData();
	for (const FVoxelLandInstruction& I : D.Instructions)
		if (FVoxelLandError Error = W.ExecuteInstruction(I, D, FloatParameters, Seeds, Positions.Num()))
			return Error;
	return W.QuantizeOutput(Positions.Num(), Profile.VoxelSizeCm, OutDensity);
}

namespace
{
FVoxelLandError FillChunkPositions(
	const FVoxelLandChunkKey& Key, float VoxelSizeCm, int32 Start, TArrayView<FVector3f> Positions)
{
	for (int32 SampleIndex = 0; SampleIndex < Positions.Num(); ++SampleIndex)
	{
		const int32 Index = Start + SampleIndex, X = Index % 35, Y = (Index / 35) % 35, Z = Index / (35 * 35);
		const double PX = double(int64(Key.Coordinate.X) * 32 + X - 1) * double(VoxelSizeCm);
		const double PY = double(int64(Key.Coordinate.Y) * 32 + Y - 1) * double(VoxelSizeCm);
		const double PZ = double(int64(Key.Coordinate.Z) * 32 + Z - 1) * double(VoxelSizeCm);
		if (FMath::Abs(PX) > double(FLT_MAX) || FMath::Abs(PY) > double(FLT_MAX) || FMath::Abs(PZ) > double(FLT_MAX))
			return Fail(EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Chunk coordinate cannot be represented"));
		Positions[SampleIndex] = FVector3f(float(PX), float(PY), float(PZ));
		if (!FMath::IsFinite(Positions[SampleIndex].X) || !FMath::IsFinite(Positions[SampleIndex].Y)
			|| !FMath::IsFinite(Positions[SampleIndex].Z))
			return Fail(EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Chunk coordinate cannot be represented"));
	}
	return {};
}
}

uint64 FVoxelLandWorkerContext::FImpl::HashChunkDensity(const TArray<int16>& Density)
{
	TArray<uint8>& Bytes = HashBytes;
	Bytes.Reset();
	Bytes.Reserve(Density.Num() * 2);
	for (int16 Value : Density)
	{
		const uint16 Bits = uint16(Value);
		Bytes.Add(uint8(Bits));
		Bytes.Add(uint8(Bits >> 8));
	}
	return FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
}

FVoxelLandError FVoxelLandGenerator::GenerateChunk(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk) const
{
	return GenerateChunkCancellable(Key, Worker, OutChunk, [] { return false; });
}

FVoxelLandError FVoxelLandGenerator::GenerateChunkCancellable(const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk, TFunctionRef<bool()> IsCancelled) const
{
	if (IsInGameThread())
		return Fail(EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Chunk generation requires a worker thread"));
	if (!Worker.Impl || Worker.Impl->Owner.Get() != this || Worker.Impl->Thread != FPlatformTLS::GetCurrentThreadId())
		return Fail(EVoxelLandErrorCode::InvalidContext, TEXT("Worker belongs to a different generator or thread"));
	OutChunk.Reset();
	if (Key.LOD != 0)
		return Fail(EVoxelLandErrorCode::UnsupportedLod, TEXT("M1 supports only LOD 0"));
	TSharedPtr<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
	Chunk->Key = Key;
	Chunk->Density.Reserve(FVoxelLandChunkData::SampleCount);
	FVector3f Positions[BatchSize];
	TArray<int16> Batch;
	Batch.Reserve(BatchSize);
	for (int32 Start = 0; Start < FVoxelLandChunkData::SampleCount; Start += BatchSize)
	{
		if (IsCancelled()) return Fail(EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled"));
		const int32 Count = FMath::Min(BatchSize, FVoxelLandChunkData::SampleCount - Start);
		if (FVoxelLandError Error =
				FillChunkPositions(Key, Profile.VoxelSizeCm, Start, MakeArrayView(Positions, Count)))
			return Error;
		if (FVoxelLandError E = EvaluateSamples(MakeArrayView(Positions, Count), Worker, Batch))
			return E;
		Chunk->Density.Append(Batch);
	}
	Chunk->Hash = Worker.Impl->HashChunkDensity(Chunk->Density);
	OutChunk = Chunk;
	return {};
}
