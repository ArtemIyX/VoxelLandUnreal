#include "Generation/Field/VoxelLandGenerator.h"
#include "VoxelLandTerrainHeightColumns.h"
#include "VoxelLandQuantizedDensity.h"
#include "../Terrain/ChunkLocal/VoxelLandFixedLatticeSampling.h"
#include "Generation/Terrain/Controls/VoxelLandTerrainControlMath.h"
#include "Generation/Terrain/Heightfield/VoxelLandHeightGrid.h"
#include "Generation/Terrain/ChunkLocal/VoxelLandFixedLattice.h"
#include "Generation/Terrain/Shaping/VoxelLandSlope.h"
#include "Generation/Terrain/Shaping/VoxelLandOverhang.h"
#include "Generation/Terrain/ChunkLocal/VoxelLandChunkLocalTerrain.h"
#include "Generation/Caves/Field/VoxelLandCaveChunkFootprint.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "../TerrainOverlay/Filters/VoxelLandTerrainFilterPlan.h"
#include "Data/Caves/Field/VoxelLandCaveChunkFootprintRecord.h"
#include "Data/Terrain/ChunkLocal/VoxelLandChunkLocalTerrainConfig.h"
#include "Data/Terrain/Shaping/VoxelLandTerrainSmoothingSettings.h"
#include "Data/Clipmap/Sampling/VoxelLandTerrainSampleGrid.h"
#include "Data/Field/Execution/VoxelLandWorkerContextImpl.h"
#include "../Noise/VoxelLandNativeNoise.h"
#include "HAL/PlatformTLS.h"
#include "Math/VectorRegister.h"
#include "Hash/xxhash.h"
#include "Operators/VoxelLandOperator.h"
#include <cfloat>

#ifndef VOXELLAND_ENABLE_VM_SIMD
#define VOXELLAND_ENABLE_VM_SIMD 1
#endif
#if VOXELLAND_ENABLE_VM_SIMD && PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS && PLATFORM_ENABLE_VECTORINTRINSICS
#define VOXELLAND_HAS_VM_SIMD 1
#else
#define VOXELLAND_HAS_VM_SIMD 0
#endif

class FVoxelLandGeneratorValidation final
{
public:
	static constexpr int32 BatchSize = 256;
	static constexpr int32 NoiseFamilyCount = 4;

	static int32 SignedSeed(uint64 WorldSeed, const FGuid& NodeId, uint32 Salt)
	{
		const uint32 Bits = uint32(VoxelLandDeterminism::DeriveNodeSeed(WorldSeed, NodeId, Salt));
		return Bits <= uint32(MAX_int32) ? int32(Bits) : int32(int64(Bits) - 4294967296ll);
	}

	static bool ApplyHeightDetail(int32 Base, int32 NoiseQ, int32 AmplitudeQ8, int32& Out)
	{
		const int64 Product = int64(AmplitudeQ8) * NoiseQ;
		const int64 Magnitude = Product < 0 ? -Product : Product;
		const int64 Delta = (Magnitude + 16383) / 32767;
		const int64 Height = int64(Base) + (Product < 0 ? -Delta : Delta);
		if (Height < -512000000 || Height > 512000000)
		{
			return false;
		}
		Out = int32(Height);
		return true;
	}

	static int32 NoiseVariant(EVoxelLandOpcode Op)
	{
		switch (Op)
		{
			case EVoxelLandOpcode::Perlin2:
			case EVoxelLandOpcode::Perlin3:
				return 0;
			case EVoxelLandOpcode::Fbm2:
			case EVoxelLandOpcode::Fbm3:
				return 1;
			case EVoxelLandOpcode::Value2:
			case EVoxelLandOpcode::Value3:
				return 2;
			case EVoxelLandOpcode::Simplex2:
			case EVoxelLandOpcode::Simplex3:
				return 3;
			default:
				return INDEX_NONE;
		}
	}

	static bool IsNoise3D(EVoxelLandOpcode Op)
	{
		return Op == EVoxelLandOpcode::Perlin3 || Op == EVoxelLandOpcode::Fbm3 || Op == EVoxelLandOpcode::Value3
			|| Op == EVoxelLandOpcode::Simplex3;
	}

	static FVoxelLandError Fail(EVoxelLandErrorCode Code, const TCHAR* Message, const FString& Parameter = {})
	{
		FVoxelLandError E;
		E.Code = Code;
		E.Message = Message;
		E.Parameter = Parameter;
		return E;
	}

	static bool NameOf(const FString& In, FString& Out)
	{
		if (In.IsEmpty())
		{
			return false;
		}
		Out.Reset();
		for (TCHAR C : In)
		{
			if (C >= 'A' && C <= 'Z')
			{
				C += 'a' - 'A';
			}
			if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_'))
			{
				return false;
			}
			Out.AppendChar(C);
		}
		return true;
	}

	static FVoxelLandError NativeError(const FString& Message)
	{
		FVoxelLandError E;
		E.Code = Message.StartsWith(TEXT("NativeLibraryUnavailable:")) ? EVoxelLandErrorCode::NativeLibraryUnavailable
			   : Message.StartsWith(TEXT("InvalidNoiseInput:"))        ? EVoxelLandErrorCode::NonFiniteCoordinate
																	   : EVoxelLandErrorCode::BackendUnavailable;
		E.Message = Message;
		return E;
	}
};

FVoxelLandWorkerContextImpl::FVoxelLandWorkerContextImpl() = default;
FVoxelLandWorkerContextImpl::~FVoxelLandWorkerContextImpl() = default;

FVoxelLandWorkerContext::FVoxelLandWorkerContext() = default;

FVoxelLandWorkerContext::~FVoxelLandWorkerContext()
{
	check(!Impl || Impl->Thread == FPlatformTLS::GetCurrentThreadId());
}

bool FVoxelLandGenerator::OwnsWorkerOnCurrentThread(const FVoxelLandWorkerContext& Worker) const
{
	return !IsInGameThread() && Worker.Impl && Worker.Impl->Owner.Get() == this
		&& Worker.Impl->Thread == FPlatformTLS::GetCurrentThreadId();
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
	return CreateInternal(Settings, nullptr, false, nullptr, OutGenerator);
}

FVoxelLandM5Diagnostic FVoxelLandGenerator::CreateMetered(const FVoxelLandGeneratorSettings& Settings,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
	return CreateMeteredInternal(
		Settings, nullptr, false, nullptr, nullptr, nullptr, Budget, IsCancelled, OutGenerator);
}

FVoxelLandM5Diagnostic FVoxelLandGenerator::CreateMeteredInternal(const FVoxelLandGeneratorSettings& Settings,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
	const FVoxelLandM4OverhangConfig* Overhang, const FVoxelLandM4SmoothSettings* Smooth,
	const FVoxelLandM4HeightDetailConfig* Detail, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
	using E = EVoxelLandM5ErrorCode;
	FVoxelLandM5Diagnostic Failure;
	Failure.Stage = TEXT("M5.GeneratorSetup");
	if (IsCancelled())
	{
		Failure.Code = E::Cancelled;
		Failure.Message = TEXT("Generator setup cancelled");
		return Failure;
	}
	if (IsInGameThread() || !Settings.Program.IsValid() || !Settings.Program->M5Lease.IsValid()
		|| !Budget->OwnsLease(*Settings.Program->M5Lease))
	{
		Failure.Code = E::InvalidInput;
		Failure.Message = TEXT("Generator requires a same-budget metered program and a worker thread");
		return Failure;
	}
	const FVoxelLandProgramDraft& Draft = Settings.Program->GetData();
	const int64 Parameters = Draft.Parameters.Num();
	const int64 Seeds = Draft.Seeds.Num();
	const int64 Noise = Draft.Noise.Num();
	const int64 Overrides = int64(Settings.Profile.FloatOverrides.Num()) + Settings.Profile.IntOverrides.Num()
						  + Settings.Profile.BoolOverrides.Num();
	// These limits match metered program validation.
	if (Parameters > 4096 || Seeds > 4096 || Noise > 4096 || Overrides > 4096 || !Draft.ExternalOperators.IsEmpty()
		|| Draft.ExternalFilterIndex != INDEX_NONE)
	{
		Failure.Code = E::InvalidInput;
		Failure.Message = TEXT("Generator setup exceeds supported metered inputs");
		return Failure;
	}
	if (!Budget->TryChargeWork(Overrides + 1))
	{
		Failure.Code = E::BudgetExceeded;
		Failure.Message = TEXT("Generator input inspection budget exhausted");
		return Failure;
	}
	// Inspect lengths without copying names. NameOf later validates each character.
	for (const TPair<FString, float>& Entry : Settings.Profile.FloatOverrides)
	{
		if (IsCancelled())
		{
			Failure.Code = E::Cancelled;
			Failure.Message = TEXT("Generator setup cancelled");
			return Failure;
		}
		if (Entry.Key.Len() > 256)
		{
			Failure.Code = E::InvalidInput;
			Failure.Message = TEXT("Generator override name too long");
			return Failure;
		}
	}
	for (const TPair<FString, int32>& Entry : Settings.Profile.IntOverrides)
	{
		if (IsCancelled())
		{
			Failure.Code = E::Cancelled;
			Failure.Message = TEXT("Generator setup cancelled");
			return Failure;
		}
		if (Entry.Key.Len() > 256)
		{
			Failure.Code = E::InvalidInput;
			Failure.Message = TEXT("Generator override name too long");
			return Failure;
		}
	}
	for (const TPair<FString, bool>& Entry : Settings.Profile.BoolOverrides)
	{
		if (IsCancelled())
		{
			Failure.Code = E::Cancelled;
			Failure.Message = TEXT("Generator setup cancelled");
			return Failure;
		}
		if (Entry.Key.Len() > 256)
		{
			Failure.Code = E::InvalidInput;
			Failure.Message = TEXT("Generator override name too long");
			return Failure;
		}
	}
	// Bounded counts make all terms representable. Work includes worst-case
	// quadratic hash-table comparisons of bounded names; bytes cover retained
	// profile/arrays and temporary maps, excluding allocator overhead.
	const int64 Names = Parameters + Overrides + 1;
	const int64 Work = 65536 + Names * Names * 512 + Seeds * 64 + Noise * 64;
	const int64 Bytes = 65536 + Names * 8192 + Seeds * 64 + Noise * 64;
	if (!Budget->TryChargeWork(Work))
	{
		Failure.Code = E::BudgetExceeded;
		Failure.Message = TEXT("Generator setup work budget exhausted");
		return Failure;
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(Bytes, Lease))
	{
		Failure.Code = E::BudgetExceeded;
		Failure.Message = TEXT("Generator setup memory budget exhausted");
		return Failure;
	}
	if (IsCancelled())
	{
		Failure.Code = E::Cancelled;
		Failure.Message = TEXT("Generator setup cancelled");
		return Failure;
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Result;
	const FVoxelLandError Error =
		CreateInternal(Settings, MoveTemp(HeightGrid), bNormalizeSlope, Overhang, Result, &IsCancelled);
	if (Error || !Result.IsValid())
	{
		Failure.Code = Error.Code == EVoxelLandErrorCode::Cancelled ? E::Cancelled : E::InvalidInput;
		Failure.Message = Error.Message;
		return Failure;
	}
	if (IsCancelled())
	{
		Failure.Code = E::Cancelled;
		Failure.Message = TEXT("Generator setup cancelled");
		return Failure;
	}
	// Create produces an unpublished mutable object. Install the lease before
	// publishing so it outlives all generator-owned allocations.
	TSharedPtr<FVoxelLandGenerator, ESPMode::ThreadSafe> Mutable = ConstCastSharedPtr<FVoxelLandGenerator>(Result);
	if (Smooth && Detail)
	{
		Mutable->bSmoothEnabled = Smooth->bEnabled;
		Mutable->SmoothStrength = Smooth->Strength;
		Mutable->M4HeightDetail = *Detail;
		const uint32 Bits =
			uint32(VoxelLandDeterminism::DeriveNodeSeed(Settings.WorldSeed, Detail->NodeId, Detail->Salt));
		Mutable->M4HeightDetailSeed = Bits <= uint32(MAX_int32) ? int32(Bits) : int32(int64(Bits) - 4294967296ll);
	}
	if (IsCancelled())
	{
		Failure.Code = E::Cancelled;
		Failure.Message = TEXT("Generator setup cancelled");
		return Failure;
	}
	Mutable->M5Lease = MoveTemp(Lease);
	OutGenerator = MoveTemp(Result);
	return {};
}

FVoxelLandM5Diagnostic FVoxelLandGenerator::CreateM4HeightFieldMetered(const FVoxelLandGeneratorSettings& Settings,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
	const FVoxelLandM4OverhangConfig& Overhang, const FVoxelLandM4SmoothSettings& Smooth,
	const FVoxelLandM4HeightDetailConfig& Detail, TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget,
	TFunctionRef<bool()> IsCancelled, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
	FVoxelLandM5Diagnostic Failure;
	Failure.Stage = TEXT("M5.GeneratorSetup");
	if (IsCancelled())
	{
		Failure.Code = EVoxelLandM5ErrorCode::Cancelled;
		Failure.Message = TEXT("Generator setup cancelled");
		return Failure;
	}
	const float Size = Settings.Profile.VoxelSizeCm;
	if (!HeightGrid || !Settings.Program || !VoxelLandM4Overhang::IsValid(Overhang.Displacement)
		|| !FMath::IsFinite(Overhang.FrequencyPerCm) || Overhang.FrequencyPerCm <= 0.f || Overhang.FrequencyPerCm > 1.f
		|| !Overhang.NodeId.IsValid()
		|| (Settings.bM4FixedLatticeShaping && Overhang.Displacement.bEnabled && Overhang.Displacement.AmplitudeQ8 != 0
			&& !VoxelLandM4FixedLattice::IsShapingFrequencySupported(Overhang.FrequencyPerCm))
		|| !FMath::IsFinite(Size) || Size < 1.f || Size > 2000000.f
		|| FMath::FloorToDouble(double(Size)) != double(Size)
		|| Settings.Program->GetData().ExternalFilterIndex != INDEX_NONE
		|| !Settings.Program->GetData().ExternalOperators.IsEmpty() || Smooth.Version != 1
		|| !FMath::IsFinite(Smooth.Strength) || Smooth.Strength < 0.f || Smooth.Strength > 1.f
		|| Detail.AlgorithmVersion != 1 || Detail.AmplitudeQ8 < 0 || Detail.AmplitudeQ8 > 1280000
		|| !FMath::IsFinite(Detail.FrequencyPerCm) || Detail.FrequencyPerCm <= 0.f || Detail.FrequencyPerCm > 1.f
		|| !Detail.NodeId.IsValid()
		|| (Settings.bM4FixedLatticeShaping && Detail.bEnabled && Detail.AmplitudeQ8 != 0
			&& !VoxelLandM4FixedLattice::IsShapingFrequencySupported(Detail.FrequencyPerCm)))
	{
		Failure.Code = EVoxelLandM5ErrorCode::InvalidInput;
		Failure.Message = TEXT("Invalid finite M4 height-field configuration");
		return Failure;
	}
	return CreateMeteredInternal(Settings, MoveTemp(HeightGrid), bNormalizeSlope, &Overhang, &Smooth, &Detail, Budget,
		IsCancelled, OutGenerator);
}

FVoxelLandError FVoxelLandGenerator::CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
	return CreateM4HeightField(
		Settings, MoveTemp(HeightGrid), bNormalizeSlope, FVoxelLandM4OverhangConfig{}, OutGenerator);
}

