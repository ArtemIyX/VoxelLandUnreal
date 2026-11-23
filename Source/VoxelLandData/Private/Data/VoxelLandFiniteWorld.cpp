#include "Data/Assets/VoxelLandFiniteWorld.h"

#include "Data/Assets/VoxelLandM5ProtectionAsset.h"
#include "Data/Structs/VoxelLandFiniteWorldSnapshot.h"
#include "UObject/StrongObjectPtr.h"

bool UVoxelLandFiniteWorld::CaptureSnapshot(FVoxelLandFiniteWorldSnapshot& OutSnapshot, FString& OutError) const
{
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("Finite world settings must be captured on the game thread.");
		return false;
	}
	UVoxelLandM5ProtectionAsset* const AuthoredMask = ProtectionMask.Get();
	if (AuthoredMask && !IsValid(AuthoredMask))
	{
		OutError = TEXT("Finite protection asset is invalid.");
		return false;
	}
	TStrongObjectPtr<UVoxelLandM5ProtectionAsset> MaskGuard(AuthoredMask);
	const FVoxelLandFiniteWorldSettings Authored = Finite;
	constexpr int64 ExactControlLimit = 16777216;
	constexpr int64 HeightLimitQ8 = 512000000;
	if (Authored.CellsX < 1 || Authored.CellsX > 1024 || Authored.CellsY < 1 || Authored.CellsY > 1024
		|| Authored.GridSpacingCm < 1 || Authored.GridSpacingCm > 134217
		|| !HasRepresentableVerticalExtent(Authored.MinZCm, Authored.MaxZCm)
		|| Authored.OriginXCm < -ExactControlLimit || Authored.OriginXCm > ExactControlLimit
		|| Authored.OriginYCm < -ExactControlLimit || Authored.OriginYCm > ExactControlLimit
		|| Authored.GridSpacingCm > (ExactControlLimit - Authored.OriginXCm) / Authored.CellsX
		|| Authored.GridSpacingCm > (ExactControlLimit - Authored.OriginYCm) / Authored.CellsY
		|| Authored.DropletCount < 0 || Authored.DropletCount > 65536 || Authored.MaxSteps < 0
		|| Authored.MaxSteps > 128 || Authored.ThermalIterations < 0 || Authored.ThermalIterations > 32
		|| Authored.CapacityMaxQ8 < 0 || Authored.CapacityMaxQ8 > HeightLimitQ8 || Authored.TalusQ8 < 0
		|| Authored.TalusQ8 > HeightLimitQ8 || Authored.MaxTransferQ8 < 0 || Authored.MaxTransferQ8 > HeightLimitQ8
		|| Authored.ErosionSalt < 0 || (Authored.bGlobalErosion && !Authored.ErosionStageId.IsValid()))
	{
		OutError =
			TEXT("Finite world grid or erosion settings are invalid or exceed the supported control-grid domain.");
		return false;
	}
	FVoxelLandFiniteWorldSnapshot Scratch;
	const auto IsModeCurrent = [this, &Authored, AuthoredMask]() {
		return ProtectionMask.Get() == AuthoredMask && (!AuthoredMask || IsValid(AuthoredMask))
			&& FVoxelLandFiniteWorldSettings::StaticStruct()->CompareScriptStruct(&Finite, &Authored, 0);
	};
	if (!CaptureCommon(EVoxelLandWorldKind::Finite, IsModeCurrent, Scratch.Common, OutError))
	{
		return false;
	}
	if (!IsModeCurrent())
	{
		OutError = TEXT("Finite world settings changed during capture.");
		return false;
	}
	// A present mask is an effective requirement, even when the authored flag
	// permits omission. Existing M5 identity validation rejects it until the
	// mask data is bound; no payload or UObject enters this snapshot.
	if (AuthoredMask)
	{
		Scratch.Common.Caves.Protection.bRequireMask = true;
	}
	Scratch.Finite = Authored;
	// The last height-grid sample is inclusive; use a half-open centimetre
	// extent. Later demand checks still validate every required halo.
	Scratch.Common.Caves.Bounds.MinInclusive.X = Authored.OriginXCm;
	Scratch.Common.Caves.Bounds.MinInclusive.Y = Authored.OriginYCm;
	Scratch.Common.Caves.Bounds.MinInclusive.Z = Authored.MinZCm;
	Scratch.Common.Caves.Bounds.MaxExclusive.X = Authored.OriginXCm + Authored.GridSpacingCm * Authored.CellsX + 1;
	Scratch.Common.Caves.Bounds.MaxExclusive.Y = Authored.OriginYCm + Authored.GridSpacingCm * Authored.CellsY + 1;
	Scratch.Common.Caves.Bounds.MaxExclusive.Z = Authored.MaxZCm;
	OutSnapshot = MoveTemp(Scratch);
	return true;
}
