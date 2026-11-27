#include "VoxelLandPCGCell.h"

#include "VoxelLandPCGLayer.h"
#include "VoxelLandPCGTerrainData.h"
#include "Misc/EngineVersionComparison.h"

#if PLATFORM_WINDOWS && ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 7
// UE 5.7 places this experimental attribute before the class keyword, which MSVC rejects.
#pragma push_macro("UE_EXPERIMENTAL")
#undef UE_EXPERIMENTAL
#define UE_EXPERIMENTAL(Version, Message)
#include "Components/InstancedSkinnedMeshComponent.h"
#pragma pop_macro("UE_EXPERIMENTAL")
#else
#include "Components/InstancedSkinnedMeshComponent.h"
#endif
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Data/PCGBasePointData.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

AVoxelLandPCGCell::AVoxelLandPCGCell()
{
	UBoxComponent* CellEnvelope = CreateDefaultSubobject<UBoxComponent>(TEXT("PlacementBounds"));
	CellEnvelope->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CellEnvelope->SetGenerateOverlapEvents(false);
	CellEnvelope->SetCanEverAffectNavigation(false);
	CellEnvelope->SetHiddenInGame(true);
	SetRootComponent(CellEnvelope);
	Execution = CreateDefaultSubobject<UPCGComponent>(TEXT("Execution"));
	Execution->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
	Execution->bGenerateOnDropWhenTriggerOnDemand = false;
	SetReplicates(false);
}

bool AVoxelLandPCGCell::Start(UPCGVoxelTerrainData* Snapshot, UPCGGraphInterface* Graph, const int32 Seed)
{
	check(IsInGameThread());
	if (!Snapshot || !Graph || Execution->IsGenerating())
	{
		return false;
	}
	TerrainData = Snapshot;
	CastChecked<UBoxComponent>(GetRootComponent())->SetBoxExtent(Snapshot->GetLogicalBounds().GetExtent(), false);
	Execution->Seed = Seed;
	Execution->SetIsPartitioned(false);
	Execution->SetGraphLocal(Graph);
	return Execution->GenerateLocalGetTaskId(true) != InvalidPCGTaskId;
}

bool AVoxelLandPCGCell::IsGenerating() const
{
	return Execution->IsGenerating();
}

bool AVoxelLandPCGCell::Collect(
	TArray<FTransform>& Out, const FBox& Ownership, const FIntVector& CapturedOrigin, const int32 MaxInstances)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCG_CollectOutput);
	if (!Execution->bGenerated)
	{
		return false;
	}
	const FVector LogicalOffset(CapturedOrigin);
	for (const FPCGTaggedData& Output : Execution->GetGeneratedGraphOutput().TaggedData)
	{
		const UPCGBasePointData* Points = Cast<UPCGBasePointData>(Output.Data);
		if (!Points)
		{
			continue;
		}
		if (Points->GetNumPoints() > MaxInstances - Out.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Points->GetNumPoints(); ++Index)
		{
			FTransform Transform = Points->GetTransform(Index);
			Transform.AddToTranslation(LogicalOffset);
			const FVector Position = Transform.GetLocation();
			const float Density = Points->GetDensity(Index);
			FRandomStream DensityRandom(HashCombineFast(GetTypeHash(Position), GetTypeHash(Points->GetSeed(Index))));
			if (Transform.ContainsNaN() || !FMath::IsFinite(Density) || Density <= 0.0f
				|| (Density < 1.0f && DensityRandom.GetFraction() >= Density) || Position.X < Ownership.Min.X
				|| Position.X >= Ownership.Max.X || Position.Y < Ownership.Min.Y || Position.Y >= Ownership.Max.Y
				|| Position.Z < Ownership.Min.Z || Position.Z >= Ownership.Max.Z)
			{
				continue;
			}
			Out.Add(Transform);
		}
	}
	return true;
}

#if WITH_EDITOR
void AVoxelLandPCGCell::RecordDebugPoints(TConstArrayView<FTransform> Output)
{
	DebugCandidates = TerrainData ? TerrainData->GetDebugCandidates() : TArray<FVector>();
	constexpr int32 MaxAccepted = 64;
	DebugAccepted.Reset();
	const int32 Count = FMath::Min(MaxAccepted, Output.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		DebugAccepted.Add(Output[Index].GetLocation());
	}
}

int32 AVoxelLandPCGCell::DrawDebugPoints(const int32 MaxPoints) const
{
	const FVector Offset(GetWorld()->OriginLocation);
	int32 Drawn = 0;
	for (const FVector& Candidate : DebugCandidates)
	{
		if (Drawn == MaxPoints)
		{
			return Drawn;
		}
		DrawDebugPoint(GetWorld(), Candidate - Offset, 9.0f, FColor::Yellow, false, 0.3f);
		++Drawn;
	}
	for (const FVector& Accepted : DebugAccepted)
	{
		if (Drawn == MaxPoints)
		{
			break;
		}
		DrawDebugPoint(GetWorld(), Accepted - Offset + FVector(0.0, 0.0, 3.0), 12.0f, FColor::Green, false, 0.3f);
		++Drawn;
	}
	return Drawn;
}
#endif

void AVoxelLandPCGCell::StopExecution()
{
	check(IsInGameThread());
	if (TerrainData)
	{
		TerrainData->Cancel();
	}
	Execution->CancelGeneration();
	Execution->CleanupLocalImmediate(true);
	TerrainData = nullptr;
}

void AVoxelLandPCGCell::Publish(const FVoxelLandPCGLayer& Layer, const TArray<FTransform>& LogicalTransforms)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_PCG_PublishInstances);
	if (LogicalTransforms.IsEmpty())
	{
		return;
	}
	const FVector LogicalRoot = GetActorLocation() + FVector(GetWorld()->OriginLocation);
	TArray<FTransform> Relative;
	Relative.Reserve(LogicalTransforms.Num());
	for (FTransform Transform : LogicalTransforms)
	{
		Transform.AddToTranslation(-LogicalRoot);
		Relative.Add(Transform);
	}

	const int32 CullDistance = FMath::RoundToInt(FMath::Clamp(Layer.VisibilityDistanceCm, 0.0, 10000000.0));
	if (Layer.SkeletalMesh)
	{
		UInstancedSkinnedMeshComponent* Skinned =
			NewObject<UInstancedSkinnedMeshComponent>(this, NAME_None, RF_Transient);
		Skinned->SetSkinnedAsset(Layer.SkeletalMesh);
		Skinned->SetCullDistances(0, CullDistance);
		TArray<int32> AnimationIndices;
		AnimationIndices.Init(INDEX_NONE, Relative.Num());
		Skinned->AddInstances(Relative, AnimationIndices, false);
		Instances = Skinned;
	}
	else if (Layer.StaticMesh)
	{
		UInstancedStaticMeshComponent* Static = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Static->SetStaticMesh(Layer.StaticMesh);
		Static->SetCullDistances(0, CullDistance);
		Static->AddInstances(Relative, false, false, false);
		Instances = Static;
	}
	if (Instances)
	{
		Instances->SetupAttachment(GetRootComponent());
		Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Instances->SetGenerateOverlapEvents(false);
		Instances->SetCanEverAffectNavigation(false);
		AddInstanceComponent(Instances);
		Instances->RegisterComponent();
		InstanceCount = Relative.Num();
	}
}