FVoxelLandError FVoxelLandGenerator::CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
	const FVoxelLandM4OverhangConfig& Overhang,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
	if (!VoxelLandM4Overhang::IsValid(Overhang.Displacement) || !FMath::IsFinite(Overhang.FrequencyPerCm)
		|| Overhang.FrequencyPerCm <= 0.f || Overhang.FrequencyPerCm > 1.f || !Overhang.NodeId.IsValid())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 overhang configuration"));
	}
	if (Settings.bM4FixedLatticeShaping && Overhang.Displacement.bEnabled && Overhang.Displacement.AmplitudeQ8 != 0
		&& !VoxelLandM4FixedLattice::IsShapingFrequencySupported(Overhang.FrequencyPerCm))
	{
		return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidParameter,
			TEXT("Fixed-lattice overhang frequency must be representable in Q24"));
	}
	if (!HeightGrid)
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M4 height grid is required"));
	}
	const float Size = Settings.Profile.VoxelSizeCm;
	if (!FMath::IsFinite(Size) || Size < 1.f || Size > 2000000.f || FMath::FloorToDouble(double(Size)) != double(Size))
	{
		return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidProfile,
			TEXT("M4 voxel size must be an integral centimeter value in [1, 2000000]"));
	}
	if (!Settings.Program || Settings.Program->GetData().ExternalFilterIndex != INDEX_NONE
		|| !Settings.Program->GetData().ExternalOperators.IsEmpty())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidProgram, TEXT("M4 height fields do not support external operators or filters"));
	}
	// Build locally so failure never changes a caller's existing generator.
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Result;
	if (FVoxelLandError Error = CreateInternal(Settings, MoveTemp(HeightGrid), bNormalizeSlope, &Overhang, Result))
	{
		return Error;
	}
	OutGenerator = MoveTemp(Result);
	return {};
}

FVoxelLandError FVoxelLandGenerator::CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
	const FVoxelLandM4OverhangConfig& Overhang, const FVoxelLandM4SmoothSettings& Smooth,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
	if (Smooth.Version != 1 || !FMath::IsFinite(Smooth.Strength) || Smooth.Strength < 0.f || Smooth.Strength > 1.f)
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or unsupported M4 smooth filter settings"));
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Result;
	if (FVoxelLandError Error = CreateM4HeightField(Settings, MoveTemp(HeightGrid), bNormalizeSlope, Overhang, Result))
	{
		return Error;
	}
	// Result has not been published and was created as a mutable generator.
	TSharedPtr<FVoxelLandGenerator, ESPMode::ThreadSafe> Mutable = ConstCastSharedPtr<FVoxelLandGenerator>(Result);
	Mutable->bSmoothEnabled = Smooth.bEnabled;
	Mutable->SmoothStrength = Smooth.Strength;
	OutGenerator = MoveTemp(Result);
	return {};
}

FVoxelLandError FVoxelLandGenerator::CreateM4Unlimited(const FVoxelLandGeneratorSettings& Settings,
	const FVoxelLandM4UnlimitedConfig& Config, TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
	if (Settings.M6Spheres.IsActive())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M6 local terrain requires the ordered stamp-list settings"));
	}
	if ((Config.Overhang.Displacement.bEnabled && Config.Overhang.Displacement.AmplitudeQ8 != 0
			&& !VoxelLandM4FixedLattice::IsShapingFrequencySupported(Config.Overhang.FrequencyPerCm))
		|| (Config.HeightDetail.bEnabled && Config.HeightDetail.AmplitudeQ8 != 0
			&& !VoxelLandM4FixedLattice::IsShapingFrequencySupported(Config.HeightDetail.FrequencyPerCm)))
	{
		return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidParameter,
			TEXT("Fixed-lattice shaping frequency must be representable in Q24"));
	}
	const auto& Detail = Config.HeightDetail;
	if (Detail.AlgorithmVersion != 1 || Detail.AmplitudeQ8 < 0 || Detail.AmplitudeQ8 > 1280000
		|| !FMath::IsFinite(Detail.FrequencyPerCm) || Detail.FrequencyPerCm <= 0.f || Detail.FrequencyPerCm > 1.f
		|| !Detail.NodeId.IsValid())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 height detail settings"));
	}
	if (IsInGameThread())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("M4 local generator setup requires a worker thread"));
	}
	const float Size = Settings.Profile.VoxelSizeCm;
	if (!FMath::IsFinite(Size) || Size < 1.f || Size > 134217.f || FMath::FloorToDouble(double(Size)) != double(Size)
		|| !Settings.Program || Settings.Program->GetData().ExternalFilterIndex != INDEX_NONE
		|| !Settings.Program->GetData().ExternalOperators.IsEmpty())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 local terrain settings or unsupported provider"));
	}
	if (!VoxelLandM4Overhang::IsValid(Config.Overhang.Displacement) || !FMath::IsFinite(Config.Overhang.FrequencyPerCm)
		|| Config.Overhang.FrequencyPerCm <= 0.f || Config.Overhang.FrequencyPerCm > 1.f
		|| !Config.Overhang.NodeId.IsValid())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 local overhang settings"));
	}
	if (Config.SmoothOverride.IsSet()
		&& (Config.SmoothOverride->Version != 1 || !FMath::IsFinite(Config.SmoothOverride->Strength)
			|| Config.SmoothOverride->Strength < 0.f || Config.SmoothOverride->Strength > 1.f))
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or unsupported M4 smooth filter settings"));
	}
	const TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Controls[] = {
		Config.ContinentalGenerator, Config.MountainGenerator, Config.PeaksGenerator};
	if (Config.AuthoredHeightGenerator)
	{
		if (Config.HeightScaleCm < 1 || Config.HeightScaleCm > 2000000)
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid Height Scale"));
		}
		if (const FVoxelLandError Error = VoxelLandM4FixedLattice::Validate(*Config.AuthoredHeightGenerator))
		{
			return Error;
		}
	}
	else
	{
		if (!Config.ContinentalCurve || !Config.MountainCurve || !Config.PeaksCurve)
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidParameter, TEXT("M4 local control curves are required"));
		}
		for (const auto& Control : Controls)
		{
			if (!Control || Control->M4HeightGrid || Control->M4Unlimited)
			{
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::InvalidParameter, TEXT("M4 local controls must be ordinary VM generators"));
			}
			const FVoxelLandError Error = VoxelLandM4FixedLattice::Validate(*Control);
			if (Error)
			{
				return Error;
			}
		}
	}
	// The chunk-local backend produces the M4 baseline using only these inputs.
	FVoxelLandGeneratorSettings LocalSettings = Settings;
	LocalSettings.M6Spheres = {};
	LocalSettings.M6Stamps = {};
	LocalSettings.M6Filters = {};
	LocalSettings.M6Redistance = {};
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Result;
	if (FVoxelLandError Error =
			CreateInternal(LocalSettings, nullptr, Config.bNormalizeSlope, &Config.Overhang, Result))
	{
		return Error;
	}
	// CreateInternal allocates a mutable object; no reference has been published.
	TSharedPtr<FVoxelLandGenerator, ESPMode::ThreadSafe> Mutable = ConstCastSharedPtr<FVoxelLandGenerator>(Result);
	if (Settings.M6Stamps.IsActive())
	{
		if (const FVoxelLandError Error = Mutable->InitializeM6Stamps(Settings.M6Stamps, Size))
		{
			return Error;
		}
	}
	if (Settings.M6Filters.IsActive())
	{
		if (const FVoxelLandError Error = Mutable->InitializeM6Filters(Settings.M6Filters, Size))
		{
			return Error;
		}
	}
	if (Settings.M6Redistance.bEnabled)
	{
		if (const FVoxelLandError Error = Mutable->InitializeM6Redistance(Settings.M6Redistance, Size))
		{
			return Error;
		}
	}
	Mutable->bM4FixedLatticeShaping = true;
	Mutable->M4HeightDetail = Config.HeightDetail;
	Mutable->M4HeightDetailSeed = FVoxelLandGeneratorValidation::SignedSeed(
		Settings.WorldSeed, Config.HeightDetail.NodeId, Config.HeightDetail.Salt);
	if (Config.SmoothOverride.IsSet())
	{
		Mutable->bSmoothEnabled = Config.SmoothOverride->bEnabled;
		Mutable->SmoothStrength = Config.SmoothOverride->Strength;
	}
	Mutable->M4Unlimited = MakeShared<FVoxelLandM4UnlimitedConfig, ESPMode::ThreadSafe>(Config);
	Mutable->M4UnlimitedSettings =
		MakeShared<FVoxelLandGeneratorSettings, ESPMode::ThreadSafe>(MoveTemp(LocalSettings));
	OutGenerator = MoveTemp(Result);
	return {};
}

