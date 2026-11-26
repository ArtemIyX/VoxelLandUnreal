#include "World/VoxelLandWorld.h"

#include "Data/Meshing/VoxelLandMaterialAppearance.h"
#include "Data/Meshing/VoxelLandWorldSignalSnapshot.h"
#include "Data/Terrain/Worlds/VoxelLandWorldConfig.h"
#include "Data/World/Streaming/VoxelLandWorldState.h"
#if !UE_SERVER
#include "Rendering/Materials/VoxelLandTerrainPresentationResolver.h"
#include "Streaming/Clipmap/VoxelLandClipmapRenderWork.h"
#endif
#include "UObject/StrongObjectPtr.h"

bool AVoxelLandWorld::SetMaterialWorldSignals(
	TSharedPtr<const FVoxelLandWorldSignalSnapshot, ESPMode::ThreadSafe> Signals, FString& Error)
{
	check(IsInGameThread());
	Error.Reset();
	if (MaterialWorldSignals == Signals)
	{
		return true;
	}
#if !UE_SERVER
	const TStrongObjectPtr<UVoxelLandWorldConfig> Config(MaterialWorldConfig.Get());
	if (State && !State->bServer && Config.IsValid() && !Config->MaterialGraph.IsNull())
	{
		const uint64 Epoch = ConfigurationEpoch;
		const TSharedPtr<const FVoxelLandWorldSignalSnapshot, ESPMode::ThreadSafe> Prior = MaterialWorldSignals;
		const FVoxelLandTerrainPresentation Replacement =
			FVoxelLandTerrainPresentationResolver::Resolve(this, *Config, Signals);
		if (ConfigurationEpoch != Epoch || !State || MaterialWorldSignals != Prior)
		{
			Error = TEXT("The world changed while material signals were being resolved.");
			return false;
		}
		if (!Replacement.Appearance || Replacement.LayoutIdentity != ActiveMaterialLayout)
		{
			Error =
				Replacement.Diagnostic.IsEmpty()
					? TEXT("The palette layout changed. Reinitialize the world before replacing its material signals.")
					: Replacement.Diagnostic;
			return false;
		}

		State->Appearance = Replacement.Appearance;
		if (VoxelLandWorldPrivate::FM7VisualState* Visual = State->M7Visual.Get())
		{
			if (Visual->Cancel)
			{
				Visual->Cancel->bCancelled.Store(true);
			}
			if (!Visual->DesiredView)
			{
				Visual->DesiredView = Visual->LastSubmittedView;
			}
			DiscardM7Staging();
		}
		else
		{
			for (TPair<FVoxelLandChunkKey, VoxelLandWorldPrivate::FRecord>& Entry : State->Records)
			{
				VoxelLandWorldPrivate::FRecord& Record = Entry.Value;
				if (!Record.bDemand && !Record.bExplicit)
				{
					continue;
				}
				Record.bFailed = false;
				Record.bQueued = true;
				if (Record.Cancel)
				{
					Record.Cancel->bCancelled.Store(true);
				}
			}
		}
	}
#endif
	MaterialWorldSignals = MoveTemp(Signals);
	return true;
}
