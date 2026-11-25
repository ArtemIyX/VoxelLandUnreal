#include "VoxelLandFieldEvaluationFixtures.h"
#include "Hash/xxhash.h"

namespace VoxelLandM1Fixtures
{
FVoxelLandInstruction Op(EVoxelLandOpcode Code, uint16 Dst, uint16 A, uint16 B, uint16 C, uint32 Imm)
{
	FVoxelLandInstruction I;
	I.Op = Code;
	I.Dst = Dst;
	I.A = A;
	I.B = B;
	I.C = C;
	I.Imm = Imm;
	return I;
}

FVoxelLandProgramDraft Plane()
{
	FVoxelLandProgramDraft P;
	P.RegisterCount = 3;
	P.Constants = {500.0f};
	P.Instructions = {Op(EVoxelLandOpcode::LoadZ, 0), Op(EVoxelLandOpcode::LoadConstant, 1),
		Op(EVoxelLandOpcode::Subtract, 2, 0, 1), Op(EVoxelLandOpcode::StoreDensity, 0, 2)};
	return P;
}

FVoxelLandProgramDraft Shapes()
{
	FVoxelLandProgramDraft P;
	P.RegisterCount = 8;
	// Sphere radius 250; box half extents 150, 200, 250; plane normal +Z at 0.
	P.Constants = {250.f, 150.f, 200.f, 250.f, 0.f, 0.f, 1.f, 0.f};
	P.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::LoadY, 1), Op(EVoxelLandOpcode::LoadZ, 2),
		Op(EVoxelLandOpcode::Sphere, 3, 0, 1, 2, 0), Op(EVoxelLandOpcode::Box, 4, 0, 1, 2, 1),
		Op(EVoxelLandOpcode::Union, 5, 3, 4), Op(EVoxelLandOpcode::Plane, 6, 0, 1, 2, 4),
		Op(EVoxelLandOpcode::Intersect, 7, 5, 6), Op(EVoxelLandOpcode::StoreDensity, 0, 7)};
	return P;
}

FVoxelLandProgramDraft Noise(EVoxelLandOpcode NoiseOp)
{
	FVoxelLandProgramDraft P;
	P.RegisterCount = 5;
	P.Constants = {0.002f, 3.f, 0.5f, 2.f, 100.f};
	FVoxelLandSeedSlot Seed;
	Seed.NodeId = FGuid(0x12345678, 0x23456789, 0x34567890, 0x45678901);
	Seed.Salt = 17;
	P.Seeds.Add(Seed);
	FVoxelLandNoiseConfig Config;
	Config.SeedSlot = 0;
	Config.Frequency.Index = 0;
	Config.Octaves.Index = 1;
	Config.Gain.Index = 2;
	Config.Lacunarity.Index = 3;
	P.Noise.Add(Config);
	P.Instructions = {Op(EVoxelLandOpcode::LoadX, 0), Op(EVoxelLandOpcode::LoadY, 1), Op(EVoxelLandOpcode::LoadZ, 2),
		Op(NoiseOp, 3, 0, 1, 2, 0), Op(EVoxelLandOpcode::LoadConstant, 4, 0, 0, 0, 4),
		Op(EVoxelLandOpcode::Multiply, 3, 3, 4), Op(EVoxelLandOpcode::Subtract, 3, 2, 3),
		Op(EVoxelLandOpcode::StoreDensity, 0, 3)};
	return P;
}

FResult GenerateShared(TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator, FVoxelLandChunkKey Key)
{
	FResult R;
	R.bOffGameThread = !IsInGameThread();
	R.Generator = Generator;
	TUniquePtr<FVoxelLandWorkerContext> Worker;
	R.Error = Generator->CreateWorker(Worker);
	if (R.Error || !Worker)
	{
		return R;
	}
	R.VM = Worker->GetVMBackend();
	R.NoiseBackend = Worker->GetNoiseBackend();
	R.Error = Generator->GenerateChunk(Key, *Worker, R.Chunk);
	return R; // Worker and native nodes are destroyed here, on their owning thread.
}

FResult Generate(FVoxelLandProgramDraft Draft, FVoxelLandZoneProfile Profile, FVoxelLandChunkKey Key,
	FVoxelLandBackendOptions Options, uint64 Seed)
{
	FResult R;
	R.bOffGameThread = !IsInGameThread();
	TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
	R.Error = FVoxelLandProgram::Validate(Draft, Program);
	if (R.Error)
	{
		return R;
	}
	R.Error = FVoxelLandGenerator::Create({Program, Seed, Profile, Options}, R.Generator);
	if (R.Error)
	{
		return R;
	}
	return GenerateShared(R.Generator, Key);
}

TArray<int16> PlaneReference(const FVoxelLandChunkKey& Key, int32 VoxelSizeCm)
{
	TArray<int16> Result;
	Result.Reserve(FVoxelLandChunkData::SampleCount);
	for (int32 ZIndex = 0; ZIndex < 35; ++ZIndex)
	{
		const int64 HeightVoxels = int64(Key.Coordinate.Z) * 32 + ZIndex - 1 - 500 / VoxelSizeCm;
		const int16 Value = static_cast<int16>(FMath::Clamp<int64>(HeightVoxels * 1024, -32768, 32767));
		for (int32 YIndex = 0; YIndex < 35; ++YIndex)
		{
			for (int32 XIndex = 0; XIndex < 35; ++XIndex)
			{
				Result.Add(Value);
			}
		}
	}
	return Result;
}

uint64 ReferenceHash(const TArray<int16>& Density)
{
	TArray<uint8> Bytes;
	Bytes.Reserve(Density.Num() * 2);
	for (int16 Value : Density)
	{
		const uint16 Bits = static_cast<uint16>(Value);
		Bytes.Add(static_cast<uint8>(Bits));
		Bytes.Add(static_cast<uint8>(Bits >> 8));
	}
	return FXxHash64::HashBuffer(Bytes.GetData(), Bytes.Num()).Hash;
}

FString FirstDifference(const TArray<int16>& Expected, const TArray<int16>& Actual, const FVoxelLandChunkKey& Key)
{
	if (Expected.Num() != Actual.Num())
	{
		return FString::Printf(TEXT("size expected=%d actual=%d"), Expected.Num(), Actual.Num());
	}
	for (int32 SampleIndex = 0; SampleIndex < Expected.Num(); ++SampleIndex)
	{
		if (Expected[SampleIndex] != Actual[SampleIndex])
		{
			const int32 X = SampleIndex % 35, Y = (SampleIndex / 35) % 35, Z = SampleIndex / (35 * 35);
			return FString::Printf(TEXT("lattice=(%lld,%lld,%lld), expected=%d actual=%d"),
				int64(Key.Coordinate.X) * 32 + X - 1, int64(Key.Coordinate.Y) * 32 + Y - 1,
				int64(Key.Coordinate.Z) * 32 + Z - 1, int32(Expected[SampleIndex]), int32(Actual[SampleIndex]));
		}
	}
	return TEXT("none");
}
}