FVoxelLandError FVoxelLandGenerator::CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
	const FVoxelLandM4OverhangConfig& Overhang, const FVoxelLandM4HeightDetailConfig& Detail,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
	if (Detail.AlgorithmVersion != 1 || Detail.AmplitudeQ8 < 0 || Detail.AmplitudeQ8 > 1280000
		|| !FMath::IsFinite(Detail.FrequencyPerCm) || Detail.FrequencyPerCm <= 0.f || Detail.FrequencyPerCm > 1.f
		|| !Detail.NodeId.IsValid())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid M4 height detail settings"));
	}
	if (Settings.bM4FixedLatticeShaping && Detail.bEnabled && Detail.AmplitudeQ8 != 0
		&& !VoxelLandM4FixedLattice::IsShapingFrequencySupported(Detail.FrequencyPerCm))
	{
		return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidParameter,
			TEXT("Fixed-lattice height detail frequency must be representable in Q24"));
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Result;
	if (FVoxelLandError Error = CreateM4HeightField(Settings, MoveTemp(HeightGrid), bNormalizeSlope, Overhang, Result))
	{
		return Error;
	}
	auto Mutable = ConstCastSharedPtr<FVoxelLandGenerator>(Result);
	Mutable->M4HeightDetail = Detail;
	Mutable->M4HeightDetailSeed =
		FVoxelLandGeneratorValidation::SignedSeed(Settings.WorldSeed, Detail.NodeId, Detail.Salt);
	OutGenerator = MoveTemp(Result);
	return {};
}

FVoxelLandError FVoxelLandGenerator::CreateM4HeightField(const FVoxelLandGeneratorSettings& Settings,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
	const FVoxelLandM4OverhangConfig& Overhang, const FVoxelLandM4SmoothSettings& Smooth,
	const FVoxelLandM4HeightDetailConfig& Detail,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator)
{
	if (Smooth.Version != 1 || !FMath::IsFinite(Smooth.Strength) || Smooth.Strength < 0.f || Smooth.Strength > 1.f)
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or unsupported M4 smooth filter settings"));
	}
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe> Result;
	if (FVoxelLandError Error =
			CreateM4HeightField(Settings, MoveTemp(HeightGrid), bNormalizeSlope, Overhang, Detail, Result))
	{
		return Error;
	}
	auto Mutable = ConstCastSharedPtr<FVoxelLandGenerator>(Result);
	Mutable->bSmoothEnabled = Smooth.bEnabled;
	Mutable->SmoothStrength = Smooth.Strength;
	OutGenerator = MoveTemp(Result);
	return {};
}

FVoxelLandError FVoxelLandGenerator::CreateInternal(const FVoxelLandGeneratorSettings& Settings,
	TSharedPtr<const FVoxelLandM4HeightGrid, ESPMode::ThreadSafe> HeightGrid, bool bNormalizeSlope,
	const FVoxelLandM4OverhangConfig* Overhang,
	TSharedPtr<const FVoxelLandGenerator, ESPMode::ThreadSafe>& OutGenerator, TFunctionRef<bool()>* IsCancelled)
{
	const bool bLegacySpheres = Settings.M6Spheres.IsActive();
	const bool bGenericStamps = Settings.M6Stamps.IsActive();
	const bool bRegionFilters = Settings.M6Filters.IsActive();
	const bool bRedistance = Settings.M6Redistance.bEnabled;
	if (bLegacySpheres && bGenericStamps)
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("Only one M6 stamp list can be active"));
	}
	if ((bLegacySpheres || bGenericStamps || bRegionFilters || bRedistance) && (!HeightGrid || IsCancelled))
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidParameter, TEXT("M6 settings require an ordinary supported M4 generator"));
	}
	OutGenerator.Reset();
	if (IsInGameThread())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Generator setup requires a worker thread"));
	}
	const TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe>& Program = Settings.Program;
	const FVoxelLandZoneProfile& Profile = Settings.Profile;
	const FVoxelLandBackendOptions& Options = Settings.Backends;
	const uint64 WorldSeed = Settings.WorldSeed;
	if (!Program.IsValid())
	{
		return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidProgram, TEXT("Program is null"));
	}
	if (!FMath::IsFinite(Profile.VoxelSizeCm) || Profile.VoxelSizeCm <= 0)
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidProfile, TEXT("Voxel size must be positive and finite"));
	}
	TSharedPtr<FVoxelLandGenerator, ESPMode::ThreadSafe> Result = MakeShareable(new FVoxelLandGenerator);
	Result->Program = Program;
	if (Settings.DirectDensityBounds.IsSet() && (HeightGrid || !Settings.DirectDensityBounds->IsValid()))
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidProfile, TEXT("Invalid finite direct-density bounds"));
	}
	Result->DirectDensityBounds = Settings.DirectDensityBounds;
	Result->DirectDensitySeed = WorldSeed;
	Result->M4HeightGrid = MoveTemp(HeightGrid);
	if (bLegacySpheres)
	{
		if (const FVoxelLandError Error = Result->InitializeM6Spheres(Settings.M6Spheres))
		{
			return Error;
		}
	}
	if (bGenericStamps)
	{
		if (const FVoxelLandError Error = Result->InitializeM6Stamps(Settings.M6Stamps, Profile.VoxelSizeCm))
		{
			return Error;
		}
	}
	if (bRegionFilters)
	{
		if (const FVoxelLandError Error = Result->InitializeM6Filters(Settings.M6Filters, Profile.VoxelSizeCm))
		{
			return Error;
		}
	}
	if (bRedistance)
	{
		if (const FVoxelLandError Error = Result->InitializeM6Redistance(Settings.M6Redistance, Profile.VoxelSizeCm))
		{
			return Error;
		}
	}
	Result->bM4FixedLatticeShaping = Result->M4HeightGrid.IsValid() && Settings.bM4FixedLatticeShaping;
	Result->bM4NormalizeSlope = bNormalizeSlope;
	if (Overhang)
	{
		Result->M4Overhang = *Overhang;
		Result->M4OverhangSeed =
			FVoxelLandGeneratorValidation::SignedSeed(Settings.WorldSeed, Overhang->NodeId, Overhang->Salt);
	}
	if (IsCancelled)
	{
		// Copy each override during its validation loop so cancellation is
		// observed between bounded entries, rather than after a bulk map copy.
		Result->Profile.VoxelSizeCm = Profile.VoxelSizeCm;
	}
	else
	{
		Result->Profile = Profile;
	}
	Result->Options = Options;
	const FVoxelLandProgramDraft& D = Program->GetData();
	TMap<FString, int32> Slots;
	Result->FloatParameters.SetNumZeroed(D.Parameters.Num());
	Result->IntParameters.SetNumZeroed(D.Parameters.Num());
	for (int32 ParameterIndex = 0; ParameterIndex < D.Parameters.Num(); ++ParameterIndex)
	{
		if (IsCancelled && (*IsCancelled)())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::Cancelled, TEXT("Generator setup cancelled"));
		}
		const FVoxelLandParameterSlot& P = D.Parameters[ParameterIndex];
		Slots.Add(P.Name, ParameterIndex);
		Result->FloatParameters[ParameterIndex] = P.FloatDefault;
		Result->IntParameters[ParameterIndex] = P.IntDefault;
	}
	TSet<FString> Seen;
	for (const TPair<FString, float>& Entry : Profile.FloatOverrides)
	{
		if (IsCancelled && (*IsCancelled)())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::Cancelled, TEXT("Generator setup cancelled"));
		}
		FString Name;
		if (!FVoxelLandGeneratorValidation::NameOf(Entry.Key, Name) || Seen.Contains(Name))
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or duplicate override"), Entry.Key);
		}
		Seen.Add(Name);
		const int32* Slot = Slots.Find(Name);
		if (!Slot || D.Parameters[*Slot].Type != EVoxelLandParameterType::Float || !FMath::IsFinite(Entry.Value))
		{
			return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidParameter,
				TEXT("Unknown, wrong type, or non-finite float override"), Entry.Key);
		}
		Result->FloatParameters[*Slot] = Entry.Value;
		if (IsCancelled)
		{
			Result->Profile.FloatOverrides.Add(Entry.Key, Entry.Value);
		}
	}
	for (const TPair<FString, int32>& Entry : Profile.IntOverrides)
	{
		if (IsCancelled && (*IsCancelled)())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::Cancelled, TEXT("Generator setup cancelled"));
		}
		FString Name;
		if (!FVoxelLandGeneratorValidation::NameOf(Entry.Key, Name) || Seen.Contains(Name))
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or duplicate override"), Entry.Key);
		}
		Seen.Add(Name);
		const int32* Slot = Slots.Find(Name);
		if (!Slot || D.Parameters[*Slot].Type != EVoxelLandParameterType::Int32)
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidParameter, TEXT("Unknown or wrong type int override"), Entry.Key);
		}
		Result->IntParameters[*Slot] = Entry.Value;
		if (IsCancelled)
		{
			Result->Profile.IntOverrides.Add(Entry.Key, Entry.Value);
		}
	}
	for (const TPair<FString, bool>& Entry : Profile.BoolOverrides)
	{
		if (IsCancelled && (*IsCancelled)())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::Cancelled, TEXT("Generator setup cancelled"));
		}
		FString Name;
		if (!FVoxelLandGeneratorValidation::NameOf(Entry.Key, Name) || Seen.Contains(Name))
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidParameter, TEXT("Invalid or duplicate override"), Entry.Key);
		}
		Seen.Add(Name);
		const int32* Slot = Slots.Find(Name);
		if (!Slot || D.Parameters[*Slot].Type != EVoxelLandParameterType::Bool)
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidParameter, TEXT("Unknown or wrong type bool override"), Entry.Key);
		}
		Result->IntParameters[*Slot] = Entry.Value ? 1 : 0;
		if (IsCancelled)
		{
			Result->Profile.BoolOverrides.Add(Entry.Key, Entry.Value);
		}
	}
	for (const FVoxelLandSeedSlot& Seed : D.Seeds)
	{
		if (IsCancelled && (*IsCancelled)())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::Cancelled, TEXT("Generator setup cancelled"));
		}
		const uint64 FullSeed = VoxelLandDeterminism::DeriveNodeSeed(WorldSeed, Seed.NodeId, Seed.Salt);
		Result->FullSeeds.Add(FullSeed);
		const uint32 Bits = uint32(FullSeed);
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
		if (IsCancelled && (*IsCancelled)())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::Cancelled, TEXT("Generator setup cancelled"));
		}
		const float Frequency = FloatValue(N.Frequency), Gain = FloatValue(N.Gain),
					Lacunarity = FloatValue(N.Lacunarity);
		const int32 Octaves = IntValue(N.Octaves);
		if (!FMath::IsFinite(Frequency) || Frequency <= 0 || !FMath::IsFinite(Gain) || Gain < 0 || Gain > 1
			|| !FMath::IsFinite(Lacunarity) || Lacunarity < 1 || Octaves < 1 || Octaves > 16)
		{
			return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidParameter,
				TEXT("Noise frequency, octaves, gain or lacunarity out of range"));
		}
	}
	if (D.SmoothFilter.bPresent)
	{
		const FVoxelLandValueSource& Enabled = D.SmoothFilter.Enabled;
		const FVoxelLandValueSource& Strength = D.SmoothFilter.Strength;
		Result->bSmoothEnabled =
			Enabled.bParameter ? Result->IntParameters[Enabled.Index] != 0 : D.Constants[Enabled.Index] != 0.f;
		Result->SmoothStrength =
			Strength.bParameter ? Result->FloatParameters[Strength.Index] : D.Constants[Strength.Index];
		if (!FMath::IsFinite(Result->SmoothStrength) || Result->SmoothStrength < 0.f || Result->SmoothStrength > 1.f)
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidParameter, TEXT("Smooth strength must be in [0,1]"));
		}
	}
	OutGenerator = Result;
	return {};
}

