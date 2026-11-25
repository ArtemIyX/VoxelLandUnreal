#include "World/Caves/Planning/VoxelLandCaveFiniteControls.h"

#include "Data/Terrain/Worlds/VoxelLandTerrainWorldSnapshot.h"
#include "Data/Field/Chunks/VoxelLandGeneratorSettings.h"
#include "Generation/Field/VoxelLandGenerator.h"
#include "Generation/Terrain/Controls/VoxelLandControlGrid.h"
#include "Generation/Terrain/Curves/VoxelLandCurveLut.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudget.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveBudgetLease.h"
#include "Generation/Caves/Infrastructure/VoxelLandCaveCheckedMath.h"
#include "Graph/Serialization/VoxelLandCaveProgramLoader.h"
#include "World/Terrain/Curves/VoxelLandCurveValidation.h"
#include "World/Terrain/Controls/VoxelLandOverrideValidation.h"
#include "World/Caves/Persistence/VoxelLandCaveMeteredTerrainIdentity.h"

class FVoxelLandM5FiniteControlsInternals final
{
public:
	// Covers the three retained curve arrays, bounded temporary profiles and
	// control bookkeeping. Programs and generators carry independent leases.
	static constexpr int64 SetupBytes = 4ll * 1024 * 1024;
	static constexpr int32 MaxOverrides = 128;
	static constexpr int32 MaxParameters = 4096;
	static constexpr int32 MaxName = 256;

	static FVoxelLandM5Diagnostic Error(EVoxelLandM5ErrorCode Code, const FString& Message)
	{
		FVoxelLandM5Diagnostic Result;
		Result.Code = Code;
		Result.Stage = TEXT("M5.FiniteControls");
		Result.Message = Message;
		return Result;
	}

	// Matches M4 BuildProfile: only compiled payload parameters get a slot.
	// Validation runs first; an unreachable declaration is deliberately skipped.
	static FVoxelLandM5Diagnostic BuildProfile(const FVoxelLandM4ControlSnapshot& Channel, int32 VoxelSizeCm,
		TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
		FVoxelLandZoneProfile& Out)
	{
		using E = EVoxelLandM5ErrorCode;
		using namespace VoxelLand::M5::CheckedMath;
		const int64 Overrides = Channel.Overrides.Num();
		const int64 Parameters = Channel.Payload.Parameters.Num();
		int64 Work = 0, Lookups = 0, Collisions = 0;
		if (Overrides > MaxOverrides || Parameters > MaxParameters
			|| !TryMultiplyNonNegative(Overrides, Parameters + 1, Lookups)
			|| !TryMultiplyNonNegative(Overrides, Overrides, Collisions)
			|| !TryMultiplyNonNegative(Collisions, 2 * MaxName + 4, Collisions) || !TryAdd(Lookups, Collisions, Work)
			|| !TryAdd(Work, 4097 * 4 + 1024, Work))
		{
			return Error(E::InvalidInput, TEXT("Finite control profile exceeds supported limits"));
		}
		if (IsCancelled())
		{
			return Error(E::Cancelled, TEXT("Finite control setup cancelled"));
		}
		if (!Budget->TryChargeWork(Work))
		{
			return Error(E::BudgetExceeded, TEXT("Finite control profile work exhausted"));
		}
		FVoxelLandZoneProfile Profile;
		Profile.VoxelSizeCm = float(VoxelSizeCm);
		for (const FVoxelLandParameterOverride& Override : Channel.Overrides)
		{
			const FVoxelLandSerializedParameter* Found = nullptr;
			for (const FVoxelLandSerializedParameter& Parameter : Channel.Payload.Parameters)
			{
				if (IsCancelled())
				{
					return Error(E::Cancelled, TEXT("Finite control profile cancelled"));
				}
				if (Parameter.Id == Override.ParameterId)
				{
					Found = &Parameter;
					break;
				}
			}
			if (!Found)
			{
				continue;
			}
			if (Found->Name.Len() > MaxName)
			{
				return Error(E::InvalidInput, TEXT("Finite control override name exceeds supported limits"));
			}
			switch (Override.Type)
			{
				case EVoxelLandGraphValueType::Float:
					Profile.FloatOverrides.Add(Found->Name, Override.FloatValue);
					break;
				case EVoxelLandGraphValueType::Int32:
					Profile.IntOverrides.Add(Found->Name, Override.IntValue);
					break;
				case EVoxelLandGraphValueType::Bool:
					Profile.BoolOverrides.Add(Found->Name, Override.BoolValue);
					break;
				default:
					break;
			}
		}
		if (IsCancelled())
		{
			return Error(E::Cancelled, TEXT("Finite control profile cancelled"));
		}
		Out = MoveTemp(Profile);
		return {};
	}
};

FVoxelLandM5FiniteControls::FVoxelLandM5FiniteControls(
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe>&& InLease)
	: Lease(MoveTemp(InLease))
{
}

const FVoxelLandGenerator& FVoxelLandM5FiniteControls::GetGenerator(int32 Index) const
{
	check(Index >= 0 && Index < 3 && Generators[Index]);
	return *Generators[Index];
}

const FVoxelLandM4CurveLut& FVoxelLandM5FiniteControls::GetCurve(int32 Index) const
{
	check(Index >= 0 && Index < 3 && Curves[Index]);
	return *Curves[Index];
}

const FVoxelLandM4IdentityRecord& FVoxelLandM5FiniteControls::GetIdentity() const
{
	return Identity->GetIdentity();
}

