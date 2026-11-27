#include "World/Collision/Chaos/VoxelLandChaosCook.h"

#include "Data/Meshing/VoxelLandChaosMesh.h"
#include "VoxelLandChaosCookProvider.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/Package.h"

#if !UE_BUILD_SHIPPING
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

bool FVoxelLandChaosCook::Start(TSharedRef<const FVoxelLandChaosMesh, ESPMode::ThreadSafe> Mesh, FCompletion Completion)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosSubmitCook);
	check(IsInGameThread());
	if (!Completion || !Mesh->Bounds.IsValid || Mesh->Vertices.IsEmpty() || Mesh->Indices.IsEmpty()
		|| Mesh->Indices.Num() % 3 != 0)
	{
		return false;
	}

	// The worker owns geometric validation. Reject oversized input before the synchronous provider copy.
	constexpr int32 MaxVertices = 128 * 1024;
	constexpr int32 MaxIndices = 768 * 1024;
	if (Mesh->Vertices.Num() > MaxVertices || Mesh->Indices.Num() > MaxIndices)
	{
		return false;
	}

	UVoxelLandChaosCookProvider* Provider =
		NewObject<UVoxelLandChaosCookProvider>(GetTransientPackage(), NAME_None, RF_Transient);
	Provider->SetMesh(MoveTemp(Mesh));
	UBodySetup* BodySetup = NewObject<UBodySetup>(Provider, NAME_None, RF_Transient);
	BodySetup->BodySetupGuid = FGuid::NewGuid();
	BodySetup->bGenerateMirroredCollision = false;
	BodySetup->bDoubleSidedGeometry = true;
	BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;

	const TSharedRef<FVoxelLandChaosCookResult, ESPMode::ThreadSafe> Result =
		MakeShared<FVoxelLandChaosCookResult, ESPMode::ThreadSafe>(*Provider, *BodySetup);
	// Do not abort in-flight UE cooks: UE 5.7 can suppress their completion delegate.
	BodySetup->CreatePhysicsMeshesAsync(FOnAsyncPhysicsCookFinished::CreateStatic(
		&FVoxelLandChaosCook::Finish, Result, MakeShared<FCompletion, ESPMode::ThreadSafe>(MoveTemp(Completion))));
	return true;
}

void FVoxelLandChaosCook::Finish(bool bSuccess, TSharedRef<FVoxelLandChaosCookResult, ESPMode::ThreadSafe> Result,
	TSharedRef<FCompletion, ESPMode::ThreadSafe> Completion)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosCompleteCook);
	check(IsInGameThread());
	if (!bSuccess || Result->GetBodySetup()->TriMeshGeometries.IsEmpty())
	{
		(*Completion)(nullptr);
		return;
	}

#if !UE_BUILD_SHIPPING
	const float Delay = GetDeliveryDelay();
	if (Delay > 0.f)
	{
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&FVoxelLandChaosCook::DeliverDelayed, Result, Completion), Delay);
		return;
	}
#endif
	(*Completion)(Result);
}

#if !UE_BUILD_SHIPPING
float FVoxelLandChaosCook::GetDeliveryDelay()
{
	int32 Milliseconds = 0;
	if (!FParse::Value(FCommandLine::Get(), TEXT("VoxelLandChaosCookDelayMs="), Milliseconds))
	{
		return 0.f;
	}
	constexpr int32 MaxDelayMilliseconds = 30'000;
	constexpr float MillisecondsPerSecond = 1000.f;
	return static_cast<float>(FMath::Clamp(Milliseconds, 0, MaxDelayMilliseconds)) / MillisecondsPerSecond;
}

bool FVoxelLandChaosCook::DeliverDelayed(float DeltaSeconds,
	TSharedRef<FVoxelLandChaosCookResult, ESPMode::ThreadSafe> Result,
	TSharedRef<FCompletion, ESPMode::ThreadSafe> Completion)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_ChaosDeliverDelayedCook);
	check(IsInGameThread());
	(*Completion)(Result);
	return false;
}
#endif
