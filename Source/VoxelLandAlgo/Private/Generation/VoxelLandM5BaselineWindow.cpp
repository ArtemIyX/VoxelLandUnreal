#include "Generation/VoxelLandM5BaselineWindow.h"

#include "Data/Structs/VoxelLandChunkKey.h"
#include "Data/Structs/VoxelLandM5ChunkFootprint.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5ChunkFootprint.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CheckedMath.h"

class FVoxelLandM5BaselineWindowInternals final
{
public:
	static FVoxelLandM5Diagnostic M5BaselineWindowError(EVoxelLandM5ErrorCode Code, const TCHAR* Message);
	static FVoxelLandM5Diagnostic M5BaselineWindowM4Error(const FVoxelLandError& Error, bool bDemand);
};

FVoxelLandM5Diagnostic FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
	EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result;
	Result.Code = Code;
	Result.Stage = TEXT("M5.BaselineWindow");
	Result.Message = Message;
	return Result;
}

FVoxelLandM5Diagnostic FVoxelLandM5BaselineWindowInternals::M5BaselineWindowM4Error(
	const FVoxelLandError& Error, bool bDemand)
{
	using E = EVoxelLandM5ErrorCode;
	E Code = E::Unproven;
	if (Error.Code == EVoxelLandErrorCode::Cancelled)
	{
		Code = E::Cancelled;
	}
	else if (Error.Code == EVoxelLandErrorCode::NonFiniteCoordinate
			 || (bDemand && Error.Code == EVoxelLandErrorCode::InvalidParameter))
	{
		Code = E::OutOfDomain;
	}
	else if (Error.Code == EVoxelLandErrorCode::NativeLibraryUnavailable
			 || Error.Code == EVoxelLandErrorCode::BackendUnavailable)
	{
		Code = E::ProviderUnavailable;
	}
	FVoxelLandM5Diagnostic Result = M5BaselineWindowError(Code, TEXT("M4 baseline failed"));
	Result.Message = FString::Printf(TEXT("M4 %s failed (code %d): %s"), bDemand ? TEXT("demand") : TEXT("generation"),
		int32(Error.Code), *Error.Message);
	return Result;
}