FVoxelLandM5Diagnostic FVoxelLandM5FiniteControls::Build(const FVoxelLandM4WorldSnapshot& Snapshot,
	TSharedRef<FVoxelLandM5Budget, ESPMode::ThreadSafe> Budget, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FVoxelLandM5FiniteControls, ESPMode::ThreadSafe>& OutControls)
{
	using E = EVoxelLandM5ErrorCode;
	if (IsCancelled())
	{
		return FVoxelLandM5FiniteControlsInternals::Error(E::Cancelled, TEXT("Finite control setup cancelled"));
	}
	if (IsInGameThread())
	{
		return FVoxelLandM5FiniteControlsInternals::Error(
			E::InvalidInput, TEXT("Finite controls require a worker thread"));
	}
	TSharedPtr<const FVoxelLandM5MeteredM4Identity, ESPMode::ThreadSafe> Identity;
	FVoxelLandM5Diagnostic Diagnostic = FVoxelLandM5MeteredM4Identity::Capture(Snapshot, Budget, IsCancelled, Identity);
	if (Diagnostic.Code != E::None)
	{
		return Diagnostic;
	}
	if (!Identity)
	{
		return FVoxelLandM5FiniteControlsInternals::Error(E::Unproven, TEXT("Finite identity missing"));
	}
	TSharedPtr<FVoxelLandM5BudgetLease, ESPMode::ThreadSafe> Lease;
	if (!Budget->TryReserveBytes(FVoxelLandM5FiniteControlsInternals::SetupBytes, Lease))
	{
		return FVoxelLandM5FiniteControlsInternals::Error(
			E::BudgetExceeded, TEXT("Finite control setup memory exhausted"));
	}
	TSharedPtr<FVoxelLandM5FiniteControls, ESPMode::ThreadSafe> Result =
		MakeShareable(new FVoxelLandM5FiniteControls(MoveTemp(Lease)));
	Result->Identity = MoveTemp(Identity);
	const FVoxelLandM4ControlSnapshot* Channels[] = {&Snapshot.Continental, &Snapshot.Mountain, &Snapshot.Peaks};
	const FGuid ChannelIds[] = {
		FGuid(0x564c4d34, 0x434f4e54, 1, 1), FGuid(0x564c4d34, 0x4d4f554e, 1, 1), FGuid(0x564c4d34, 0x5045414b, 1, 1)};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FVoxelLandM4ControlSnapshot& Channel = *Channels[Index];
		Diagnostic = VoxelLandM4OverrideValidation::ValidateMetered(Channel, Budget, IsCancelled);
		if (Diagnostic.Code != E::None)
		{
			return Diagnostic;
		}
		Diagnostic = VoxelLandM4CurveValidation::ValidateMetered(Channel, Budget, IsCancelled);
		if (Diagnostic.Code != E::None)
		{
			return Diagnostic;
		}
		TSharedPtr<const FVoxelLandProgram, ESPMode::ThreadSafe> Program;
		Diagnostic = FVoxelLandM5ProgramLoader::Load(Channel.Source, Channel.Payload, Budget, IsCancelled, Program);
		if (Diagnostic.Code != E::None)
		{
			return Diagnostic;
		}
		if (!Program)
		{
			return FVoxelLandM5FiniteControlsInternals::Error(E::Unproven, TEXT("Finite control program missing"));
		}
		// ValidateProgram scans a bounded instruction array in one batch.
		if (IsCancelled())
		{
			return FVoxelLandM5FiniteControlsInternals::Error(E::Cancelled, TEXT("Finite control setup cancelled"));
		}
		if (!Budget->TryChargeWork(Program->GetData().Instructions.Num() + 1))
		{
			return FVoxelLandM5FiniteControlsInternals::Error(
				E::BudgetExceeded, TEXT("Finite control program work exhausted"));
		}
		if (const FVoxelLandError Failure = VoxelLandM4ControlGrid::ValidateProgram(*Program))
		{
			return FVoxelLandM5FiniteControlsInternals::Error(E::Unproven, Failure.Message);
		}
		FVoxelLandGeneratorSettings Settings;
		Settings.Program = MoveTemp(Program);
		Settings.WorldSeed = VoxelLandDeterminism::DeriveNodeSeed(uint64(Snapshot.Seed), ChannelIds[Index], 0);
		Diagnostic = FVoxelLandM5FiniteControlsInternals::BuildProfile(
			Channel, Snapshot.VoxelSizeCm, Budget, IsCancelled, Settings.Profile);
		if (Diagnostic.Code != E::None)
		{
			return Diagnostic;
		}
		Diagnostic = FVoxelLandGenerator::CreateMetered(Settings, Budget, IsCancelled, Result->Generators[Index]);
		if (Diagnostic.Code != E::None)
		{
			return Diagnostic;
		}
		if (IsCancelled())
		{
			return FVoxelLandM5FiniteControlsInternals::Error(E::Cancelled, TEXT("Finite control setup cancelled"));
		}
		FString Reason;
		if (!FVoxelLandM4CurveLut::Create(
				Channel.CookedCurve.SamplesQ8, -512000000, 512000000, Result->Curves[Index], Reason))
		{
			return FVoxelLandM5FiniteControlsInternals::Error(E::InvalidInput, Reason);
		}
		if (IsCancelled())
		{
			return FVoxelLandM5FiniteControlsInternals::Error(E::Cancelled, TEXT("Finite control setup cancelled"));
		}
	}
	OutControls = MoveTemp(Result);
	return {};
}
