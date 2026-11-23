#include "Generation/VoxelLandM5ScratchChunks.h"

#include "Data/Structs/VoxelLandM5ChunkFootprint.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"
#include "Generation/VoxelLandM5ChunkFootprint.h"
#include "Generation/VoxelLandM5FinalField.h"
#include "Generation/VoxelLandM5WindowFinalField.h"
#include "Hash/xxhash.h"

class FVoxelLandM5ScratchChunksInternals final
{
public:
	static constexpr int32 MaximumChunks = 512;
	static constexpr int64 Allowance = 4096;
	static constexpr int64 SampleWork = 8;

	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.ScratchChunks");
		Result.Message = Message;
		return Result;
	}
};

FVoxelLandM5ScratchChunks::FVoxelLandM5ScratchChunks(
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
{
}

bool FVoxelLandM5ScratchChunks::IsOwnedBy(const FVoxelLandM5Budget& Budget) const
{
	return Lease.IsValid() && Budget.OwnsLease(*Lease);
}

bool FVoxelLandM5ScratchChunks::MatchesDependencies(const FVoxelLandM5BaselineWindow& Window,
	const FVoxelLandM5CandidateResult& Candidate, const FVoxelLandM5ProtectionGrid* Protection,
	int64 WorldSeed, const FVoxelLandM5StageManifest& Manifest) const
{
	return SourceWindow.Get() == &Window && SourceCandidate.Get() == &Candidate && SourceProtection.Get() == Protection
		&& SourceWorldSeed == WorldSeed && SourceManifest.Descriptor == Manifest.Descriptor
		&& SourceManifest.Seed == Manifest.Seed && SourceManifest.Topology == Manifest.Topology
		&& SourceManifest.Geometry == Manifest.Geometry && SourceManifest.RegionalOwnership == Manifest.RegionalOwnership
		&& SourceManifest.PocketNoise == Manifest.PocketNoise && SourceManifest.SurfaceValidation == Manifest.SurfaceValidation
		&& SourceManifest.Composition == Manifest.Composition;
}

FVoxelLandM5Diagnostic FVoxelLandM5ScratchChunks::Build(const FVoxelLandM5CaveSettings& Settings,
	const FVoxelLandM5StageManifest& Manifest, int64 WorldSeed,
	TSharedRef<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window,
	TSharedRef<const FVoxelLandM5CandidateResult, ESPMode::ThreadSafe> Candidate,
	TSharedPtr<const FVoxelLandM5ProtectionGrid, ESPMode::ThreadSafe> Protection,
	const FIntVector& MinInclusive, const FIntVector& MaxExclusive,
	int32 MaxChunks, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe>& OutChunks)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5ScratchChunksInternals::Error(E::Cancelled, TEXT("Scratch chunk assembly cancelled"));
	}
	if (MaxChunks < 1 || MaxChunks > FVoxelLandM5ScratchChunksInternals::MaximumChunks
		|| Window->GetVoxelSizeCm() <= 0)
	{
		return FVoxelLandM5ScratchChunksInternals::Error(E::InvalidInput, TEXT("Invalid scratch chunk limit or spacing"));
	}
	FVoxelLandChunkKey FirstKey;
	FirstKey.Coordinate = MinInclusive;
	FVoxelLandM5ChunkFootprint First;
	if (!VoxelLand::M5::ChunkFootprint::TryBuild(FirstKey, Window->GetVoxelSizeCm(), false, false, First))
	{
		return FVoxelLandM5ScratchChunksInternals::Error(E::OutOfDomain, TEXT("Scratch chunk footprint overflow"));
	}
	const FVoxelLandM5PointCm WindowOrigin = Window->GetOriginCm();
	const int64 StoredMin[] = {First.StoredMin.X, First.StoredMin.Y, First.StoredMin.Z};
	const int64 WindowMin[] = {WindowOrigin.X, WindowOrigin.Y, WindowOrigin.Z};
	const FIntVector WindowSize = Window->GetSize();
	FIntVector Start;
	FIntVector Count;
	int64 ChunkCount = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 AxisChunks = int64(MaxExclusive[Axis]) - int64(MinInclusive[Axis]);
		int64 Samples = 0, Delta = 0, End = 0;
		if (AxisChunks < 1 || AxisChunks > MaxChunks || !TryMultiplyNonNegative(ChunkCount, AxisChunks, ChunkCount)
			|| ChunkCount > MaxChunks || !TryMultiplyNonNegative(AxisChunks, FVoxelLandChunkData::Cells, Samples)
			|| !TryAdd(Samples, 3, Samples) || Samples > MAX_int32
			|| !TrySubtract(StoredMin[Axis], WindowMin[Axis], Delta) || Delta < 0
			|| Delta % Window->GetVoxelSizeCm() != 0)
		{
			return FVoxelLandM5ScratchChunksInternals::Error(E::OutOfDomain, TEXT("Unsupported chunk rectangle or window alignment"));
		}
		const int64 AxisStart = Delta / Window->GetVoxelSizeCm();
		if (!TryAdd(AxisStart, Samples, End) || AxisStart > MAX_int32 || End > WindowSize[Axis])
		{
			return FVoxelLandM5ScratchChunksInternals::Error(E::OutOfDomain, TEXT("Scratch chunk rectangle is outside baseline window"));
		}
		Start[Axis] = int32(AxisStart);
		Count[Axis] = int32(Samples);
	}
	int64 ChunkSamples = 0, DensityBytes = 0, ChunkBytes = 0, OutputBytes = 0, Work = 0;
	if (!TryMultiplyNonNegative(ChunkCount, FVoxelLandChunkData::SampleCount, ChunkSamples)
		|| !TryMultiplyNonNegative(ChunkSamples, 2 * int64(sizeof(int16)), DensityBytes)
		|| !TryMultiplyNonNegative(ChunkCount, int64(sizeof(FVoxelLandChunkData)) + 512, ChunkBytes)
		|| !TryAdd(DensityBytes, ChunkBytes, OutputBytes)
		|| !TryAdd(OutputBytes, FVoxelLandM5ScratchChunksInternals::Allowance, OutputBytes)
		|| !TryMultiplyNonNegative(ChunkSamples, FVoxelLandM5ScratchChunksInternals::SampleWork, Work)
		|| !TryAdd(Work, ChunkCount + 1, Work))
	{
		return FVoxelLandM5ScratchChunksInternals::Error(E::ArithmeticOverflow, TEXT("Scratch chunk admission overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5ScratchChunksInternals::Error(E::BudgetExceeded, TEXT("Scratch chunk work exhausted"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> OutputLease;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> HashLease;
	const int64 HashBytes = 2 * int64(FVoxelLandChunkData::SampleCount) + FVoxelLandM5ScratchChunksInternals::Allowance;
	if (!Budget->TryReserveBytes(OutputBytes, OutputLease) || !Budget->TryReserveBytes(HashBytes, HashLease))
	{
		return FVoxelLandM5ScratchChunksInternals::Error(E::BudgetExceeded, TEXT("Scratch chunk memory exhausted"));
	}
	const FVoxelLandM5StageManifest CapturedManifest = Manifest;
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Field;
	const FVoxelLandM5Diagnostic Diagnostic = VoxelLand::M5::WindowFinalField::Build(
		Settings, CapturedManifest, WorldSeed, Window.Get(), Candidate, Protection.Get(), Start, Count, Budget, IsCancelled, Field);
	if (Diagnostic.Code != E::None)
	{
		return Diagnostic;
	}
	int64 XY = 0, FieldCount = 0;
	if (!Field || !TryMultiplyNonNegative(Count.X, Count.Y, XY) || !TryMultiplyNonNegative(XY, Count.Z, FieldCount)
		|| FieldCount != Field->GetDensity().Num())
	{
		return FVoxelLandM5ScratchChunksInternals::Error(E::Unproven, TEXT("Scratch field size mismatch"));
	}
	TSharedPtr<FVoxelLandM5ScratchChunks, ESPMode::ThreadSafe> Result =
		MakeShareable(new FVoxelLandM5ScratchChunks(MoveTemp(OutputLease)));
	Result->SourceWindow = Window;
	Result->SourceCandidate = Candidate;
	Result->SourceProtection = Protection;
	Result->SourceWorldSeed = WorldSeed;
	Result->SourceManifest = CapturedManifest;
	Result->MinInclusive = MinInclusive;
	Result->MaxExclusive = MaxExclusive;
	Result->VoxelSizeCm = Window->GetVoxelSizeCm();
	Result->Chunks.Reserve(int32(ChunkCount));
	TArray<uint8> HashInput;
	HashInput.Reserve(FVoxelLandChunkData::SampleCount * 2);
	const TConstArrayView<int16> Density = Field->GetDensity();
	for (int64 CZ = MinInclusive.Z; CZ < int64(MaxExclusive.Z); ++CZ)
	{
		for (int64 CY = MinInclusive.Y; CY < int64(MaxExclusive.Y); ++CY)
		{
			for (int64 CX = MinInclusive.X; CX < int64(MaxExclusive.X); ++CX)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5ScratchChunksInternals::Error(E::Cancelled, TEXT("Scratch chunk assembly cancelled"));
				}
				FVoxelLandChunkData& Chunk = Result->Chunks.AddDefaulted_GetRef();
				Chunk.Key.Coordinate = FIntVector(int32(CX), int32(CY), int32(CZ));
				Chunk.Key.LOD = 0;
				Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
				HashInput.Reset();
				const int64 BaseX = (CX - int64(MinInclusive.X)) * FVoxelLandChunkData::Cells;
				const int64 BaseY = (CY - int64(MinInclusive.Y)) * FVoxelLandChunkData::Cells;
				const int64 BaseZ = (CZ - int64(MinInclusive.Z)) * FVoxelLandChunkData::Cells;
				int32 Index = 0;
				for (int64 Z = 0; Z < FVoxelLandChunkData::SamplesPerAxis; ++Z)
				{
					for (int64 Y = 0; Y < FVoxelLandChunkData::SamplesPerAxis; ++Y)
					{
						for (int64 X = 0; X < FVoxelLandChunkData::SamplesPerAxis; ++X, ++Index)
						{
							if ((Index & 1023) == 0 && IsCancelled())
							{
								return FVoxelLandM5ScratchChunksInternals::Error(E::Cancelled, TEXT("Scratch chunk assembly cancelled"));
							}
							const int64 Source = BaseX + X + int64(Count.X) * (BaseY + Y) + XY * (BaseZ + Z);
							const int16 Value = Density[int32(Source)];
							Chunk.Density[Index] = Value;
							const uint16 Bits = uint16(Value);
							HashInput.Add(uint8(Bits));
							HashInput.Add(uint8(Bits >> 8));
						}
					}
				}
				Chunk.Hash = FXxHash64::HashBuffer(HashInput.GetData(), HashInput.Num()).Hash;
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5ScratchChunksInternals::Error(E::Cancelled, TEXT("Scratch chunk assembly cancelled"));
	}
	OutChunks = MoveTemp(Result);
	return {};
}
