#include "VoxelLandChaosProps.h"

#include "../../VoxelLandWorldCoordinates.h"
#include "Generation/Collision/VoxelLandCollisionField.h"
#include "World/Collision/Chaos/VoxelLandChaosTerrainComponent.h"
#include "World/Collision/VoxelLandCollisionWorldView.h"
#include "World/VoxelLandWorld.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool FVoxelLandChaosProps::Register(
	AVoxelLandWorld& Terrain, UPrimitiveComponent& Component, FGuid& OutId, const bool bSimulateWhenReady)
{
	check(IsInGameThread());
	const FBodyInstance* Body = Component.GetBodyInstance();
	const UBodySetup* Setup = Body ? Body->GetBodySetup() : nullptr;
	if (Props.Num() >= MaxManagedProps || Component.GetWorld() != Terrain.GetWorld() || !Component.IsRegistered()
		|| Component.GetAttachParent() || Component.IsWelded() || Component.IsA<USkinnedMeshComponent>() || !Body
		|| !Body->IsValidBodyInstance() || !Setup || Setup->AggGeom.GetElementCount() == 0
		|| Setup->GetCollisionTraceFlag() == CTF_UseComplexAsSimple
		|| Component.GetCollisionEnabled() != ECollisionEnabled::QueryAndPhysics
		|| Component.Mobility != EComponentMobility::Movable || GetDefault<UPhysicsSettings>()->bTickPhysicsAsync)
	{
		return false;
	}
	for (const TPair<FGuid, FVoxelLandChaosManagedProp>& Entry : Props)
	{
		if (Entry.Value.Component.Get() == &Component)
		{
			return false;
		}
	}
	const FGuid Id = FGuid::NewGuid();
	FVoxelLandChaosManagedProp& Prop = Props.Add(Id);
	Prop.Component = &Component;
	Prop.bWantsSimulation = bSimulateWhenReady;
	Suspend(Prop);
	Prop.bSuspended = bSimulateWhenReady;
	OutId = Id;
	return true;
}

void FVoxelLandChaosProps::Unregister(AVoxelLandWorld& Terrain, const FGuid& ConsumerId)
{
	check(IsInGameThread());
	if (Props.Remove(ConsumerId) != 0)
	{
		Terrain.ReleaseChaosCollisionLease(ConsumerId);
	}
}

void FVoxelLandChaosProps::Tick(AVoxelLandWorld& Terrain)
{
	check(IsInGameThread());
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosPropSafety);
	for (TMap<FGuid, FVoxelLandChaosManagedProp>::TIterator It = Props.CreateIterator(); It; ++It)
	{
		if (!It.Value().Component.IsValid())
		{
			Terrain.ReleaseChaosCollisionLease(It.Key());
			It.RemoveCurrent();
			continue;
		}
		UpdateProp(Terrain, It.Key(), It.Value());
	}
}

void FVoxelLandChaosProps::SuspendAll()
{
	for (TPair<FGuid, FVoxelLandChaosManagedProp>& Entry : Props)
	{
		Suspend(Entry.Value);
		Entry.Value.SafeLogicalPose.Reset();
		Entry.Value.bNeedsSupport = true;
		Entry.Value.bNeedsRecoveryCoverage = false;
	}
}

void FVoxelLandChaosProps::Reset()
{
	SuspendAll();
	Props.Reset();
}

void FVoxelLandChaosProps::Suspend(FVoxelLandChaosManagedProp& Prop)
{
	UPrimitiveComponent* Component = Prop.Component.Get();
	if (!Component || !Component->IsSimulatingPhysics())
	{
		return;
	}
	Prop.LinearVelocity = Component->GetPhysicsLinearVelocity();
	Prop.AngularVelocity = Component->GetPhysicsAngularVelocityInRadians();
	Prop.bWasAwake = Component->IsAnyRigidBodyAwake();
	Component->SetSimulatePhysics(false);
	Prop.bSuspended = true;
	++Holds;
}

