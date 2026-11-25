#include "Generation/VoxelLandGenerator.h"

#include "Data/Structs/VoxelLandCanonicalWriter.h"
#include "Data/Structs/VoxelLandWorkerContextImpl.h"
#include "Generation/VoxelLandM4Unlimited.h"
#include "Generation/VoxelLandM6OverlayValidation.h"
#include "Generation/VoxelLandM6StampComposition.h"
#include "Generation/VoxelLandM6StampMath.h"

class FVoxelLandM6StampList final
{
public:
	static constexpr uint32 SphereIdentityTag = 0x5053364d; // M6SP in little endian.
	static constexpr uint32 StampIdentityTag = 0x5453364d; // M6ST in little endian.

	static FVoxelLandError Validate(TConstArrayView<FVoxelLandM6Stamp> Stamps, int64 CoordinateLimit);
	static void WriteId(FCanonicalWriter& Writer, const FGuid& Id);
	static void WritePoint(FCanonicalWriter& Writer, const FInt64Vector& Point);
};

FVoxelLandError FVoxelLandM6StampList::Validate(TConstArrayView<FVoxelLandM6Stamp> Stamps, int64 CoordinateLimit)
{
	if (Stamps.Num() > FVoxelLandM6StampSettings::MaxStamps)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Too many M6 stamp entries")};
	}
	for (int32 Index = 0; Index < Stamps.Num(); ++Index)
	{
		const FVoxelLandM6Stamp& Stamp = Stamps[Index];
		if (FVoxelLandError Error = FVoxelLandM6StampMath::Validate(Stamp, CoordinateLimit))
		{
			Error.Message = FString::Printf(TEXT("Stamp %d: %s"), Index + 1, *Error.Message);
			return Error;
		}
		for (int32 Prior = 0; Prior < Index; ++Prior)
		{
			if (Stamps[Prior].Id == Stamp.Id)
			{
				return {EVoxelLandErrorCode::InvalidParameter, TEXT("Duplicate M6 stamp ID")};
			}
		}
	}
	return {};
}

void FVoxelLandM6StampList::WriteId(FCanonicalWriter& Writer, const FGuid& Id)
{
	Writer.U32(Id.A);
	Writer.U32(Id.B);
	Writer.U32(Id.C);
	Writer.U32(Id.D);
}

void FVoxelLandM6StampList::WritePoint(FCanonicalWriter& Writer, const FInt64Vector& Point)
{
	const int64 Axes[] = {Point.X, Point.Y, Point.Z};
	for (int64 Axis : Axes)
	{
		const uint64 Bits = uint64(Axis);
		Writer.U32(uint32(Bits));
		Writer.U32(uint32(Bits >> 32));
	}
}

FVoxelLandM6Stamp FVoxelLandM6StampComposition::CutHole(FVoxelLandM6Stamp Stamp)
{
	Stamp.Operation = EVoxelLandM6StampOperation::Subtract;
	return Stamp;
}

FVoxelLandError FVoxelLandM6StampComposition::AppendBoxFoundation(const FVoxelLandM6Stamp& ClearingBox,
	const FVoxelLandM6Stamp& FillingBox, TArray<FVoxelLandM6Stamp>& Stamps)
{
	constexpr int32 FoundationEntryCount = 2;
	if (ClearingBox.Shape != EVoxelLandM6StampShape::Box || FillingBox.Shape != EVoxelLandM6StampShape::Box
		|| Stamps.Num() > FVoxelLandM6StampSettings::MaxStamps - FoundationEntryCount)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("M6 foundations require two box entries within the stamp budget")};
	}
	TArray<FVoxelLandM6Stamp, TInlineAllocator<FVoxelLandM6StampSettings::MaxStamps>> Candidate(Stamps);
	Candidate.Add(CutHole(ClearingBox));
	FVoxelLandM6Stamp Fill = FillingBox;
	Fill.Operation = EVoxelLandM6StampOperation::Add;
	Candidate.Add(Fill);
	if (const FVoxelLandError Error =
			FVoxelLandM6StampList::Validate(Candidate, FVoxelLandM6StampMath::CoordinateLimitCm))
	{
		return Error;
	}
	Stamps.Append(Candidate.GetData() + Stamps.Num(), FoundationEntryCount);
	return {};
}