FVoxelLandM5BaselineWindow::FVoxelLandM5BaselineWindow(
	TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> InGenerator,
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
	, Generator(MoveTemp(InGenerator))
{
}

bool FVoxelLandM5BaselineWindow::IsOwnedBy(const FVoxelLandM5Budget& Budget) const
{
	return Lease.IsValid() && Budget.OwnsLease(*Lease);
}

FVoxelLandM5Diagnostic FVoxelLandM5BaselineWindow::Build(
	TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator, const FIntVector& MinChunk,
	const FIntVector& MaxChunk, int32 MaxChunks, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe>& OutWindow)
{
	return BuildInternal(Generator, nullptr, MinChunk, MaxChunk, MaxChunks, Budget, IsCancelled, OutWindow);
}

FVoxelLandM5Diagnostic FVoxelLandM5BaselineWindow::Build(
	TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator, FVoxelLandWorkerContext& Worker,
	const FIntVector& MinChunk, const FIntVector& MaxChunk, int32 MaxChunks,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe>& OutWindow)
{
	return BuildInternal(Generator, &Worker, MinChunk, MaxChunk, MaxChunks, Budget, IsCancelled, OutWindow);
}

FVoxelLandM5Diagnostic FVoxelLandM5BaselineWindow::BuildBox(
	TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator, FVoxelLandWorkerContext& Worker,
	const FVoxelLandM5PointCm& MinLattice, const FIntVector& Size,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe>& OutWindow)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	using I = FVoxelLandM5BaselineWindowInternals;
	if (IsCancelled()) return I::M5BaselineWindowError(E::Cancelled, TEXT("Baseline box cancelled"));
	const float RawSpacing = Generator->GetProfile().VoxelSizeCm;
	if (!Generator->OwnsWorkerOnCurrentThread(Worker) || Size.X < 1 || Size.Y < 1 || Size.Z < 1
		|| Size.X > 4096 || Size.Y > 4096 || Size.Z > 4096 || !FMath::IsFinite(RawSpacing)
		|| RawSpacing < 1.f || RawSpacing > 65536.f || RawSpacing != float(int32(RawSpacing)))
		return I::M5BaselineWindowError(E::InvalidInput, TEXT("Invalid baseline box or worker"));
	int64 Count = 1, Bytes = 0;
	FVoxelLandM5PointCm Origin;
	const int64 Coordinates[] = {MinLattice.X, MinLattice.Y, MinLattice.Z};
	int64* Origins[] = {&Origin.X, &Origin.Y, &Origin.Z};
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		int64 Last = 0, LastCm = 0;
		if (!TryMultiplyNonNegative(Count, Size[Axis], Count) || Count > MAX_int32
			|| !TryAdd(Coordinates[Axis], int64(Size[Axis]) - 1, Last)
			|| !TryMultiply(Coordinates[Axis], int64(RawSpacing), *Origins[Axis])
			|| !TryMultiply(Last, int64(RawSpacing), LastCm)
			|| *Origins[Axis] < -1048576 || LastCm > 1048576)
			return I::M5BaselineWindowError(E::OutOfDomain, TEXT("Baseline box outside logical domain"));
	}
	if (!TryMultiplyNonNegative(Count, int64(sizeof(int16)), Bytes) || !TryAdd(Bytes, 4096, Bytes))
		return I::M5BaselineWindowError(E::ArithmeticOverflow, TEXT("Baseline box admission overflow"));
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease))
		return I::M5BaselineWindowError(E::BudgetExceeded, TEXT("Baseline box memory exhausted"));
	TArray<int16> Density;
	const auto Diagnostic = Generator->GenerateFiniteM4Box(MinLattice, Size, Worker, Budget, IsCancelled, Density);
	if (Diagnostic.Code != E::None) return Diagnostic;
	if (Density.Num() != Count) return I::M5BaselineWindowError(E::Unproven, TEXT("Baseline box sample count mismatch"));
	if (IsCancelled()) return I::M5BaselineWindowError(E::Cancelled, TEXT("Baseline box cancelled"));
	TSharedPtr<FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Built =
		MakeShareable(new FVoxelLandM5BaselineWindow(Generator, MoveTemp(Lease)));
	Built->Size = Size; Built->OriginCm = Origin; Built->VoxelSizeCm = int32(RawSpacing);
	Built->Density = MoveTemp(Density);
	if (IsCancelled()) return I::M5BaselineWindowError(E::Cancelled, TEXT("Baseline box cancelled"));
	OutWindow = MoveTemp(Built);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandM5BaselineWindow::BuildInternal(
	TSharedRef<const FVoxelLandGenerator, ESPMode::ThreadSafe> Generator, FVoxelLandWorkerContext* SuppliedWorker,
	const FIntVector& MinChunk, const FIntVector& MaxChunk, int32 MaxChunks,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe>& OutWindow)
{
	using namespace VoxelLand::M5::CheckedMath;
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
			E::Cancelled, TEXT("Baseline generation cancelled"));
	}
	if (IsInGameThread() || MaxChunks < 1 || MaxChunks > 512
		|| (SuppliedWorker && !Generator->OwnsWorkerOnCurrentThread(*SuppliedWorker))
		|| (!Generator->IsM4Unlimited() && !Generator->GetM4HeightGrid().IsValid()))
	{
		return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
			E::InvalidInput, TEXT("Expected an M4 generator and worker thread"));
	}
	const float RawSpacing = Generator->GetProfile().VoxelSizeCm;
	if (!FMath::IsFinite(RawSpacing) || RawSpacing < 1.f || RawSpacing > 65536.f
		|| RawSpacing != static_cast<float>(static_cast<int32>(RawSpacing)))
	{
		return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
			E::InvalidInput, TEXT("M5 requires integer voxel spacing in centimetres"));
	}
	const int32 Spacing = static_cast<int32>(RawSpacing);
	int64 ChunkCounts[3] = {}, Sizes[3] = {}, Origins[3] = {};
	int64 ChunkCount = 1, Samples = 1;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 Count = int64(MaxChunk[Axis]) - int64(MinChunk[Axis]);
		if (Count < 1 || Count > MaxChunks || !TryMultiplyNonNegative(ChunkCount, Count, ChunkCount)
			|| ChunkCount > MaxChunks || !TryMultiply(Count, 32, Sizes[Axis]) || !TryAdd(Sizes[Axis], 3, Sizes[Axis])
			|| Sizes[Axis] > MAX_int32 || !TryMultiplyNonNegative(Samples, Sizes[Axis], Samples) || Samples > MAX_int32)
		{
			return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
				E::OutOfDomain, TEXT("Chunk rectangle or sample footprint is unsupported"));
		}
		ChunkCounts[Axis] = Count;
	}
	FVoxelLandChunkKey FirstKey;
	FirstKey.Coordinate = MinChunk;
	FVoxelLandChunkKey LastKey;
	LastKey.Coordinate = FIntVector(MaxChunk.X - 1, MaxChunk.Y - 1, MaxChunk.Z - 1);
	FVoxelLandM5ChunkFootprint FirstFootprint;
	FVoxelLandM5ChunkFootprint LastFootprint;
	if (!VoxelLand::M5::ChunkFootprint::TryBuild(FirstKey, Spacing, false, false, FirstFootprint)
		|| !VoxelLand::M5::ChunkFootprint::TryBuild(LastKey, Spacing, false, false, LastFootprint))
	{
		return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
			E::OutOfDomain, TEXT("Chunk rectangle or sample footprint is unsupported"));
	}
	Origins[0] = FirstFootprint.StoredMin.X;
	Origins[1] = FirstFootprint.StoredMin.Y;
	Origins[2] = FirstFootprint.StoredMin.Z;
	const int64 Lasts[] = {LastFootprint.StoredMax.X, LastFootprint.StoredMax.Y, LastFootprint.StoredMax.Z};
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Origins[Axis] < -1048576 || Lasts[Axis] > 1048576)
		{
			return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
				E::OutOfDomain, TEXT("Chunk rectangle or sample footprint is unsupported"));
		}
	}
	int64 OutputBytes = 0, ScratchBytes = 0, Work = 0;
	if (!TryMultiplyNonNegative(Samples, int64(sizeof(int16)), OutputBytes) || !TryAdd(OutputBytes, 4096, OutputBytes)
		|| !TryAdd(Samples, int64(FVoxelLandChunkData::SampleCount) * sizeof(int16) + 4096, ScratchBytes)
		|| !TryMultiplyNonNegative(ChunkCount, int64(FVoxelLandChunkData::SampleCount) * 8, Work)
		|| !TryAdd(Work, Samples, Work) || !TryAdd(Work, ChunkCount * 2 + 1, Work))
	{
		return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
			E::ArithmeticOverflow, TEXT("Baseline admission overflow"));
	}
	if (!Budget->TryChargeWork(Work))
	{
		return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
			E::BudgetExceeded, TEXT("Baseline assembly budget exhausted"));
	}
	// Prove every upstream M4 filter/slope footprint before allocating either
	// the output or a worker. M4 demand validation is read-only and covers both
	// finite height grids and the selected local-mode coordinate/backend rules.
	for (int64 Z = 0; Z < ChunkCounts[2]; ++Z)
	{
		for (int64 Y = 0; Y < ChunkCounts[1]; ++Y)
		{
			for (int64 X = 0; X < ChunkCounts[0]; ++X)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
						E::Cancelled, TEXT("Baseline demand preflight cancelled"));
				}
				FVoxelLandChunkKey Key;
				Key.Coordinate = FIntVector(
					int32(int64(MinChunk.X) + X), int32(int64(MinChunk.Y) + Y), int32(int64(MinChunk.Z) + Z));
				if (const FVoxelLandError Error = Generator->ValidateM4ChunkDemand(Key))
				{
					return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowM4Error(Error, true);
				}
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
			E::Cancelled, TEXT("Baseline demand preflight cancelled"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> OutputLease, ScratchLease;
	if (!Budget->TryReserveBytes(OutputBytes, OutputLease) || !Budget->TryReserveBytes(ScratchBytes, ScratchLease))
	{
		return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
			E::BudgetExceeded, TEXT("Baseline assembly budget exhausted"));
	}
	TUniquePtr<FVoxelLandWorkerContext> OwnedWorker;
	if (!SuppliedWorker)
	{
		const FVoxelLandError WorkerError = Generator->CreateWorker(OwnedWorker);
		if (WorkerError)
		{
			return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowM4Error(WorkerError, false);
		}
		if (!OwnedWorker)
		{
			return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
				E::ProviderUnavailable, TEXT("M4 worker could not be created"));
		}
		SuppliedWorker = OwnedWorker.Get();
	}
	TSharedPtr<FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Scratch =
		MakeShareable(new FVoxelLandM5BaselineWindow(Generator, MoveTemp(OutputLease)));
	Scratch->Size = FIntVector(int32(Sizes[0]), int32(Sizes[1]), int32(Sizes[2]));
	Scratch->OriginCm.X = Origins[0];
	Scratch->OriginCm.Y = Origins[1];
	Scratch->OriginCm.Z = Origins[2];
	Scratch->VoxelSizeCm = Spacing;
	Scratch->Density.SetNumUninitialized(int32(Samples));
	TArray<uint8> Seen;
	Seen.Init(0, int32(Samples));
	int64 Visits = 0;
	for (int64 Z = 0; Z < ChunkCounts[2]; ++Z)
	{
		for (int64 Y = 0; Y < ChunkCounts[1]; ++Y)
		{
			for (int64 X = 0; X < ChunkCounts[0]; ++X)
			{
				if (IsCancelled())
				{
					return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
						E::Cancelled, TEXT("Baseline generation cancelled"));
				}
				FVoxelLandChunkKey Key;
				Key.Coordinate = FIntVector(
					int32(int64(MinChunk.X) + X), int32(int64(MinChunk.Y) + Y), int32(int64(MinChunk.Z) + Z));
				TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
				const FVoxelLandError Error = Generator->GenerateChunkCancellable(Key, *SuppliedWorker, Chunk, IsCancelled);
				if (Error)
				{
					return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowM4Error(Error, false);
				}
				if (!Chunk || !(Chunk->Key == Key) || Chunk->Density.Num() != FVoxelLandChunkData::SampleCount)
				{
					return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
						E::Unproven, TEXT("M4 chunk generation returned malformed data"));
				}
				for (int32 K = 0; K < 35; ++K)
				{
					for (int32 J = 0; J < 35; ++J)
					{
						for (int32 I = 0; I < 35; ++I)
						{
							if ((Visits++ & 63) == 0 && IsCancelled())
							{
								return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
									E::Cancelled, TEXT("Baseline assembly cancelled"));
							}
							const int64 Target = X * 32 + I + Sizes[0] * (Y * 32 + J + Sizes[1] * (Z * 32 + K));
							const int16 Value = Chunk->Density[I + 35 * (J + 35 * K)];
							if (Seen[int32(Target)] && Scratch->Density[int32(Target)] != Value)
							{
								return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
									E::Unproven, TEXT("M4 chunk overlap disagrees"));
							}
							Scratch->Density[int32(Target)] = Value;
							Seen[int32(Target)] = 1;
						}
					}
				}
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandM5BaselineWindowInternals::M5BaselineWindowError(
			E::Cancelled, TEXT("Baseline generation cancelled"));
	}
	OutWindow = MoveTemp(Scratch);
	return {};
}
