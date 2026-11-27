#include "World/Collision/Chaos/VoxelLandChaosTerrainComponent.h"

#include "Data/Meshing/VoxelLandChaosMesh.h"
#include "VoxelLandChaosCookProvider.h"
#include "VoxelLandChaosTerrainComponentProxy.h"
#include "World/Collision/Chaos/VoxelLandChaosCookBodySettings.h"
#include "World/Collision/Chaos/VoxelLandChaosCookResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UVoxelLandChaosTerrainComponent::UVoxelLandChaosTerrainComponent()
{
	SetMobility(EComponentMobility::Static);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetCastShadow(false);
	BodyInstance.bAutoWeld = false;
	BodyInstance.bSimulatePhysics = false;
	BodyInstance.bNotifyRigidBodyCollision = false;
}

UVoxelLandChaosTerrainComponent* UVoxelLandChaosTerrainComponent::Install(AActor& Owner, const FVector& Position,
	const FVoxelLandChaosCookResult& Cooked, const FVoxelLandChaosCookBodySettings& Settings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosInstallTerrainBody);
	check(IsInGameThread());
	UWorld* World = Owner.GetWorld();
	USceneComponent* Root = Owner.GetRootComponent();
	UBodySetup* BodySetup = Cooked.GetBodySetup();
	if (!World || World->bIsTearingDown || !World->GetPhysicsScene() || !Owner.GetActorEnableCollision() || !Root
		|| Root->Mobility != EComponentMobility::Static || Position.ContainsNaN() || !BodySetup
		|| !BodySetup->bCreatedPhysicsMeshes || BodySetup->bFailedToCreatePhysicsMeshes
		|| BodySetup->TriMeshGeometries.IsEmpty() || !Cooked.GetProvider() || !Cooked.GetMesh()
		|| Settings.CollisionEnabled == ECollisionEnabled::NoCollision)
	{
		return nullptr;
	}

	UVoxelLandChaosTerrainComponent* Component =
		NewObject<UVoxelLandChaosTerrainComponent>(&Owner, NAME_None, RF_Transient);
	Component->Initialize(Cooked, Settings);
	Component->SetupAttachment(Root);
	Component->SetRelativeLocation(Root->GetComponentTransform().InverseTransformPosition(Position));
	Owner.AddInstanceComponent(Component);
	Component->RegisterComponentWithWorld(World);
	if (!Component->IsRegistered() || !Component->IsPhysicsStateCreated()
		|| !Component->BodyInstance.IsValidBodyInstance())
	{
		Component->DestroyComponent();
		return nullptr;
	}
#if !UE_SERVER && !UE_BUILD_SHIPPING
	if (Settings.bDrawCollision)
	{
		Component->bDrawCollision = true;
		Component->MarkRenderStateDirty();
	}
#endif
	return Component;
}

void UVoxelLandChaosTerrainComponent::Initialize(
	const FVoxelLandChaosCookResult& Cooked, const FVoxelLandChaosCookBodySettings& Settings)
{
	Provider = Cooked.GetProvider();
	CookedSetup = Cooked.GetBodySetup();
	LocalBounds = FBox(Cooked.GetMesh()->Bounds);
	SetCollisionObjectType(Settings.ObjectType);
	SetCollisionResponseToChannels(Settings.Responses);
	SetCollisionEnabled(Settings.CollisionEnabled);
}

UBodySetup* UVoxelLandChaosTerrainComponent::GetBodySetup()
{
	return CookedSetup.Get();
}

FBoxSphereBounds UVoxelLandChaosTerrainComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(LocalBounds).TransformBy(LocalToWorld);
}

FPrimitiveSceneProxy* UVoxelLandChaosTerrainComponent::CreateSceneProxy()
{
#if !UE_SERVER && !UE_BUILD_SHIPPING
	if (bDrawCollision && Provider && Provider->GetMesh())
	{
		return MakeUnique<FVoxelLandChaosTerrainComponentProxy>(*this, Provider->GetMesh().ToSharedRef()).Release();
	}
#endif
	return nullptr;
}