FVoxelLandError FVoxelLandGenerator::InitializeM6Spheres(const FVoxelLandM6SphereSettings& Settings)
{
	if (Settings.Version != FVoxelLandM6SphereSettings::CurrentVersion
		|| Settings.Stamps.Num() > FVoxelLandM6SphereSettings::MaxStamps)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Unsupported M6 sphere version or stamp count")};
	}
	TArray<FVoxelLandM6Stamp, TInlineAllocator<FVoxelLandM6SphereSettings::MaxStamps>> Converted;
	for (const FVoxelLandM6SphereStamp& Legacy : Settings.Stamps)
	{
		FVoxelLandM6Stamp& Stamp = Converted.AddDefaulted_GetRef();
		Stamp.Id = Legacy.Id;
		Stamp.Operation = Legacy.Operation;
		Stamp.OriginCm = FInt64Vector(Legacy.CenterCm);
		Stamp.RadiusCm = Legacy.RadiusCm;
	}
	if (const FVoxelLandError Error = FVoxelLandM6StampList::Validate(Converted, FVoxelLandM6StampMath::LegacySphereLimitCm))
	{
		return Error;
	}

	FCanonicalWriter Writer;
	constexpr int32 LegacyRecordBytes = 8 * sizeof(uint32) + sizeof(uint8);
	Writer.Bytes.Reserve(3 * sizeof(uint32) + Settings.Stamps.Num() * LegacyRecordBytes);
	Writer.U32(FVoxelLandM6StampList::SphereIdentityTag);
	Writer.U32(Settings.Version);
	Writer.U32(uint32(Settings.Stamps.Num()));
	for (const FVoxelLandM6SphereStamp& Stamp : Settings.Stamps)
	{
		FVoxelLandM6StampList::WriteId(Writer, Stamp.Id);
		Writer.U8(uint8(Stamp.Operation));
		Writer.U32(uint32(Stamp.CenterCm.X));
		Writer.U32(uint32(Stamp.CenterCm.Y));
		Writer.U32(uint32(Stamp.CenterCm.Z));
		Writer.U32(uint32(Stamp.RadiusCm));
	}

	bM6LegacySphereEnvelope = true;
	M6StampIdentity = MoveTemp(Writer.Bytes);
	M6Stamps.Append(Converted.GetData(), Converted.Num());
	return {};
}

FVoxelLandError FVoxelLandM6OverlayValidation::ValidateStamps(const FVoxelLandM6StampSettings& Settings, float VoxelSizeCm)
{
	if (Settings.Version != FVoxelLandM6StampSettings::CurrentVersion)
	{
		return {EVoxelLandErrorCode::InvalidParameter, TEXT("Unsupported M6 stamp-list version")};
	}
	if (const FVoxelLandError Error = ValidateVoxelSize(VoxelSizeCm))
	{
		return Error;
	}
	return FVoxelLandM6StampList::Validate(Settings.Stamps, FVoxelLandM6StampMath::CoordinateLimitCm);
}

FVoxelLandError FVoxelLandGenerator::InitializeM6Stamps(const FVoxelLandM6StampSettings& Settings, float VoxelSizeCm)
{
	if (const FVoxelLandError Error = FVoxelLandM6OverlayValidation::ValidateStamps(Settings, VoxelSizeCm))
	{
		return Error;
	}
	FCanonicalWriter Writer;
	constexpr int32 MaxRecordBytes = 17 * sizeof(uint32) + 2 * sizeof(uint8);
	Writer.Bytes.Reserve(3 * sizeof(uint32) + Settings.Stamps.Num() * MaxRecordBytes);
	Writer.U32(FVoxelLandM6StampList::StampIdentityTag);
	Writer.U32(Settings.Version);
	Writer.U32(uint32(Settings.Stamps.Num()));
	for (const FVoxelLandM6Stamp& Stamp : Settings.Stamps)
	{
		FVoxelLandM6StampList::WriteId(Writer, Stamp.Id);
		Writer.U8(uint8(Stamp.Operation));
		Writer.U8(uint8(Stamp.Shape));
		FVoxelLandM6StampList::WritePoint(Writer, Stamp.OriginCm);
		switch (Stamp.Shape)
		{
		case EVoxelLandM6StampShape::Sphere:
			Writer.U32(uint32(Stamp.RadiusCm));
			break;
		case EVoxelLandM6StampShape::Box:
			Writer.U32(uint32(Stamp.HalfExtentCm.X));
			Writer.U32(uint32(Stamp.HalfExtentCm.Y));
			Writer.U32(uint32(Stamp.HalfExtentCm.Z));
			break;
		case EVoxelLandM6StampShape::Capsule:
			FVoxelLandM6StampList::WritePoint(Writer, Stamp.SegmentEndCm);
			Writer.U32(uint32(Stamp.RadiusCm));
			break;
		}
	}
	M6StampIdentity = MoveTemp(Writer.Bytes);
	M6Stamps.Append(Settings.Stamps.GetData(), Settings.Stamps.Num());
	return {};
}

