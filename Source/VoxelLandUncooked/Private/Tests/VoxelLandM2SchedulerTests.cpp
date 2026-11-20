#if WITH_DEV_AUTOMATION_TESTS
#include "VoxelLandM1Fixtures.h"
#include "World/VoxelLandWorld.h"
#include "Rendering/VoxelLandChunkComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace
{
class FVoxelLandM2SchedulerCommand final : public IAutomationLatentCommand
{
public:
    FVoxelLandM2SchedulerCommand(FAutomationTestBase* InTest,UWorld* InWorld,AVoxelLandWorld* InActor,
        TSharedPtr<const FVoxelLandGenerator,ESPMode::ThreadSafe> InGenerator,uint64 InHash)
        : Test(InTest), World(InWorld), Actor(InActor), Generator(MoveTemp(InGenerator)), ExpectedHash(InHash), Started(FPlatformTime::Seconds()) {}
    virtual bool Update() override
    {
        if (!IsValid(World) || !IsValid(Actor)) { Test->AddError(TEXT("M2 test world unexpectedly destroyed")); Cleanup(); return true; }
        if (FPlatformTime::Seconds()-Started>30.0) { Test->AddError(TEXT("M2 scheduler completion timed out")); Cleanup(); return true; }
        Actor->Tick(0.016f);
        if (Stage==0)
        {
            // Retire an in-flight request and immediately reuse its key.
            Actor->ReleaseChunk(Key); Actor->Tick(0.016f);
            Test->TestTrue(TEXT("re-request after cancellation"),Actor->RequestChunk(Key));
            Stage=1; return false;
        }
        if (Stage==1)
        {
            const auto Data=Actor->FindChunk(Key); if (!Data) return false;
            Test->TestEqual(TEXT("reused key publishes current density"),Data->Hash,ExpectedHash);
            int32 RegionCount=0; for (TActorIterator<AVoxelLandRegion> It(World); It; ++It) ++RegionCount;
            Test->TestEqual(TEXT("data-only path creates no regions"),RegionCount,0);
            // Replace the snapshot with another request in flight; shutdown must join workers.
            FVoxelLandChunkKey Other; Other.Coordinate=FIntVector(1,0,0);
            Actor->RequestChunk(Other); Actor->Tick(0.016f);
            Actor->Initialize(Generator);
            Test->TestNull(TEXT("snapshot replacement clears previous field"),Actor->FindChunk(Key).Get());
            Test->TestTrue(TEXT("request after replacement"),Actor->RequestChunk(Key));
            Stage=2; return false;
        }
        const auto Data=Actor->FindChunk(Key); if (!Data) return false;
        Test->TestEqual(TEXT("replacement field"),Data->Hash,ExpectedHash);
        // Exercise actor/component lifetime on a scene, including visibility and material changes.
        AVoxelLandRegion* Region=World->SpawnActor<AVoxelLandRegion>();
        Test->TestNotNull(TEXT("region actor"),Region);
        if (Region)
        {
            auto Mesh=MakeShared<FVoxelLandMeshData,ESPMode::ThreadSafe>();
            FVoxelLandMeshVertex A,B,C; B.Position=FVector3f(100,0,0); C.Position=FVector3f(0,100,0);
            Mesh->Vertices={A,B,C}; Mesh->Indices={0,1,2};
            Mesh->Bounds+=A.Position; Mesh->Bounds+=B.Position; Mesh->Bounds+=C.Position;
            UMaterialInterface* Initial=UMaterial::GetDefaultMaterial(MD_Surface);
            Test->TestTrue(TEXT("region publishes mesh"),Region->Publish(FIntVector::ZeroValue,FVector::ZeroVector,Mesh,Initial));
            TInlineComponentArray<UVoxelLandChunkComponent*> Components; Region->GetComponents(Components);
            Test->TestEqual(TEXT("one component per surface"),Components.Num(),1);
            Region->SetActorHiddenInGame(true); Test->TestTrue(TEXT("region hidden"),Region->IsHidden());
            Region->SetActorHiddenInGame(false); Test->TestFalse(TEXT("region restored"),Region->IsHidden());
            UMaterialInstanceDynamic* Replacement=UMaterialInstanceDynamic::Create(Initial,Region);
            Region->SetTerrainMaterial(Replacement);
            if (Components.Num()==1) Test->TestEqual(TEXT("region updates chunk material"),Components[0]->GetMaterial(0),static_cast<UMaterialInterface*>(Replacement));
            Region->Remove(FIntVector::ZeroValue);
            Test->TestTrue(TEXT("removal clears region"),Region->IsEmpty());
            Region->Destroy();
            // Drain deferred create/release work before world destruction. This checks lifecycle,
            // but visual output and GPU leak measurements still need a real RHI run.
            FlushRenderingCommands();
        }
        FVoxelLandChunkKey Other; Other.Coordinate=FIntVector(-1,0,0);
        Actor->RequestChunk(Other); Actor->Tick(0.016f);
        Cleanup(); return true;
    }
private:
    void Cleanup()
    {
        if (IsValid(Actor)) { Actor->Shutdown(); Actor->Destroy(); }
        if (IsValid(World))
        {
            GEngine->ShutdownWorldNetDriver(World); World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World); World->RemoveFromRoot();
        }
        Actor=nullptr; World=nullptr;
    }
    FAutomationTestBase* Test; UWorld* World; AVoxelLandWorld* Actor;
    TSharedPtr<const FVoxelLandGenerator,ESPMode::ThreadSafe> Generator;
    uint64 ExpectedHash; double Started; int32 Stage=0; FVoxelLandChunkKey Key;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM2SchedulerTest,"VoxelLand.M2.SchedulerDataOnly",EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM2SchedulerTest::RunTest(const FString& Parameters)
{
    const auto Fixture=Async(EAsyncExecution::ThreadPool,[] {
        return VoxelLandM1Fixtures::Generate(VoxelLandM1Fixtures::Plane(),FVoxelLandZoneProfile(),FVoxelLandChunkKey(),FVoxelLandBackendOptions());
    }).Get();
    if (Fixture.Error || !Fixture.Generator || !Fixture.Chunk) { AddError(TEXT("Unable to initialize scheduler fixture")); return false; }
    if (!GEngine) { AddError(TEXT("Engine unavailable")); return false; }
    FWorldContext& Context=GEngine->CreateNewWorldContext(EWorldType::Game);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if (!World) { AddError(TEXT("Unable to create test world")); return false; }
    World->AddToRoot(); Context.SetCurrentWorld(World); World->InitializeActorsForPlay(FURL());
    AVoxelLandWorld* Actor=World->SpawnActor<AVoxelLandWorld>();
    if (!Actor) { AddError(TEXT("Unable to spawn terrain actor")); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->RemoveFromRoot(); return false; }
    Actor->bStartDemo=false; Actor->bDataOnly=true; Actor->WorkerCount=2; Actor->Initialize(Fixture.Generator);
    FVoxelLandChunkKey Key; if (!Actor->RequestChunk(Key)) { AddError(TEXT("Unable to request fixture chunk")); Actor->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World->RemoveFromRoot(); return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FVoxelLandM2SchedulerCommand(this,World,Actor,Fixture.Generator,Fixture.Chunk->Hash));
    return true;
}
#endif