void FVoxelLandChaosProps::Resume(FVoxelLandChaosManagedProp& Prop)
{
	if (!Prop.bSuspended || !Prop.bWantsSimulation)
	{
		return;
	}
	UPrimitiveComponent& Component = *Prop.Component.Get();
	Component.SetSimulatePhysics(true);
	if (!Component.IsSimulatingPhysics())
	{
		return;
	}
	Component.SetPhysicsLinearVelocity(Prop.LinearVelocity);
	Component.SetPhysicsAngularVelocityInRadians(Prop.AngularVelocity);
	if (!Prop.bWasAwake)
	{
		Component.PutRigidBodyToSleep();
	}
	Prop.bSuspended = false;
}

FBox FVoxelLandChaosProps::BuildFootprint(
	const FVoxelLandChaosManagedProp& Prop, UPrimitiveComponent& Component, const double GravityZ)
{
	const FVector Velocity = Prop.bSuspended ? Prop.LinearVelocity : Component.GetPhysicsLinearVelocity();
	const FVector Angular = Prop.bSuspended ? Prop.AngularVelocity : Component.GetPhysicsAngularVelocityInRadians();
	const FBox Bounds = Component.Bounds.GetBox();
	if (Velocity.ContainsNaN() || Angular.ContainsNaN() || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN())
	{
		return FBox(ForceInit);
	}

	// Adding the gravity envelope independently includes a possible apex between the endpoints.
	const FVector Travel = Velocity * PredictionSeconds;
	const double Fall = Component.IsGravityEnabled() ? 0.5 * GravityZ * FMath::Square(PredictionSeconds) : 0.0;
	const FVector Pivot = Component.GetCenterOfMass();
	const double Radius = Bounds.GetExtent().Size() + FVector::Distance(Bounds.GetCenter(), Pivot);
	const double Rotation = Radius * FMath::Min(2.0, Angular.Size() * PredictionSeconds);
	const FBox End(Bounds.Min + Travel, Bounds.Max + Travel);
	const double RecoveryMargin = Prop.bNeedsRecoveryCoverage ? RecoveryLimitCm : 0.0;
	FBox Result = (Bounds + End).ExpandBy(Rotation + MinimumMarginCm + RecoveryMargin);
	Result.Min.Z += FMath::Min(0.0, Fall) - (Prop.bNeedsSupport ? SpawnProbeCm : 0.0);
	Result.Max.Z += FMath::Max(0.0, Fall);
	return Result;
}

bool FVoxelLandChaosProps::IsSupportHit(const FHitResult& Hit)
{
	return Hit.bBlockingHit && !Hit.bStartPenetrating && Hit.ImpactNormal.Z > 0
		&& Cast<UVoxelLandChaosTerrainComponent>(Hit.GetComponent());
}

bool FVoxelLandChaosProps::HasTerrainSupport(UPrimitiveComponent& Component)
{
	const UWorld* World = Component.GetWorld();
	FComponentQueryParams Params(SCENE_QUERY_STAT(VoxelLandChaosSupport), Component.GetOwner());
	Params.bTraceComplex = true;
	TArray<FHitResult> Hits;
	const FVector Location = Component.GetComponentLocation();
	const FVector Start = Location + FVector(0, 0, MinimumMarginCm);
	const FVector End = Location - FVector(0, 0, SpawnProbeCm);
	World->ComponentSweepMulti(Hits, &Component, Start, End, Component.GetComponentQuat(), Params);
	for (const FHitResult& Hit : Hits)
	{
		if (IsSupportHit(Hit))
		{
			return true;
		}
	}
	// A suspended component may not expose sweepable physics geometry until it wakes.
	FHitResult Support;
	const FCollisionResponseParams Responses(Component.GetCollisionResponseToChannels());
	return World->LineTraceSingleByChannel(Support, Start, End, Component.GetCollisionObjectType(), Params, Responses)
		&& IsSupportHit(Support) && IsPoseClear(Component, Component.GetComponentTransform());
}

bool FVoxelLandChaosProps::IsPoseClear(UPrimitiveComponent& Component, const FTransform& Pose)
{
	FComponentQueryParams Params(SCENE_QUERY_STAT(VoxelLandChaosClearance), Component.GetOwner());
	Params.bTraceComplex = true;
	TArray<FOverlapResult> Overlaps;
	return !Component.ComponentOverlapMulti(Overlaps, Component.GetWorld(), Pose.GetLocation(), Pose.GetRotation(),
		Component.GetCollisionObjectType(), Params);
}

