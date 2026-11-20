#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandBackendOptions.h"
#include "Data/Structs/VoxelLandError.h"
#include "Data/Structs/VoxelLandProgramDraft.h"

class FVoxelLandGenerator;
namespace VoxelLandNative
{
class FWorker;
}

struct FVoxelLandWorkerContextImpl
{
	FVoxelLandWorkerContextImpl();
	~FVoxelLandWorkerContextImpl();
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
	int32 ExecuteSimd(EVoxelLandOpcode Op, int32 N, const float* A, const float* B, float* Dest) const;
	FVoxelLandError ExecuteScalar(const FVoxelLandInstruction& I, const FVoxelLandProgramDraft& D,
		const TArray<float>& FloatParameters, int32 Start, int32 N, const float* A, const float* B, const float* C,
		float* Dest);
	FVoxelLandError ExecuteInstruction(const FVoxelLandInstruction& I, const FVoxelLandProgramDraft& D,
		const TArray<float>& FloatParameters, const TArray<int32>& Seeds, int32 N);
	FVoxelLandError QuantizeOutput(int32 N, float VoxelSizeCm, TArray<int16>& OutDensity);
};
