#include "Generation/VoxelLandM5EvaluateChunk.h"
#include "Data/Structs/VoxelLandChunkData.h"
#include "Data/Structs/VoxelLandM5ChunkFootprint.h"
#include "Data/Structs/VoxelLandM5FinalFieldInputs.h"
#include "Data/Structs/VoxelLandM5WorldSnapshot.h"
#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM5BaselineWindow.h"
#include "Generation/VoxelLandM5BoundBaseline.h"
#include "Generation/VoxelLandM5Budget.h"
#include "Generation/VoxelLandM5BudgetLease.h"
#include "Generation/VoxelLandM5CarveMasks.h"
#include "Generation/VoxelLandM5ChunkFootprint.h"
#include "Generation/VoxelLandM5FieldInputs.h"
#include "Generation/VoxelLandM5FiniteContributorSeal.h"
#include "Generation/VoxelLandM5CandidateResult.h"
#include "Generation/VoxelLandM5FinalField.h"
#include "Generation/VoxelLandM5InfluenceSet.h"
#include "Generation/VoxelLandM5PocketSurfaceColumns.h"
#include "Generation/VoxelLandM5ValidatedDefinition.h"
#include "Generation/VoxelLandM5WindowCarveMasks.h"
#include "Hash/xxhash.h"

namespace
{
FVoxelLandM5Diagnostic FieldError(EVoxelLandM5ErrorCode Code, const TCHAR* Message)
{
	FVoxelLandM5Diagnostic Result; Result.Code = Code; Result.Stage = TEXT("M5.EvaluateChunk"); Result.Message = Message; return Result;
}
struct FBorrowedPlanDeleter { void operator()(const FVoxelLandM5CavePlan*) const {} };
struct FChunkLeaseDeleter
{
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	void operator()(const FVoxelLandChunkData* Chunk) const { delete Chunk; }
};
}
FVoxelLandM5Diagnostic VoxelLand::M5::FinalField::EvaluateChunk(const FVoxelLandM5FieldInputs& Inputs,
	const FVoxelLandChunkKey& Key, FVoxelLandWorkerContext& Worker,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Job, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk)
{
	using E = EVoxelLandM5ErrorCode;
	using namespace VoxelLand::M5::CheckedMath;
	if (IsCancelled()) return FieldError(E::Cancelled, TEXT("Chunk evaluation cancelled"));
	const auto& Baseline = Inputs.GetBaseline();
	const auto& Influence = Inputs.GetInfluence();
	const auto& Snapshot = Baseline.GetDefinition().GetSnapshot();
	const auto& Generator = Baseline.GetGenerator();
	if (Key.LOD != 0 || &Baseline.GetDefinition() != &Influence.GetDefinition()
		|| !Baseline.UsesBudget(*Inputs.GetSourceBudget()) || !Influence.UsesBudget(*Inputs.GetSourceBudget())
		|| !Generator.OwnsWorkerOnCurrentThread(Worker))
		return FieldError(E::InvalidInput, TEXT("Chunk key, source or worker mismatch"));
	if ((Snapshot.Caves.bEnabled || Snapshot.Caves.Pockets.bEnabled) && Snapshot.Manifest.Composition != 2)
		return FieldError(E::UnsupportedVersion, TEXT("Unsupported composition version"));
	if (Snapshot.Caves.Pockets.bEnabled && Snapshot.Manifest.PocketNoise != 1)
		return FieldError(E::UnsupportedVersion, TEXT("Unsupported pocket noise version"));
	const FVoxelLandM5FiniteContributorSeal* Seal = Inputs.GetSeal();
	if (Snapshot.Caves.bEnabled && (!Seal || !Seal->Matches(Baseline, Influence, *Inputs.GetSourceBudget())))
		return FieldError(E::Unproven, TEXT("Network contributor completeness unavailable"));
	if (Snapshot.Caves.bEnabled && Snapshot.Manifest.Geometry == 2
		&& (!Seal->GetCandidate().HasOwnedCanonicalPlan(*Inputs.GetSourceBudget())
			|| Seal->GetCandidate().GetPlan().GeometryVersion != Snapshot.Manifest.Geometry))
		return FieldError(E::InvalidInput, TEXT("Owned profile candidate mismatch"));
	if (Snapshot.Caves.bEnabled && Snapshot.Manifest.Geometry != 1 && Snapshot.Manifest.Geometry != 2)
		return FieldError(E::UnsupportedVersion, TEXT("Unsupported geometry version"));
	if (!Influence.CoversChunk(Key))
		return FieldError(E::OutOfDomain, TEXT("Chunk is outside influence coverage"));
	int64 JobBytes = 0, JobWork = 0;
	Job->GetLimits(JobBytes, JobWork);
	if (&Job.Get() == &Inputs.GetSourceBudget().Get() || JobBytes > Snapshot.Caves.Limits.MaxBytes
		|| JobWork > Snapshot.Caves.Limits.MaxValidationWork)
		return FieldError(E::InvalidInput, TEXT("Invalid chunk job budget"));
	const int64 OutputBytes = sizeof(FVoxelLandChunkData) + int64(FVoxelLandChunkData::SampleCount) * sizeof(int16) + 4096;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> OutputLease;
	if (!Job->TryChargeWork(FVoxelLandChunkData::SampleCount) || !Job->TryReserveBytes(OutputBytes, OutputLease))
		return FieldError(E::BudgetExceeded, TEXT("Chunk output admission exhausted"));
	const bool bPocket = Snapshot.Caves.Pockets.bEnabled && Snapshot.Caves.Pockets.AmplitudeCm != 0;
	if (!bPocket && !Snapshot.Caves.bEnabled)
	{
		FVoxelLandM5ChunkFootprint Stored;
		if (!VoxelLand::M5::ChunkFootprint::TryBuild(Key, Snapshot.Base.VoxelSizeCm, false, false, Stored))
			return FieldError(E::OutOfDomain, TEXT("Chunk footprint overflow"));
		FVoxelLandM5PointCm Min;
		Min.X = Stored.StoredMin.X / Snapshot.Base.VoxelSizeCm;
		Min.Y = Stored.StoredMin.Y / Snapshot.Base.VoxelSizeCm;
		Min.Z = Stored.StoredMin.Z / Snapshot.Base.VoxelSizeCm;
		TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> HashLease;
		if (!Job->TryReserveBytes(int64(FVoxelLandChunkData::SampleCount) * 2 + 4096, HashLease))
			return FieldError(E::BudgetExceeded, TEXT("Chunk hash admission exhausted"));
		TSharedPtr<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk =
			MakeShareable(new FVoxelLandChunkData(), FChunkLeaseDeleter{OutputLease});
		Chunk->Key = Key;
		const auto Generated = Generator.GenerateFiniteM4Box(Min, FIntVector(35, 35, 35), Worker, Job,
			IsCancelled, Chunk->Density);
		if (Generated.Code != E::None) return Generated;
		if (Chunk->Density.Num() != FVoxelLandChunkData::SampleCount)
			return FieldError(E::Unproven, TEXT("M4 chunk generation incomplete"));
		TArray<uint8> HashInput; HashInput.Reserve(FVoxelLandChunkData::SampleCount * 2);
		for (int16 Sample : Chunk->Density) { const uint16 Bits = uint16(Sample); HashInput.Add(uint8(Bits)); HashInput.Add(uint8(Bits >> 8)); }
		Chunk->Hash = FXxHash64::HashBuffer(HashInput.GetData(), HashInput.Num()).Hash;
		if (IsCancelled()) return FieldError(E::Cancelled, TEXT("Chunk evaluation cancelled"));
		OutChunk = MoveTemp(Chunk); return {};
	}
	const int64 V = Snapshot.Base.VoxelSizeCm;
	if (V <= 0 || Snapshot.Caves.RoofThicknessCm < 0) return FieldError(E::InvalidInput, TEXT("Invalid chunk lattice"));
	const int64 Halo64 = Snapshot.Caves.RoofThicknessCm / V + (Snapshot.Caves.RoofThicknessCm % V != 0) + 4;
	if (Halo64 > (MAX_int32 - FVoxelLandChunkData::SamplesPerAxis) / 2)
		return FieldError(E::ArithmeticOverflow, TEXT("Chunk halo overflow"));
	const int32 Halo = int32(Halo64), Side = FVoxelLandChunkData::SamplesPerAxis + 2 * Halo;
	FVoxelLandM5ChunkFootprint Footprint;
	if (!VoxelLand::M5::ChunkFootprint::TryBuildExpanded(Key, V, false, false, Halo, 0, Footprint))
		return FieldError(E::OutOfDomain, TEXT("Chunk footprint overflow"));
	FVoxelLandM5PointCm Min;
	Min.X = Footprint.BaselineMin.X / V; Min.Y = Footprint.BaselineMin.Y / V; Min.Z = Footprint.BaselineMin.Z / V;
	TSharedPtr<const FVoxelLandM5BaselineWindow, ESPMode::ThreadSafe> Window;
	auto Result = FVoxelLandM5BaselineWindow::BuildBox(Baseline.GetGeneratorShared(), Worker, Min,
		FIntVector(Side, Side, Side), Job, IsCancelled, Window);
	if (Result.Code != E::None) return Result;
	if (!Window) return FieldError(E::Unproven, TEXT("Baseline window missing"));
	FVoxelLandM5PointCm OutputMin;
	OutputMin.X = Footprint.StoredMin.X / V; OutputMin.Y = Footprint.StoredMin.Y / V; OutputMin.Z = Footprint.StoredMin.Z / V;
	TVoxelLandM5ConstPtr<FVoxelLandM5PocketSurfaceColumns> Columns;
	if (bPocket)
	{
		Result = FVoxelLandM5PocketSurfaceColumns::BuildForJob(Inputs.GetBaselineShared(), Inputs.GetInfluenceShared(),
			Worker, OutputMin, FIntPoint(35, 35), Inputs.GetSourceBudget(), Job, IsCancelled, Columns);
		if (Result.Code != E::None) return Result;
		if (!Columns) return FieldError(E::Unproven, TEXT("Pocket columns missing"));
	}
	TSharedPtr<const FVoxelLandM5CarveMasks, ESPMode::ThreadSafe> Masks;
	if (Snapshot.Caves.bEnabled)
		Result = VoxelLand::M5::WindowCarveMasks::BuildSealedForJob(Baseline, Influence, *Seal, Columns.Get(), *Window,
			FIntVector(Halo, Halo, Halo), FIntVector(35, 35, 35), Inputs.GetSourceBudget(), Job, IsCancelled, Masks);
	else
		Result = VoxelLand::M5::WindowCarveMasks::BuildOwnedForJob(Baseline, Influence, *Columns, *Window,
			FIntVector(Halo, Halo, Halo), FIntVector(35, 35, 35), Inputs.GetSourceBudget(), Job, IsCancelled, Masks);
	if (Result.Code != E::None) return Result;
	if (!Masks || Masks->GetPocketAllowed().Num() != FVoxelLandChunkData::SampleCount
		|| Masks->GetNetworkAllowed().Num() != FVoxelLandChunkData::SampleCount
		|| Masks->GetProtected().Num() != FVoxelLandChunkData::SampleCount)
		return FieldError(E::Unproven, TEXT("Chunk masks missing or incomplete"));
	const int32 Count = FVoxelLandChunkData::SampleCount;
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> CropLease;
	if (!Job->TryChargeWork(Count * 4) || !Job->TryReserveBytes(int64(Count) * 6 + 4096, CropLease))
		return FieldError(E::BudgetExceeded, TEXT("Chunk crop admission exhausted"));
	TArray<int16> Cropped; Cropped.SetNumUninitialized(Count);
	TArray<uint8> GatePocket; GatePocket.SetNumUninitialized(Count);
	TArray<uint8> GateNetwork; GateNetwork.SetNumUninitialized(Count);
	const auto Source = Window->GetDensity();
	const auto Allowed = Masks->GetPocketAllowed();
	const auto NetworkAllowed = Masks->GetNetworkAllowed();
	const auto& Gate = Influence.GetDeclaredBounds();
	int32 I = 0;
	for (int32 Z = 0; Z < 35; ++Z) for (int32 Y = 0; Y < 35; ++Y) for (int32 X = 0; X < 35; ++X, ++I)
	{
		if ((I & 1023) == 0 && IsCancelled()) return FieldError(E::Cancelled, TEXT("Chunk evaluation cancelled"));
		const int64 SX = X + Halo, SY = Y + Halo, SZ = Z + Halo;
		Cropped[I] = Source[int32(SX + int64(Side) * (SY + int64(Side) * SZ))];
		const int64 CX = Footprint.StoredMin.X + int64(X) * V, CY = Footprint.StoredMin.Y + int64(Y) * V,
			CZ = Footprint.StoredMin.Z + int64(Z) * V;
		const bool bInside = CX >= Gate.MinInclusive.X && CX < Gate.MaxExclusive.X
			&& CY >= Gate.MinInclusive.Y && CY < Gate.MaxExclusive.Y
			&& CZ >= Gate.MinInclusive.Z && CZ < Gate.MaxExclusive.Z;
		GatePocket[I] = Allowed[I] && bInside ? 1 : 0;
		GateNetwork[I] = NetworkAllowed[I] && bInside ? 1 : 0;
	}
	TArray<TSharedPtr<const FVoxelLandM5CavePlan, ESPMode::ThreadSafe>> Plans;
	if (Seal) Plans.Add(MakeShareable(&Seal->GetCandidate().GetPlan(), FBorrowedPlanDeleter{}));
	FVoxelLandM5FinalFieldInputs Field;
	Field.bNetworkEnabled = Snapshot.Caves.bEnabled; Field.Plans = Plans; Field.NetworkAllowed = GateNetwork;
	Field.Size = FIntVector(35, 35, 35); Field.OriginCm = Footprint.StoredMin; Field.SpacingCm = V;
	Field.VoxelSizeCm = int32(V); Field.Baseline = Cropped; Field.PocketAllowed = GatePocket;
	Field.Protected = Masks->GetProtected(); Field.Pockets = Snapshot.Caves.Pockets;
	Field.PocketNoiseVersion = Snapshot.Manifest.PocketNoise; Field.CompositionVersion = Snapshot.Manifest.Composition;
	Field.GeometryVersion = Snapshot.Manifest.Geometry;
	Field.WorldSeed = Snapshot.Base.Seed; Field.StageId = Snapshot.Caves.StageId;
	TSharedPtr<const FVoxelLandM5FinalField, ESPMode::ThreadSafe> Evaluated;
	Result = Snapshot.Caves.bEnabled && Snapshot.Manifest.Geometry == 2
		? FVoxelLandM5FinalField::BuildOwned(Inputs, Field, Job, IsCancelled, Evaluated)
		: FVoxelLandM5FinalField::Build(Field, Job, IsCancelled, Evaluated);
	if (Result.Code != E::None) return Result;
	if (!Evaluated || Evaluated->GetDensity().Num() != Count) return FieldError(E::Unproven, TEXT("Chunk field missing"));
	TSharedPtr<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk =
		MakeShareable(new FVoxelLandChunkData(), FChunkLeaseDeleter{OutputLease});
	Chunk->Key = Key; Chunk->Density.Append(Evaluated->GetDensity().GetData(), Count);
	TArray<uint8> HashInput; HashInput.Reserve(Count * 2);
	for (int16 Sample : Chunk->Density) { const uint16 Bits = uint16(Sample); HashInput.Add(uint8(Bits)); HashInput.Add(uint8(Bits >> 8)); }
	Chunk->Hash = FXxHash64::HashBuffer(HashInput.GetData(), HashInput.Num()).Hash;
	if (IsCancelled()) return FieldError(E::Cancelled, TEXT("Chunk evaluation cancelled"));
	OutChunk = MoveTemp(Chunk); return {};
}