class FVoxelLandGeneratorBounds final
{
public:
	static FVoxelLandError SelectVMBackend(EVoxelLandVMBackend Requested, EVoxelLandVMBackend& Selected)
	{
		if (Requested != EVoxelLandVMBackend::Auto && Requested != EVoxelLandVMBackend::Scalar
			&& Requested != EVoxelLandVMBackend::Simd4)
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::BackendUnavailable, TEXT("Unknown VM backend"));
		}
#if VOXELLAND_HAS_VM_SIMD
		Selected = Requested == EVoxelLandVMBackend::Scalar ? EVoxelLandVMBackend::Scalar : EVoxelLandVMBackend::Simd4;
#else
		if (Requested == EVoxelLandVMBackend::Simd4)
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::BackendUnavailable, TEXT("SIMD VM is unavailable"));
		}
		Selected = EVoxelLandVMBackend::Scalar;
#endif
		return {};
	}
};

FVoxelLandError FVoxelLandWorkerContextImpl::InitializeNoise(const FVoxelLandProgramDraft& Program,
	const FVoxelLandBackendOptions& Options, const TArray<float>& FloatParameters, const TArray<int32>& IntParameters)
{
	NoiseIndices.Init(INDEX_NONE, Program.Noise.Num() * FVoxelLandGeneratorValidation::NoiseFamilyCount);
	TArray<VoxelLandNative::FConfig> Configs;
	for (const FVoxelLandInstruction& Instruction : Program.Instructions)
	{
		const int32 Variant = FVoxelLandGeneratorValidation::NoiseVariant(Instruction.Op);
		if (Variant == INDEX_NONE)
		{
			continue;
		}
		const int32 Lookup = int32(Instruction.Imm) * FVoxelLandGeneratorValidation::NoiseFamilyCount + Variant;
		if (NoiseIndices[Lookup] != INDEX_NONE)
		{
			continue;
		}
		const FVoxelLandNoiseConfig& NoiseConfig = Program.Noise[Instruction.Imm];
		VoxelLandNative::FConfig Config;
		Config.Family = EVoxelLandNoiseFamily(Variant);
		Config.Octaves = NoiseConfig.Octaves.bParameter ? IntParameters[NoiseConfig.Octaves.Index]
														: int32(Program.Constants[NoiseConfig.Octaves.Index]);
		Config.Gain = NoiseConfig.Gain.bParameter ? FloatParameters[NoiseConfig.Gain.Index]
												  : Program.Constants[NoiseConfig.Gain.Index];
		Config.Lacunarity = NoiseConfig.Lacunarity.bParameter ? FloatParameters[NoiseConfig.Lacunarity.Index]
															  : Program.Constants[NoiseConfig.Lacunarity.Index];
		NoiseIndices[Lookup] = Configs.Add(Config);
	}
	if (Configs.IsEmpty())
	{
		return {};
	}

	VoxelLandNative::EBackend Backend = VoxelLandNative::EBackend::Auto;
	if (Options.Noise == EVoxelLandNoiseBackend::Scalar)
	{
		Backend = VoxelLandNative::EBackend::Scalar;
	}
	else if (Options.Noise == EVoxelLandNoiseBackend::SSE2)
	{
		Backend = VoxelLandNative::EBackend::SSE2;
	}
	else if (Options.Noise != EVoxelLandNoiseBackend::Auto)
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::BackendUnavailable, TEXT("Unknown noise backend"));
	}
	FString Error;
	Noise = VoxelLandNative::FWorker::Create(Backend, Configs, Error);
	if (!Noise)
	{
		return FVoxelLandGeneratorValidation::NativeError(Error);
	}
	NoiseBackend = Noise->GetBackend() == VoxelLandNative::EBackend::SSE2 ? EVoxelLandNoiseBackend::SSE2
																		  : EVoxelLandNoiseBackend::Scalar;
	return {};
}

void FVoxelLandWorkerContextImpl::AllocateScratch(int32 RegisterCount)
{
	Registers.SetNumUninitialized(RegisterCount * FVoxelLandGeneratorValidation::BatchSize);
	IntRegisters.SetNumUninitialized(RegisterCount * FVoxelLandGeneratorValidation::BatchSize);
	X.SetNumUninitialized(FVoxelLandGeneratorValidation::BatchSize);
	Y.SetNumUninitialized(FVoxelLandGeneratorValidation::BatchSize);
	Z.SetNumUninitialized(FVoxelLandGeneratorValidation::BatchSize);
	Output.SetNumUninitialized(FVoxelLandGeneratorValidation::BatchSize);
	NoiseX.SetNumUninitialized(FVoxelLandGeneratorValidation::BatchSize);
	NoiseY.SetNumUninitialized(FVoxelLandGeneratorValidation::BatchSize);
	NoiseZ.SetNumUninitialized(FVoxelLandGeneratorValidation::BatchSize);
}

FVoxelLandError FVoxelLandGenerator::CreateWorker(TUniquePtr<FVoxelLandWorkerContext>& OutWorker) const
{
	if (IsInGameThread())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Worker setup requires a worker thread"));
	}
	if (OutWorker && OutWorker->Impl && OutWorker->Impl->Thread != FPlatformTLS::GetCurrentThreadId())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidContext, TEXT("Existing worker belongs to a different thread"));
	}
	// Destroy existing worker-owned resources only on the thread that created them.
	OutWorker.Reset();
	EVoxelLandVMBackend SelectedVM = EVoxelLandVMBackend::Scalar;
	if (FVoxelLandError Error = FVoxelLandGeneratorBounds::SelectVMBackend(Options.VM, SelectedVM))
	{
		return Error;
	}

	TUniquePtr<FVoxelLandWorkerContext> Worker(new FVoxelLandWorkerContext);
	Worker->Impl = MakeUnique<FVoxelLandWorkerContextImpl>();
	FVoxelLandWorkerContextImpl& W = *Worker->Impl;
	W.Owner = AsShared();
	W.Thread = FPlatformTLS::GetCurrentThreadId();
	W.VM = SelectedVM;
	if (M4Unlimited)
	{
		OutWorker = MoveTemp(Worker);
		return {};
	}
	if (M4HeightGrid)
	{
		// Native noise resources are created and destroyed on this worker's thread.
		if (!bM4FixedLatticeShaping
			&& ((M4Overhang.Displacement.bEnabled && M4Overhang.Displacement.AmplitudeQ8 != 0)
				|| (M4HeightDetail.bEnabled && M4HeightDetail.AmplitudeQ8 != 0)))
		{
			VoxelLandNative::EBackend Backend = VoxelLandNative::EBackend::Auto;
			if (Options.Noise == EVoxelLandNoiseBackend::Scalar)
			{
				Backend = VoxelLandNative::EBackend::Scalar;
			}
			else if (Options.Noise == EVoxelLandNoiseBackend::SSE2)
			{
				Backend = VoxelLandNative::EBackend::SSE2;
			}
			else if (Options.Noise != EVoxelLandNoiseBackend::Auto)
			{
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::BackendUnavailable, TEXT("Unknown noise backend"));
			}
			VoxelLandNative::FConfig Config;
			Config.Family = EVoxelLandNoiseFamily::Perlin;
			TArray<VoxelLandNative::FConfig> Configs;
			Configs.Add(Config);
			FString Error;
			W.Noise = VoxelLandNative::FWorker::Create(Backend, Configs, Error);
			if (!W.Noise)
			{
				return FVoxelLandGeneratorValidation::NativeError(Error);
			}
			W.NoiseBackend = W.Noise->GetBackend() == VoxelLandNative::EBackend::SSE2 ? EVoxelLandNoiseBackend::SSE2
																					  : EVoxelLandNoiseBackend::Scalar;
		}
		OutWorker = MoveTemp(Worker);
		return {};
	}
	const FVoxelLandProgramDraft& ProgramData = Program->GetData();
	if (FVoxelLandError Error = W.InitializeNoise(ProgramData, Options, FloatParameters, IntParameters))
	{
		return Error;
	}
	for (int32 Index = 0; Index < ProgramData.ExternalOperators.Num(); ++Index)
	{
		const FVoxelLandExternalOperator& Operator = ProgramData.ExternalOperators[Index];
		FString Error;
		TUniquePtr<IVoxelLandOperatorWorker> OperatorWorker =
			Program->GetOperators()[Index]->CreateWorker(Operator.Configuration, FullSeeds[Operator.SeedSlot], Error);
		if (!OperatorWorker)
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidProgram, TEXT("External operator worker creation failed"), Error);
		}
		W.OperatorWorkers.Add(MoveTemp(OperatorWorker));
		const EVoxelLandOperatorSimdRequirement Requirement = Program->GetOperators()[Index]->SimdRequirement();
		bool bUseSimd =
			SelectedVM == EVoxelLandVMBackend::Simd4 && Requirement == EVoxelLandOperatorSimdRequirement::SSE2;
		TUniquePtr<IVoxelLandOperatorWorker> SimdWorker;
		if (bUseSimd)
		{
			Error.Reset();
			SimdWorker = Program->GetOperators()[Index]->CreateWorker(
				Operator.Configuration, FullSeeds[Operator.SeedSlot], Error);
			if (!SimdWorker)
			{
				bUseSimd = false;
				UE_LOG(LogTemp, Warning, TEXT("VoxelLand external SIMD worker unavailable; using scalar: %s"), *Error);
			}
		}
		W.OperatorSimdWorkers.Add(MoveTemp(SimdWorker));
		W.OperatorSimdEnabled.Add(bUseSimd);
	}
	if (W.OperatorSimdEnabled.Contains(true))
	{
		W.OperatorSimdFloatScratch.SetNumUninitialized(FVoxelLandGeneratorValidation::BatchSize);
		W.OperatorSimdIntScratch.SetNumUninitialized(FVoxelLandGeneratorValidation::BatchSize);
	}
	W.AllocateScratch(ProgramData.RegisterCount);
	OutWorker = MoveTemp(Worker);
	return {};
}