void FVoxelLandChaosProps::UpdateProp(AVoxelLandWorld& Terrain, const FGuid& Id, FVoxelLandChaosManagedProp& Prop)
{
	UPrimitiveComponent& Component = *Prop.Component.Get();
	const UWorld* World = Terrain.GetWorld();
	FVector Logical;
	if (!VoxelLandWorldCoordinates::ToGlobal(Component.GetComponentLocation(), World->OriginLocation, Logical))
	{
		Suspend(Prop);
		return;
	}
	if (Prop.PreviousLogicalLocation
		&& FVector::DistSquared(Logical, Prop.PreviousLogicalLocation.GetValue()) > FMath::Square(TeleportThresholdCm))
	{
		Prop.bNeedsSupport = true;
		Prop.bNeedsRecoveryCoverage = false;
		Prop.SafeLogicalPose.Reset();
		Prop.LinearVelocity = FVector::ZeroVector;
		Prop.AngularVelocity = FVector::ZeroVector;
	}
	Prop.PreviousLogicalLocation = Logical;

	const FBox Footprint = BuildFootprint(Prop, Component, World->GetGravityZ());
	if (!Footprint.IsValid || Terrain.UpdateChaosCollisionLease(Id, Footprint) != EVoxelLandChaosReadiness::Ready)
	{
		Suspend(Prop);
		return;
	}

	if (!Prop.bWantsSimulation || (!Prop.bSuspended && !Component.IsSimulatingPhysics()))
	{
		return;
	}
	FVoxelLandCollisionWorldView View;
	if (!Terrain.CaptureChaosCollisionView(Id, View) || !View.Field)
	{
		Suspend(Prop);
		return;
	}
	const FVector Bottom(Component.Bounds.Origin.X, Component.Bounds.Origin.Y, Component.Bounds.GetBox().Min.Z);
	FVector LogicalBottom;
	double Density = 0;
	if (!VoxelLandWorldCoordinates::ToGlobal(Bottom, View.OriginLocation, LogicalBottom)
		|| View.Field->Sample(LogicalBottom, Density) != EVoxelLandCollisionFieldStatus::Available)
	{
		Suspend(Prop);
		return;
	}
	if (Density < -PenetrationThresholdVoxels)
	{
		if (!Prop.bNeedsRecoveryCoverage)
		{
			Prop.bNeedsRecoveryCoverage = true;
			Suspend(Prop);
			return;
		}
		if (Recover(Prop, View))
		{
			++Recoveries;
		}
		else
		{
			Suspend(Prop);
			if (!Prop.bReportedUnrecoverable)
			{
				UE_LOG(LogTemp, Warning, TEXT("VoxelLand held managed prop %s after terrain penetration"),
					*Component.GetName());
				Prop.bReportedUnrecoverable = true;
			}
			return;
		}
	}
	if (Prop.bNeedsSupport && !HasTerrainSupport(Component))
	{
		Suspend(Prop);
		return;
	}
	Prop.bNeedsSupport = false;
	Prop.bNeedsRecoveryCoverage = false;
	Prop.bReportedUnrecoverable = false;
	Resume(Prop);
	RememberSafePose(Prop, View);
}