FVoxelLandError FVoxelLandGenerator::ValidateM6SphereFootprint(const FVoxelLandChunkKey& Key, int32 Halo) const
{
	constexpr int64 Limit = FVoxelLandM6StampMath::LegacySphereLimitCm;
	static_assert(2 * Limit / (FVoxelLandChunkData::SamplesPerAxis - 1) <= FVoxelLandM6StampMath::MaxVoxelSizeCm);
	const int64 VoxelSizeCm = int64(Profile.VoxelSizeCm);
	const FInt64Vector First = FVoxelLandM6StampMath::FirstSampleCm(Key, VoxelSizeCm) - FInt64Vector(Halo * VoxelSizeCm);
	const FInt64Vector Last = First + FInt64Vector((FVoxelLandChunkData::SamplesPerAxis - 1 + 2 * Halo) * VoxelSizeCm);
	if (First.X < -Limit || First.Y < -Limit || First.Z < -Limit
		|| Last.X > Limit || Last.Y > Limit || Last.Z > Limit)
	{
		return {EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M6 sphere sample footprint exceeds the coordinate range")};
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::ApplyM6Stamps(const FInt64Vector& First, int32 Axis, TArray<int16>& Samples,
	TFunctionRef<bool()> IsCancelled) const
{
	check(Samples.Num() == Axis * Axis * Axis);
	static_assert(FVoxelLandM6StampSettings::MaxStamps * FVoxelLandChunkData::SampleCount == 686000);
	const int64 VoxelSizeCm = int64(Profile.VoxelSizeCm);
	int32 Index = 0;
	for (int32 Z = 0; Z < Axis; ++Z)
	{
		for (int32 Y = 0; Y < Axis; ++Y)
		{
			if (IsCancelled())
			{
				return {EVoxelLandErrorCode::Cancelled, bM6LegacySphereEnvelope
					? TEXT("M6 sphere stamp evaluation cancelled") : TEXT("M6 stamp evaluation cancelled")};
			}
			for (int32 X = 0; X < Axis; ++X)
			{
				const FInt64Vector PositionCm = First + FInt64Vector(X, Y, Z) * VoxelSizeCm;
				int16& Density = Samples[Index++];
				for (const FVoxelLandM6Stamp& Stamp : M6Stamps)
				{
					Density = FVoxelLandM6StampMath::Combine(Density, Stamp, PositionCm, VoxelSizeCm);
				}
			}
		}
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateM4LocalChunkWithStamps(const FVoxelLandChunkKey& Key,
	FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
	TFunctionRef<bool()> IsCancelled) const
{
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Baseline;
	if (const FVoxelLandError Error =
			VoxelLandM4Unlimited::GenerateChunk(*M4UnlimitedSettings, *M4Unlimited, Key, Baseline, IsCancelled))
	{
		return Error;
	}
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("M6 local chunk preparation cancelled")};
	}
	if (!Baseline || Baseline->Density.Num() != FVoxelLandChunkData::SampleCount)
	{
		return {EVoxelLandErrorCode::InvalidContext, TEXT("M6 requires a complete local M4 baseline chunk")};
	}
	// One fixed-size 35^3 int16 copy keeps the local backend's const output
	// immutable and separates it from the chunk we publish after composition.
	const TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk =
		MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>(*Baseline);
	Baseline.Reset();
	return FinalizeDensityChunk(Chunk, Worker, OutChunk, IsCancelled);
}

FVoxelLandError FVoxelLandGenerator::FinalizeDensityChunk(TSharedRef<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk,
	FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
	TFunctionRef<bool()> IsCancelled, bool bApplyStamps) const
{
	if (bApplyStamps && !M6Stamps.IsEmpty())
	{
		const FInt64Vector First = FVoxelLandM6StampMath::FirstSampleCm(Chunk->Key, int64(Profile.VoxelSizeCm));
		if (const FVoxelLandError Error =
				ApplyM6Stamps(First, FVoxelLandChunkData::SamplesPerAxis, Chunk->Density, IsCancelled))
		{
			return Error;
		}
	}
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled")};
	}
	Chunk->Hash = Worker.Impl->HashChunkDensity(Chunk->Density);
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled")};
	}
	OutChunk = MoveTemp(Chunk);
	return {};
}