FVoxelLandError FVoxelLandWorkerContextImpl::LoadPositions(TConstArrayView<FVector3f> Positions)
{
	FVoxelLandWorkerContextImpl& W = *this;
	const int32 N = Positions.Num();
	for (int32 SampleIndex = 0; SampleIndex < N; ++SampleIndex)
	{
		const FVector3f P = Positions[SampleIndex];
		if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z))
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Non-finite sample coordinate"));
		}
		W.X[SampleIndex] = P.X;
		W.Y[SampleIndex] = P.Y;
		W.Z[SampleIndex] = P.Z;
	}
	return {};
}

FVoxelLandError FVoxelLandWorkerContextImpl::EvaluateNoise(const FVoxelLandInstruction& I,
	const FVoxelLandProgramDraft& D, const TArray<float>& FloatParameters, const TArray<int32>& Seeds, int32 N,
	const float* A, const float* B, const float* C, float* Dest)
{
	FVoxelLandWorkerContextImpl& W = *this;
	const FVoxelLandNoiseConfig& Config = D.Noise[I.Imm];
	const float Frequency =
		Config.Frequency.bParameter ? FloatParameters[Config.Frequency.Index] : D.Constants[Config.Frequency.Index];
	const bool b3D = FVoxelLandGeneratorValidation::IsNoise3D(I.Op);
	for (int32 SampleIndex = 0; SampleIndex < N; ++SampleIndex)
	{
		W.NoiseX[SampleIndex] = A[SampleIndex] * Frequency;
		W.NoiseY[SampleIndex] = B[SampleIndex] * Frequency;
		W.NoiseZ[SampleIndex] = b3D ? C[SampleIndex] * Frequency : 0.f;
	}
	FString Error;
	if (!W.Noise->Sample(NoiseIndices[int32(I.Imm) * FVoxelLandGeneratorValidation::NoiseFamilyCount
									  + FVoxelLandGeneratorValidation::NoiseVariant(I.Op)],
			b3D, Seeds[Config.SeedSlot], N, W.NoiseX.GetData(), W.NoiseY.GetData(), W.NoiseZ.GetData(), Dest, Error))
	{
		return FVoxelLandGeneratorValidation::NativeError(Error);
	}
	return {};
}

void FVoxelLandWorkerContextImpl::ResolveOperands(
	const FVoxelLandInstruction& I, const float*& A, const float*& B, const float*& C)
{
	FVoxelLandWorkerContextImpl& W = *this;
	// Validation checks only operands read by each opcode. Do not form pointers from unused fields.
	switch (I.Op)
	{
		case EVoxelLandOpcode::Abs:
		case EVoxelLandOpcode::Negate:
		case EVoxelLandOpcode::StoreDensity:
			A = W.Registers.GetData() + int32(I.A) * FVoxelLandGeneratorValidation::BatchSize;
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
		case EVoxelLandOpcode::Value2:
		case EVoxelLandOpcode::Simplex2:
			A = W.Registers.GetData() + int32(I.A) * FVoxelLandGeneratorValidation::BatchSize;
			B = W.Registers.GetData() + int32(I.B) * FVoxelLandGeneratorValidation::BatchSize;
			break;
		case EVoxelLandOpcode::Plane:
		case EVoxelLandOpcode::Sphere:
		case EVoxelLandOpcode::Box:
		case EVoxelLandOpcode::Perlin3:
		case EVoxelLandOpcode::Fbm3:
		case EVoxelLandOpcode::Value3:
		case EVoxelLandOpcode::Simplex3:
			A = W.Registers.GetData() + int32(I.A) * FVoxelLandGeneratorValidation::BatchSize;
			B = W.Registers.GetData() + int32(I.B) * FVoxelLandGeneratorValidation::BatchSize;
			C = W.Registers.GetData() + int32(I.C) * FVoxelLandGeneratorValidation::BatchSize;
			break;
		default:
			break;
	}
}

#if VOXELLAND_HAS_VM_SIMD
int32 FVoxelLandWorkerContextImpl::ExecuteSimd(
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

FVoxelLandError FVoxelLandWorkerContextImpl::ExecuteScalar(const FVoxelLandInstruction& I,
	const FVoxelLandProgramDraft& D, const TArray<float>& FloatParameters, int32 Start, int32 N, const float* A,
	const float* B, const float* C, float* Dest)
{
	FVoxelLandWorkerContextImpl& W = *this;
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
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::InvalidProgram, TEXT("Unsupported opcode during evaluation"));
		}
	}
	return {};
}

FVoxelLandError FVoxelLandWorkerContextImpl::ExecuteExternal(
	const FVoxelLandInstruction& I, const FVoxelLandProgramDraft& D, int32 N)
{
	const FVoxelLandExternalOperator& Operator = D.ExternalOperators[I.Imm];
	const uint16 RegistersUsed[] = {I.A, I.B, I.C};
	TArray<const void*, TInlineAllocator<3>> Inputs;
	for (int32 Index = 0; Index < Operator.Inputs.Num(); ++Index)
	{
		const int32 Offset = int32(RegistersUsed[Index]) * FVoxelLandGeneratorValidation::BatchSize;
		Inputs.Add(Operator.Inputs[Index] == EVoxelLandGraphValueType::Float
					   ? static_cast<const void*>(Registers.GetData() + Offset)
					   : static_cast<const void*>(IntRegisters.GetData() + Offset));
	}
	const int32 Offset = int32(I.Dst) * FVoxelLandGeneratorValidation::BatchSize;
	void* OutputData = Operator.Output == EVoxelLandGraphValueType::Float
						 ? static_cast<void*>(Registers.GetData() + Offset)
						 : static_cast<void*>(IntRegisters.GetData() + Offset);
	FString Error;
	FVoxelLandOperatorOutput Writer(Operator.Output, OutputData, N);
	if (!OperatorWorkers[I.Imm]->Evaluate(Inputs, Writer, N, Error) || !Writer.IsComplete(Error))
	{
		return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidMathDomain,
			TEXT("External operator evaluation failed or output was incomplete"), Error);
	}
	CheckOperatorSimd(I.Imm, Operator.Output, Inputs, OutputData, N);
	return {};
}

void FVoxelLandWorkerContextImpl::CheckOperatorSimd(uint32 OperatorIndex, EVoxelLandGraphValueType OutputType,
	TConstArrayView<const void*> Inputs, void* ScalarOutput, int32 Count)
{
	const int32 SimdCount = Count - (Count % 4);
	if (OperatorSimdEnabled[OperatorIndex] && SimdCount > 0)
	{
		void* SimdOutput = OutputType == EVoxelLandGraphValueType::Float
							 ? static_cast<void*>(OperatorSimdFloatScratch.GetData())
							 : static_cast<void*>(OperatorSimdIntScratch.GetData());
		// Seed every lane with the opposite scalar bits so an unwritten lane cannot pass comparison.
		for (int32 Index = 0; Index < SimdCount; ++Index)
		{
			uint32 Bits;
			FMemory::Memcpy(&Bits, static_cast<const uint8*>(ScalarOutput) + Index * sizeof(uint32), sizeof(Bits));
			Bits ^= 0xffffffffu;
			FMemory::Memcpy(static_cast<uint8*>(SimdOutput) + Index * sizeof(uint32), &Bits, sizeof(Bits));
		}
		FString SimdError;
		FVoxelLandOperatorOutput Writer(OutputType, SimdOutput, SimdCount);
		if (!OperatorSimdWorkers[OperatorIndex]->EvaluateSimd(Inputs, Writer, SimdCount, SimdError)
			|| !Writer.IsComplete(SimdError)
			|| FMemory::Memcmp(ScalarOutput, SimdOutput, SIZE_T(SimdCount) * sizeof(uint32)) != 0)
		{
			OperatorSimdEnabled[OperatorIndex] = false;
			UE_LOG(LogTemp, Warning, TEXT("VoxelLand external SIMD kernel disabled after scalar mismatch or error: %s"),
				*SimdError);
		}
		else
		{
			FMemory::Memcpy(ScalarOutput, SimdOutput, SIZE_T(SimdCount) * sizeof(uint32));
		}
	}
}

FVoxelLandError FVoxelLandWorkerContextImpl::ExecuteInstruction(const FVoxelLandInstruction& I,
	const FVoxelLandProgramDraft& D, const TArray<float>& FloatParameters, const TArray<int32>& IntParameters,
	const TArray<int32>& Seeds, int32 N)
{
	if (I.Op == EVoxelLandOpcode::ExternalOperator)
	{
		return ExecuteExternal(I, D, N);
	}
	if (uint16(I.Op) > uint16(EVoxelLandOpcode::StoreDensity)
		&& FVoxelLandGeneratorValidation::NoiseVariant(I.Op) == INDEX_NONE)
	{
		return ExecuteTyped(I, IntParameters, N);
	}
	float* Dest = I.Op == EVoxelLandOpcode::StoreDensity
					? Output.GetData()
					: Registers.GetData() + int32(I.Dst) * FVoxelLandGeneratorValidation::BatchSize;
	const float* A = nullptr;
	const float* B = nullptr;
	const float* C = nullptr;
	ResolveOperands(I, A, B, C);
	if (I.Op == EVoxelLandOpcode::Divide)
	{
		for (int32 SampleIndex = 0; SampleIndex < N; ++SampleIndex)
		{
			if (B[SampleIndex] == 0.f)
			{
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::InvalidMathDomain, TEXT("Division by zero"));
			}
		}
	}
	if (FVoxelLandGeneratorValidation::NoiseVariant(I.Op) != INDEX_NONE)
	{
		if (FVoxelLandError Error = EvaluateNoise(I, D, FloatParameters, Seeds, N, A, B, C, Dest))
		{
			return Error;
		}
	}
	else
	{
		int32 Start = 0;
#if VOXELLAND_HAS_VM_SIMD
		Start = ExecuteSimd(I.Op, N, A, B, Dest);
#endif
		if (FVoxelLandError Error = ExecuteScalar(I, D, FloatParameters, Start, N, A, B, C, Dest))
		{
			return Error;
		}
	}
	for (int32 SampleIndex = 0; SampleIndex < N; ++SampleIndex)
	{
		if (!FMath::IsFinite(Dest[SampleIndex]))
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidMathDomain, TEXT("Non-finite VM result"));
		}
	}
	return {};
}

FVoxelLandError FVoxelLandWorkerContextImpl::QuantizeOutput(int32 N, float VoxelSizeCm, TArray<int16>& OutDensity)
{
	FVoxelLandWorkerContextImpl& W = *this;
	W.Quantized.SetNumUninitialized(N);
	for (int32 SampleIndex = 0; SampleIndex < N; ++SampleIndex)
	{
		if (FVoxelLandError E =
				VoxelLandDeterminism::Quantize(W.Output[SampleIndex], VoxelSizeCm, W.Quantized[SampleIndex]))
		{
			return E;
		}
	}
	OutDensity = W.Quantized;
	return {};
}