bool FVoxelLandChaosProps::Recover(FVoxelLandChaosManagedProp& Prop, const FVoxelLandCollisionWorldView& View)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosPropRecovery);
	UPrimitiveComponent& Component = *Prop.Component.Get();
	const FVector Location = Component.GetComponentLocation();
	const FVector Bottom(Component.Bounds.Origin.X, Component.Bounds.Origin.Y, Component.Bounds.GetBox().Min.Z);
	FVector LogicalBottom;
	FVector Gradient;
	double Density = 0;
	const double SkinVoxels = 0.1;
	if (VoxelLandWorldCoordinates::ToGlobal(Bottom, View.OriginLocation, LogicalBottom)
		&& View.Field->Sample(LogicalBottom, Density, &Gradient) == EVoxelLandCollisionFieldStatus::Available
		&& !Gradient.ContainsNaN())
	{
		const double Slope = Gradient.Size();
		const FVector Normal = Gradient.GetSafeNormal();
		const double StepCm = (SkinVoxels - Density) * View.Field->GetVoxelSizeCm() / FMath::Max(Slope, 0.01);
		if (Slope > 0.01 && StepCm > 0 && StepCm <= RecoveryLimitCm)
		{
			FTransform Candidate = Component.GetComponentTransform();
			Candidate.SetLocation(Location + Normal * StepCm);
			double RevisedDensity = 0;
			if (View.Field->Sample(LogicalBottom + Normal * StepCm, RevisedDensity)
					== EVoxelLandCollisionFieldStatus::Available
				&& RevisedDensity >= 0 && IsPoseClear(Component, Candidate))
			{
				Component.SetWorldTransform(Candidate, false, nullptr, ETeleportType::TeleportPhysics);
				FVector Velocity = Prop.bSuspended ? Prop.LinearVelocity : Component.GetPhysicsLinearVelocity();
				Velocity -= Normal * FMath::Min(0.0, FVector::DotProduct(Velocity, Normal));
				Prop.LinearVelocity = Velocity;
				if (!Prop.bSuspended)
				{
					Component.SetPhysicsLinearVelocity(Velocity);
				}
				return true;
			}
		}
	}
	if (!Prop.SafeLogicalPose || Prop.SafePoseEpoch != View.GetConfigurationEpoch())
	{
		return false;
	}
	FTransform Safe = Prop.SafeLogicalPose.GetValue();
	FVector SafeLocal;
	if (!VoxelLandWorldCoordinates::ToLocal(Safe.GetLocation(), View.OriginLocation, SafeLocal)
		|| FVector::DistSquared(Location, SafeLocal) > FMath::Square(RecoveryLimitCm))
	{
		return false;
	}
	Safe.SetLocation(SafeLocal);
	const FBox SafeBounds = Component.CalcBounds(Safe).GetBox();
	const FVector SafeBottom(SafeBounds.GetCenter().X, SafeBounds.GetCenter().Y, SafeBounds.Min.Z);
	FVector SafeLogicalBottom;
	double SafeDensity = 0;
	if (!VoxelLandWorldCoordinates::ToGlobal(SafeBottom, View.OriginLocation, SafeLogicalBottom)
		|| View.Field->Sample(SafeLogicalBottom, SafeDensity) != EVoxelLandCollisionFieldStatus::Available
		|| SafeDensity < -0.05 || !IsPoseClear(Component, Safe))
	{
		return false;
	}
	Component.SetWorldTransform(Safe, false, nullptr, ETeleportType::TeleportPhysics);
	Prop.LinearVelocity = FVector::ZeroVector;
	Prop.AngularVelocity = FVector::ZeroVector;
	if (!Prop.bSuspended)
	{
		Component.SetPhysicsLinearVelocity(FVector::ZeroVector);
		Component.SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	}
	return true;
}

void FVoxelLandChaosProps::RememberSafePose(FVoxelLandChaosManagedProp& Prop, const FVoxelLandCollisionWorldView& View)
{
	UPrimitiveComponent& Component = *Prop.Component.Get();
	FTransform Pose = Component.GetComponentTransform();
	FVector Logical;
	if (!VoxelLandWorldCoordinates::ToGlobal(Pose.GetLocation(), View.OriginLocation, Logical))
	{
		return;
	}
	constexpr double MinimumPositionChangeCm = 2.0;
	if (Prop.SafeLogicalPose && Prop.SafePoseEpoch == View.GetConfigurationEpoch()
		&& FVector::DistSquared(Prop.SafeLogicalPose->GetLocation(), Logical) <= FMath::Square(MinimumPositionChangeCm)
		&& Pose.GetRotation().Equals(Prop.SafeLogicalPose->GetRotation(), UE_KINDA_SMALL_NUMBER))
	{
		return;
	}
	const FVector Bottom(Component.Bounds.Origin.X, Component.Bounds.Origin.Y, Component.Bounds.GetBox().Min.Z);
	FVector LogicalBottom;
	double Density = 0;
	if (!VoxelLandWorldCoordinates::ToGlobal(Bottom, View.OriginLocation, LogicalBottom)
		|| View.Field->Sample(LogicalBottom, Density) != EVoxelLandCollisionFieldStatus::Available || Density < -0.05
		|| Density > 1.0 || !IsPoseClear(Component, Pose))
	{
		return;
	}
	Pose.SetLocation(Logical);
	Prop.SafeLogicalPose = Pose;
	Prop.SafePoseEpoch = View.GetConfigurationEpoch();
}