FVoxelLandError FVoxelLandGenerator::EvaluateToWorkerBuffer(
	TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker, TArray<int32>* IntegerPreview) const
{
	if (M4HeightGrid || M4Unlimited)
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidContext, TEXT("M4 height fields require exact chunk lattice coordinates"));
	}
	if (IsInGameThread())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Evaluation requires a worker thread"));
	}
	if (!Worker.Impl || Worker.Impl->Owner.Get() != this || Worker.Impl->Thread != FPlatformTLS::GetCurrentThreadId())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidContext, TEXT("Worker belongs to a different generator or thread"));
	}
	if (Positions.Num() < 1 || Positions.Num() > FVoxelLandGeneratorValidation::BatchSize)
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidContext, TEXT("A batch must contain 1..256 samples"));
	}

	FVoxelLandWorkerContextImpl& W = *Worker.Impl;
	if (FVoxelLandError Error = W.LoadPositions(Positions))
	{
		return Error;
	}
	const FVoxelLandProgramDraft& D = Program->GetData();
	TConstArrayView<FVoxelLandInstruction> Instructions = D.Instructions;
	if (IntegerPreview)
	{
		if (Instructions.Num() < 2 || Instructions.Last().Op != EVoxelLandOpcode::StoreDensity
			|| Instructions.Last(1).Op != EVoxelLandOpcode::IntToFloat
			|| Instructions.Last(1).Dst != Instructions.Last().A)
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidContext, TEXT("Integer preview requires a terminal integer conversion"));
		}
		Instructions = Instructions.Left(Instructions.Num() - 2);
	}
	for (const FVoxelLandInstruction& I : Instructions)
	{
		if (FVoxelLandError Error = W.ExecuteInstruction(I, D, FloatParameters, IntParameters, Seeds, Positions.Num()))
		{
			return Error;
		}
	}
	if (IntegerPreview)
	{
		const int32 Offset = int32(D.Instructions.Last(1).A) * FVoxelLandGeneratorValidation::BatchSize;
		IntegerPreview->Append(W.IntRegisters.GetData() + Offset, Positions.Num());
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::EvaluateIntegerSamples(
	TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker, TArray<int32>& OutValues) const
{
	OutValues.Reset();
	return EvaluateToWorkerBuffer(Positions, Worker, &OutValues);
}

#if WITH_EDITOR
FVoxelLandError FVoxelLandGenerator::EvaluateIntegerPreview(
	TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker, TArray<int32>& OutValues) const
{
	return EvaluateIntegerSamples(Positions, Worker, OutValues);
}
#endif

FVoxelLandError FVoxelLandGenerator::EvaluateSamples(
	TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity) const
{
	OutDensity.Reset();
	if (FVoxelLandError Error = EvaluateToWorkerBuffer(Positions, Worker))
	{
		return Error;
	}
	return Worker.Impl->QuantizeOutput(Positions.Num(), Profile.VoxelSizeCm, OutDensity);
}

FVoxelLandError FVoxelLandGenerator::EvaluateSamplesRaw(
	TConstArrayView<FVector3f> Positions, FVoxelLandWorkerContext& Worker, TArray<float>& OutValues) const
{
	OutValues.Reset();
	if (FVoxelLandError Error = EvaluateToWorkerBuffer(Positions, Worker))
	{
		return Error;
	}
	OutValues.Append(Worker.Impl->Output.GetData(), Positions.Num());
	return {};
}

class FVoxelLandGeneratorSampling final
{
public:
	static FVoxelLandError FillChunkPositions(
		const FVoxelLandChunkKey& Key, float VoxelSizeCm, int32 Start, TArrayView<FVector3f> Positions)
	{
		for (int32 SampleIndex = 0; SampleIndex < Positions.Num(); ++SampleIndex)
		{
			const int32 Index = Start + SampleIndex, X = Index % 35, Y = (Index / 35) % 35, Z = Index / (35 * 35);
			const double PX = double(int64(Key.Coordinate.X) * 32 + X - 1) * double(VoxelSizeCm);
			const double PY = double(int64(Key.Coordinate.Y) * 32 + Y - 1) * double(VoxelSizeCm);
			const double PZ = double(int64(Key.Coordinate.Z) * 32 + Z - 1) * double(VoxelSizeCm);
			if (FMath::Abs(PX) > double(FLT_MAX) || FMath::Abs(PY) > double(FLT_MAX)
				|| FMath::Abs(PZ) > double(FLT_MAX))
			{
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Chunk coordinate cannot be represented"));
			}
			Positions[SampleIndex] = FVector3f(float(PX), float(PY), float(PZ));
			if (!FMath::IsFinite(Positions[SampleIndex].X) || !FMath::IsFinite(Positions[SampleIndex].Y)
				|| !FMath::IsFinite(Positions[SampleIndex].Z))
			{
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("Chunk coordinate cannot be represented"));
			}
		}
		return {};
	}
};

uint64 FVoxelLandWorkerContextImpl::HashChunkDensity(const TArray<int16>& Density)
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

FVoxelLandError FVoxelLandGenerator::GenerateChunkCancellable(const FVoxelLandChunkKey& Key,
	FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
	TFunctionRef<bool()> IsCancelled) const
{
	if (Key.LOD != 0)
	{
		return Key.LOD == 1 && SupportsM7FiniteSource() ? GenerateM7FiniteChunk(Key, Worker, OutChunk, IsCancelled)
														: GenerateM7CoarseChunk(Key, Worker, OutChunk, IsCancelled);
	}
	if (!M6Filters.IsEmpty() || M6RedistanceBand > 0)
	{
		const FVoxelLandM6FilterPlan Plan = MakeM6FilterPlan(Key);
		if (Plan.IsActive())
		{
			return GenerateM6ProcessedChunk(Key, Plan, Worker, OutChunk, IsCancelled);
		}
	}
	if (M4Unlimited)
	{
		if (IsInGameThread())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("M4 local generation requires a worker thread"));
		}
		if (!Worker.Impl || Worker.Impl->Owner.Get() != this
			|| Worker.Impl->Thread != FPlatformTLS::GetCurrentThreadId())
		{
			return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidContext,
				TEXT("M4 local worker belongs to a different generator or thread"));
		}
		return M6Stamps.IsEmpty()
				 ? VoxelLandM4Unlimited::GenerateChunk(*M4UnlimitedSettings, *M4Unlimited, Key, OutChunk, IsCancelled)
				 : GenerateM4LocalChunkWithStamps(Key, Worker, OutChunk, IsCancelled);
	}
	if (!DirectDensityBounds.IsSet())
	{
		return GenerateDirectDensityChunk(Key, Worker, OutChunk, IsCancelled);
	}
	if (!IncludesChunk(Key))
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidProfile, TEXT("Chunk lies outside the finite density world"));
	}
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
	if (const FVoxelLandError Error = GenerateDirectDensityChunk(Key, Worker, Chunk, IsCancelled))
	{
		return Error;
	}
	if (const FVoxelLandError Error = ClipDirectDensityChunk(Chunk, IsCancelled))
	{
		return Error;
	}
	OutChunk = MoveTemp(Chunk);
	return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateDirectDensityChunk(const FVoxelLandChunkKey& Key,
	FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
	TFunctionRef<bool()> IsCancelled) const
{
	if (Program->GetData().ExternalFilterIndex != INDEX_NONE)
	{
		return GenerateExternalFilteredChunkCancellable(Key, Worker, OutChunk, IsCancelled);
	}
	if (bSmoothEnabled && SmoothStrength > 0.f)
	{
		return GenerateSmoothedChunkCancellable(Key, Worker, true, SmoothStrength, OutChunk, IsCancelled);
	}
	return GeneratePureChunkCancellable(Key, Worker, OutChunk, IsCancelled);
}

FVoxelLandError FVoxelLandGenerator::ValidateM4ChunkDemand(const FVoxelLandChunkKey& Key) const
{
	if (Key.LOD != 0)
	{
		return Key.LOD == 1 && SupportsM7FiniteSource() ? ValidateM7FiniteDemand(Key) : ValidateM7CoarseDemand(Key);
	}
	if (!M6Filters.IsEmpty() || M6RedistanceBand > 0)
	{
		const FVoxelLandM6FilterPlan Plan = MakeM6FilterPlan(Key);
		if (Plan.IsActive())
		{
			return ValidateM6FilterDemand(Key, Plan);
		}
	}
	if (M4Unlimited)
	{
		return VoxelLandM4Unlimited::ValidateChunkDemand(Key, Profile.VoxelSizeCm);
	}
	if (!M4HeightGrid)
	{
		return {};
	}
	return bSmoothEnabled && SmoothStrength > 0.f ? ValidateM4Footprint(Key, 37, -2) : ValidateM4Footprint(Key, 35, -1);
}

FVoxelLandError FVoxelLandGenerator::ValidateM4Footprint(const FVoxelLandChunkKey& Key, int32 Axis, int32 Offset) const
{
	if (!M4HeightGrid || (Axis != 35 && Axis != 37))
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidContext, TEXT("Invalid M4 height-field evaluation"));
	}
	if (Key.LOD != 0)
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::UnsupportedLod, TEXT("M4 height field supports only LOD 0"));
	}
	const int64 VoxelSize = int64(Profile.VoxelSizeCm);
	const int64 BaseX = int64(Key.Coordinate.X) * 32;
	const int64 BaseY = int64(Key.Coordinate.Y) * 32;
	const int64 BaseZ = int64(Key.Coordinate.Z) * 32;
	const int32 Margin = bM4NormalizeSlope ? 1 : 0;
	int64 MinX = 0, MaxX = 0, MinY = 0, MaxY = 0;
	int64 MinZ = 0, MaxZ = 0;
	const bool bStandardFootprint = (Axis == 35 && Offset == -1) || (Axis == 37 && Offset == -2);
	FVoxelLandM5ChunkFootprint Footprint;
	const bool bUseFootprint =
		bStandardFootprint
		&& VoxelLand::M5::ChunkFootprint::TryBuild(Key, VoxelSize, Axis == 37, bM4NormalizeSlope, Footprint);
	if (bUseFootprint)
	{
		MinX = Footprint.HeightInputMin.X;
		MaxX = Footprint.HeightInputMax.X;
		MinY = Footprint.HeightInputMin.Y;
		MaxY = Footprint.HeightInputMax.Y;
		MinZ = Footprint.DensityInputMin.Z;
		MaxZ = Footprint.DensityInputMax.Z;
	}
	else if (!VoxelLandM4Control::LatticePositionCm(BaseX + Offset - Margin, VoxelSize, MinX)
			 || !VoxelLandM4Control::LatticePositionCm(BaseX + Offset + Axis - 1 + Margin, VoxelSize, MaxX)
			 || !VoxelLandM4Control::LatticePositionCm(BaseY + Offset - Margin, VoxelSize, MinY)
			 || !VoxelLandM4Control::LatticePositionCm(BaseY + Offset + Axis - 1 + Margin, VoxelSize, MaxY))
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M4 height-field footprint overflows"));
	}
	// Native noise receives exact integer centimeter positions converted to float.
	if (!bM4FixedLatticeShaping
		&& ((M4Overhang.Displacement.bEnabled && M4Overhang.Displacement.AmplitudeQ8 != 0)
			|| (M4HeightDetail.bEnabled && M4HeightDetail.AmplitudeQ8 != 0))
		&& (MinX < -16777216 || MaxX > 16777216 || MinY < -16777216 || MaxY > 16777216))
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M4 overhang coordinates exceed exact float lattice"));
	}
	int32 Check = 0;
	if (!M4HeightGrid->Sample(MinX, MinY, Check) || !M4HeightGrid->Sample(MaxX, MaxY, Check))
	{
		return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidParameter,
			TEXT("M4 chunk including padding, filter halo and slope stencil is outside the height grid"));
	}
	if ((!bUseFootprint
			&& (!VoxelLandM4Control::LatticePositionCm(BaseZ + Offset, VoxelSize, MinZ)
				|| !VoxelLandM4Control::LatticePositionCm(BaseZ + Offset + Axis - 1, VoxelSize, MaxZ)))
		|| MinZ < -2000000 || MaxZ > 2000000)
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M4 height-field Z exceeds the fixed-point range"));
	}
	return bM6LegacySphereEnvelope ? ValidateM6SphereFootprint(Key) : FVoxelLandError{};
}

FVoxelLandError FVoxelLandGenerator::GenerateM4Density(const FVoxelLandChunkKey& Key, int32 Axis, int32 Offset,
	FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity, TFunctionRef<bool()> IsCancelled) const
{
	if (FVoxelLandError Error = ValidateM4Footprint(Key, Axis, Offset))
	{
		return Error;
	}
	FVoxelLandM5PointCm MinLattice;
	MinLattice.X = int64(Key.Coordinate.X) * 32 + Offset;
	MinLattice.Y = int64(Key.Coordinate.Y) * 32 + Offset;
	MinLattice.Z = int64(Key.Coordinate.Z) * 32 + Offset;
	return GenerateM4DensityBox(MinLattice, FIntVector(Axis, Axis, Axis), Worker, OutDensity, IsCancelled);
}

FVoxelLandError FVoxelLandGenerator::GenerateM4DensityBox(const FVoxelLandM5PointCm& MinLattice, const FIntVector& Size,
	FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity, TFunctionRef<bool()> IsCancelled) const
{
	const FVoxelLandM7SampleGrid Grid{FInt64Vector(MinLattice.X, MinLattice.Y, MinLattice.Z), Size, 1};
	return GenerateM4SampleGrid(Grid, Worker, OutDensity, IsCancelled);
}

FVoxelLandError FVoxelLandGenerator::GenerateM4StridedDetail(const FVoxelLandM7SampleGrid& Grid,
	const FIntVector& Offset, FVoxelLandWorkerContext& Worker, TArray<int32>& Heights, TFunctionRef<bool()> IsCancelled,
	TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const
{
	if (!bM4FixedLatticeShaping && (!Worker.Impl || !Worker.Impl->Noise))
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidContext, TEXT("M4 height detail worker missing"));
	}
	const int64 VoxelSize = int64(Profile.VoxelSizeCm);
	const int64 Step = VoxelSize * Grid.Stride;
	const FInt64Vector First = (Grid.First + FInt64Vector(Offset)) * VoxelSize;
	for (int32 Y = 0; Y < Grid.Size.Y; ++Y)
	{
		if (IsCancelled())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::Cancelled, TEXT("M4 height detail generation cancelled"));
		}
		const int64 YCm = First.Y + Y * Step;
		float XNoise[37], YNoise[37], Noise[37];
		if (!bM4FixedLatticeShaping)
		{
			for (int32 X = 0; X < Grid.Size.X; ++X)
			{
				XNoise[X] = float(First.X + X * Step) * M4HeightDetail.FrequencyPerCm;
				YNoise[X] = float(YCm) * M4HeightDetail.FrequencyPerCm;
			}
			FString Error;
			if (!Worker.Impl->Noise->Sample(
					0, false, M4HeightDetailSeed, Grid.Size.X, XNoise, YNoise, nullptr, Noise, Error))
			{
				return FVoxelLandGeneratorValidation::NativeError(Error);
			}
		}
		for (int32 X = 0; X < Grid.Size.X; ++X)
		{
			const int64 XCm = First.X + X * Step;
			int32 Base = 0, NoiseQ = 0;
			const bool bNoiseValid = bM4FixedLatticeShaping
									   ? VoxelLandM4FixedLattice::SampleShaping2D(XCm, YCm,
											 M4HeightDetail.FrequencyPerCm, uint64(uint32(M4HeightDetailSeed)), NoiseQ)
									   : VoxelLandM4Overhang::QuantizeNoise(Noise[X], NoiseQ);
			if (!bNoiseValid || !SampleM4BaseHeight(XCm, YCm, Base, LocalHeight)
				|| !FVoxelLandGeneratorValidation::ApplyHeightDetail(
					Base, NoiseQ, M4HeightDetail.AmplitudeQ8, Heights[X + Grid.Size.X * Y]))
			{
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::InvalidMathDomain, TEXT("Invalid M4 strided height detail sample"));
			}
		}
	}
	return {};
}

bool FVoxelLandGenerator::SupportsHeightClipmap() const
{
	return M4Unlimited && M4Unlimited->AuthoredHeightGenerator && !M5Lease.IsValid() && GetM6CompatibilityHalo() == 0
		&& M6Stamps.IsEmpty() && M6Filters.IsEmpty() && M6RedistanceBand == 0
		&& !(M4Overhang.Displacement.bEnabled && M4Overhang.Displacement.AmplitudeQ8 != 0);
}

FVoxelLandError FVoxelLandGenerator::SampleHeightClipmapPoint(
	int64 X, int64 Y, TArray<int64>& Registers, int32& OutHeightQ8) const
{
	int32 HeightQ8 = 0;
	if (const FVoxelLandError Error =
			VoxelLandM4FixedLattice::EvaluateValidatedHeightPoint(*M4Unlimited->AuthoredHeightGenerator, X, Y,
				M4Unlimited->BaseQ8, M4Unlimited->HeightScaleCm, Registers, HeightQ8))
	{
		return Error;
	}
	int32 NoiseQ = 0;
	if (M4HeightDetail.bEnabled && M4HeightDetail.AmplitudeQ8 != 0
		&& (!VoxelLandM4FixedLattice::SampleShaping2D(
				X, Y, M4HeightDetail.FrequencyPerCm, uint64(uint32(M4HeightDetailSeed)), NoiseQ)
			|| !FVoxelLandGeneratorValidation::ApplyHeightDetail(
				HeightQ8, NoiseQ, M4HeightDetail.AmplitudeQ8, HeightQ8)))
	{
		return {EVoxelLandErrorCode::InvalidMathDomain, TEXT("Invalid height clipmap detail sample")};
	}
	OutHeightQ8 = HeightQ8;
	return {};
}

FVoxelLandError FVoxelLandGenerator::SampleHeightClipmap(
	TConstArrayView<FInt64Vector> Locations, TArray<int32>& OutHeightQ8, TFunctionRef<bool()> IsCancelled) const
{
	if (IsInGameThread())
	{
		return {EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Height clipmap sampling requires a worker thread")};
	}
	if (!SupportsHeightClipmap())
	{
		return {EVoxelLandErrorCode::UnsupportedLod, TEXT("Height clipmap requires an authored single-valued field")};
	}

	TArray<int64> Registers;
	TArray<int32> Heights;
	Heights.Reserve(Locations.Num());
	constexpr int32 CancelPollInterval = 64;
	for (int32 Index = 0; Index < Locations.Num(); ++Index)
	{
		if (Index % CancelPollInterval == 0 && IsCancelled())
		{
			return {EVoxelLandErrorCode::Cancelled, TEXT("Height clipmap sampling cancelled")};
		}
		const FInt64Vector& Point = Locations[Index];
		int32 HeightQ8 = 0;
		if (const FVoxelLandError Error = SampleHeightClipmapPoint(Point.X, Point.Y, Registers, HeightQ8))
		{
			return Error;
		}
		Heights.Add(HeightQ8);
	}
	if (IsCancelled())
	{
		return {EVoxelLandErrorCode::Cancelled, TEXT("Height clipmap sampling cancelled")};
	}
	OutHeightQ8 = MoveTemp(Heights);
	return {};
}

bool FVoxelLandGenerator::SampleM4BaseHeight(
	int64 X, int64 Y, int32& Height, TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const
{
	return LocalHeight ? (*LocalHeight)(X, Y, Height) : M4HeightGrid->Sample(X, Y, Height);
}

FVoxelLandError FVoxelLandGenerator::SampleTerrainHeightColumns(const FVoxelLandM7SampleGrid& Grid,
	FVoxelLandTerrainHeightColumns& Columns, TFunctionRef<bool()> IsCancelled,
	TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const
{
	const int64 VoxelSize = int64(Profile.VoxelSizeCm);
	const int64 Stride = Grid.Stride;
	const int64 BaseX = Grid.First.X, BaseY = Grid.First.Y;
	const int32 AxisX = Grid.Size.X, AxisY = Grid.Size.Y;
	// Height and stencil inputs are 2D; evaluate each XY column once rather
	// than repeating the same bilinear lookups for every Z sample.
	Columns.Centers.SetNumUninitialized(AxisX * AxisY);
	if (bM4NormalizeSlope)
	{
		Columns.PlusX.SetNumUninitialized(AxisX * AxisY);
		Columns.MinusX.SetNumUninitialized(AxisX * AxisY);
		Columns.PlusY.SetNumUninitialized(AxisX * AxisY);
		Columns.MinusY.SetNumUninitialized(AxisX * AxisY);
	}
	for (int32 Y = 0; Y < AxisY; ++Y)
	{
		if (IsCancelled())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::Cancelled, TEXT("M4 height-field generation cancelled"));
		}
		const int64 YCm = (BaseY + Y * Stride) * VoxelSize;
		for (int32 X = 0; X < AxisX; ++X)
		{
			const int64 XCm = (BaseX + X * Stride) * VoxelSize;
			const int32 Column = X + AxisX * Y;
			if (!SampleM4BaseHeight(XCm, YCm, Columns.Centers[Column], LocalHeight))
			{
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::InvalidParameter, TEXT("M4 height sample outside grid"));
			}
			if (bM4NormalizeSlope
				&& (!SampleM4BaseHeight(XCm + VoxelSize, YCm, Columns.PlusX[Column], LocalHeight)
					|| !SampleM4BaseHeight(XCm - VoxelSize, YCm, Columns.MinusX[Column], LocalHeight)
					|| !SampleM4BaseHeight(XCm, YCm + VoxelSize, Columns.PlusY[Column], LocalHeight)
					|| !SampleM4BaseHeight(XCm, YCm - VoxelSize, Columns.MinusY[Column], LocalHeight)))
			{
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::InvalidParameter, TEXT("M4 slope stencil outside grid"));
			}
		}
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::ApplyTerrainHeightDetail(const FVoxelLandM7SampleGrid& Grid,
	FVoxelLandTerrainHeightColumns& Columns, FVoxelLandWorkerContext& Worker, TFunctionRef<bool()> IsCancelled,
	TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const
{
	const int64 VoxelSize = int64(Profile.VoxelSizeCm);
	const int64 Stride = Grid.Stride;
	const int64 BaseX = Grid.First.X, BaseY = Grid.First.Y;
	const int32 AxisX = Grid.Size.X, AxisY = Grid.Size.Y;
	const bool bDetail = M4HeightDetail.bEnabled && M4HeightDetail.AmplitudeQ8 != 0;
	if (bDetail && Stride != 1 && bM4NormalizeSlope)
	{
		const FIntVector Offsets[] = {FIntVector::ZeroValue, FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
			FIntVector(0, 1, 0), FIntVector(0, -1, 0)};
		TArray<int32>* Heights[] = {&Columns.Centers, &Columns.PlusX, &Columns.MinusX, &Columns.PlusY, &Columns.MinusY};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Offsets); ++Index)
		{
			if (const FVoxelLandError Error =
					GenerateM4StridedDetail(Grid, Offsets[Index], Worker, *Heights[Index], IsCancelled, LocalHeight))
			{
				return Error;
			}
		}
	}
	else if (bDetail)
	{
		if (!bM4FixedLatticeShaping && (!Worker.Impl || !Worker.Impl->Noise))
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::InvalidContext, TEXT("M4 height detail worker missing"));
		}
		const int32 Margin = bM4NormalizeSlope ? 1 : 0;
		const int32 DetailAxisX = AxisX + 2 * Margin;
		const int32 DetailAxisY = AxisY + 2 * Margin;
		TArray<int32> DetailedHeights;
		DetailedHeights.SetNumUninitialized(DetailAxisX * DetailAxisY);
		for (int32 Y = 0; Y < DetailAxisY; ++Y)
		{
			if (IsCancelled())
			{
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::Cancelled, TEXT("M4 height detail generation cancelled"));
			}
			const int64 YCm = (BaseY + (Y - Margin) * Stride) * VoxelSize;
			float XNoise[39], YNoise[39], Noise[39];
			if (!bM4FixedLatticeShaping)
			{
				for (int32 X = 0; X < DetailAxisX; ++X)
				{
					XNoise[X] = float((BaseX + (X - Margin) * Stride) * VoxelSize) * M4HeightDetail.FrequencyPerCm;
					YNoise[X] = float(YCm) * M4HeightDetail.FrequencyPerCm;
				}
				FString Error;
				if (!Worker.Impl->Noise->Sample(
						0, false, M4HeightDetailSeed, DetailAxisX, XNoise, YNoise, nullptr, Noise, Error))
				{
					return FVoxelLandGeneratorValidation::NativeError(Error);
				}
			}
			for (int32 X = 0; X < DetailAxisX; ++X)
			{
				int32 BaseHeight = 0, NoiseQ = 0;
				const int64 XCm = (BaseX + (X - Margin) * Stride) * VoxelSize;
				const bool bNoiseValid = bM4FixedLatticeShaping ? VoxelLandM4FixedLattice::SampleShaping2D(XCm, YCm,
																	  M4HeightDetail.FrequencyPerCm,
																	  uint64(uint32(M4HeightDetailSeed)), NoiseQ)
																: VoxelLandM4Overhang::QuantizeNoise(Noise[X], NoiseQ);
				if (!SampleM4BaseHeight(XCm, YCm, BaseHeight, LocalHeight) || !bNoiseValid)
				{
					return FVoxelLandGeneratorValidation::Fail(
						EVoxelLandErrorCode::InvalidMathDomain, TEXT("Invalid M4 height detail sample"));
				}
				if (!FVoxelLandGeneratorValidation::ApplyHeightDetail(
						BaseHeight, NoiseQ, M4HeightDetail.AmplitudeQ8, DetailedHeights[X + DetailAxisX * Y]))
				{
					return FVoxelLandGeneratorValidation::Fail(
						EVoxelLandErrorCode::InvalidMathDomain, TEXT("M4 detailed height exceeds fixed-point range"));
				}
			}
		}
		for (int32 Y = 0; Y < AxisY; ++Y)
		{
			for (int32 X = 0; X < AxisX; ++X)
			{
				const int32 Column = X + AxisX * Y;
				const int32 I = X + Margin + DetailAxisX * (Y + Margin);
				Columns.Centers[Column] = DetailedHeights[I];
				if (bM4NormalizeSlope)
				{
					Columns.PlusX[Column] = DetailedHeights[I + 1];
					Columns.MinusX[Column] = DetailedHeights[I - 1];
					Columns.PlusY[Column] = DetailedHeights[I + DetailAxisX];
					Columns.MinusY[Column] = DetailedHeights[I - DetailAxisX];
				}
			}
		}
	}
	return {};
}

FVoxelLandError FVoxelLandGenerator::WriteTerrainDensityGrid(const FVoxelLandM7SampleGrid& Grid,
	const FVoxelLandTerrainHeightColumns& Columns, FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity,
	TFunctionRef<bool()> IsCancelled) const
{
	const int64 VoxelSize = int64(Profile.VoxelSizeCm);
	const int64 Stride = Grid.Stride;
	const int64 BaseX = Grid.First.X, BaseY = Grid.First.Y, BaseZ = Grid.First.Z;
	const int32 AxisX = Grid.Size.X, AxisY = Grid.Size.Y, AxisZ = Grid.Size.Z;
	TArray<int16> Density;
	Density.Reserve(AxisX * AxisY * AxisZ);
	for (int32 Z = 0; Z < AxisZ; ++Z)
	{
		if (IsCancelled())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::Cancelled, TEXT("M4 height-field generation cancelled"));
		}
		int64 ZCm = 0;
		if (!VoxelLandM4Control::LatticePositionCm(BaseZ + Z * Stride, VoxelSize, ZCm) || ZCm < -2000000
			|| ZCm > 2000000)
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::NonFiniteCoordinate, TEXT("M4 height-field Z exceeds the fixed-point range"));
		}
		for (int32 Y = 0; Y < AxisY; ++Y)
		{
			if (IsCancelled())
			{
				return FVoxelLandGeneratorValidation::Fail(
					EVoxelLandErrorCode::Cancelled, TEXT("M4 height-field generation cancelled"));
			}
			float NX[37], NY[37], NZ[37], Noise[37];
			const bool bOverhang = M4Overhang.Displacement.bEnabled && M4Overhang.Displacement.AmplitudeQ8 != 0;
			if (bOverhang)
			{
				if (!bM4FixedLatticeShaping && (!Worker.Impl || !Worker.Impl->Noise))
				{
					return FVoxelLandGeneratorValidation::Fail(
						EVoxelLandErrorCode::InvalidContext, TEXT("M4 overhang worker missing"));
				}
				if (!bM4FixedLatticeShaping)
				{
					for (int32 X = 0; X < AxisX; ++X)
					{
						NX[X] = float((BaseX + X * Stride) * VoxelSize) * M4Overhang.FrequencyPerCm;
						NY[X] = float((BaseY + Y * Stride) * VoxelSize) * M4Overhang.FrequencyPerCm;
						NZ[X] = float(ZCm) * M4Overhang.FrequencyPerCm;
					}
					FString Error;
					if (!Worker.Impl->Noise->Sample(0, true, M4OverhangSeed, AxisX, NX, NY, NZ, Noise, Error))
					{
						return FVoxelLandGeneratorValidation::NativeError(Error);
					}
				}
			}
			for (int32 X = 0; X < AxisX; ++X)
			{
				const int32 Column = X + AxisX * Y;
				const int32 Center = Columns.Centers[Column];
				int64 DensityQ8 = ZCm * 256 - Center;
				if (bM4NormalizeSlope)
				{
					if (!VoxelLandM4Slope::DensityQ8(ZCm * 256, Center, Columns.PlusX[Column], Columns.MinusX[Column],
							Columns.PlusY[Column], Columns.MinusY[Column], int32(VoxelSize), DensityQ8))
					{
						return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::InvalidMathDomain,
							TEXT("M4 slope stencil is outside its supported range"));
					}
				}
				if (bOverhang)
				{
					int32 NoiseQ = 0, DeltaQ8 = 0;
					const bool bNoiseValid =
						bM4FixedLatticeShaping
							? VoxelLandM4FixedLattice::SampleShaping3D((BaseX + X * Stride) * VoxelSize,
								  (BaseY + Y * Stride) * VoxelSize, ZCm, M4Overhang.FrequencyPerCm,
								  uint64(uint32(M4OverhangSeed)), NoiseQ)
							: VoxelLandM4Overhang::QuantizeNoise(Noise[X], NoiseQ);
					if (!bNoiseValid
						|| !VoxelLandM4Overhang::DisplacementQ8(
							M4Overhang.Displacement, ZCm * 256 - Center, NoiseQ, DeltaQ8))
					{
						return FVoxelLandGeneratorValidation::Fail(
							EVoxelLandErrorCode::InvalidMathDomain, TEXT("Invalid M4 overhang result"));
					}
					DensityQ8 += DeltaQ8;
				}
				Density.Add(FVoxelLandQuantizedDensity::FromHeightQ8(DensityQ8, VoxelSize));
			}
		}
	}
	if (IsCancelled())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::Cancelled, TEXT("M4 height-field generation cancelled"));
	}
	OutDensity = MoveTemp(Density);
	return {};
}

FVoxelLandError FVoxelLandGenerator::GenerateM4SampleGrid(const FVoxelLandM7SampleGrid& Grid,
	FVoxelLandWorkerContext& Worker, TArray<int16>& OutDensity, TFunctionRef<bool()> IsCancelled,
	TFunctionRef<bool(int64, int64, int32&)>* LocalHeight) const
{
	check(Grid.Size.X > 0 && Grid.Size.X <= 37 && Grid.Size.Y > 0 && Grid.Size.Y <= 37 && Grid.Size.Z > 0
		  && Grid.Size.Z <= 37 && Grid.Stride > 0);
	FVoxelLandTerrainHeightColumns Columns;
	if (const FVoxelLandError Error = SampleTerrainHeightColumns(Grid, Columns, IsCancelled, LocalHeight))
	{
		return Error;
	}
	if (const FVoxelLandError Error = ApplyTerrainHeightDetail(Grid, Columns, Worker, IsCancelled, LocalHeight))
	{
		return Error;
	}
	return WriteTerrainDensityGrid(Grid, Columns, Worker, OutDensity, IsCancelled);
}

FVoxelLandError FVoxelLandGenerator::GeneratePureChunkCancellable(const FVoxelLandChunkKey& Key,
	FVoxelLandWorkerContext& Worker, TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe>& OutChunk,
	TFunctionRef<bool()> IsCancelled) const
{
	if (!M4HeightGrid)
	{
		OutChunk.Reset();
	}
	if (IsInGameThread())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::GameThreadNotAllowed, TEXT("Chunk generation requires a worker thread"));
	}
	if (!Worker.Impl || Worker.Impl->Owner.Get() != this || Worker.Impl->Thread != FPlatformTLS::GetCurrentThreadId())
	{
		return FVoxelLandGeneratorValidation::Fail(
			EVoxelLandErrorCode::InvalidContext, TEXT("Worker belongs to a different generator or thread"));
	}
	if (Key.LOD != 0)
	{
		return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::UnsupportedLod, TEXT("M1 supports only LOD 0"));
	}
	TSharedPtr<FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk = MakeShared<FVoxelLandChunkData, ESPMode::ThreadSafe>();
	Chunk->Key = Key;
	Chunk->Density.Reserve(FVoxelLandChunkData::SampleCount);
	if (M4HeightGrid)
	{
		if (FVoxelLandError Error = GenerateM4Density(Key, 35, -1, Worker, Chunk->Density, IsCancelled))
		{
			return Error;
		}
		return FinalizeDensityChunk(Chunk.ToSharedRef(), Worker, OutChunk, IsCancelled);
	}
	FVector3f Positions[FVoxelLandGeneratorValidation::BatchSize];
	TArray<int16> Batch;
	Batch.Reserve(FVoxelLandGeneratorValidation::BatchSize);
	for (int32 Start = 0; Start < FVoxelLandChunkData::SampleCount; Start += FVoxelLandGeneratorValidation::BatchSize)
	{
		if (IsCancelled())
		{
			return FVoxelLandGeneratorValidation::Fail(
				EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled"));
		}
		const int32 Count =
			FMath::Min(FVoxelLandGeneratorValidation::BatchSize, FVoxelLandChunkData::SampleCount - Start);
		if (FVoxelLandError Error = FVoxelLandGeneratorSampling::FillChunkPositions(
				Key, Profile.VoxelSizeCm, Start, MakeArrayView(Positions, Count)))
		{
			return Error;
		}
		if (FVoxelLandError E = EvaluateSamples(MakeArrayView(Positions, Count), Worker, Batch))
		{
			return E;
		}
		Chunk->Density.Append(Batch);
	}
	if (IsCancelled())
	{
		return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled"));
	}
	Chunk->Hash = Worker.Impl->HashChunkDensity(Chunk->Density);
	if (IsCancelled())
	{
		return FVoxelLandGeneratorValidation::Fail(EVoxelLandErrorCode::Cancelled, TEXT("Chunk generation cancelled"));
	}
	OutChunk = Chunk;
	return {};
}
