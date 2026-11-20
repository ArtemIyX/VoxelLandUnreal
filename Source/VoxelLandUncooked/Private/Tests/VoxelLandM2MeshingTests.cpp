#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

namespace
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
FVoxelLandChunkData Plane(FIntVector Key, int32 NormalAxis, int32 PlaneTimesTwo)
{
    FVoxelLandChunkData Chunk; Chunk.Key.Coordinate=Key;
    Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
    for (int32 Z=-1; Z<=33; ++Z) for (int32 Y=-1; Y<=33; ++Y) for (int32 X=-1; X<=33; ++X)
    {
        const int32 Coords[3]={32*Key.X+X,32*Key.Y+Y,32*Key.Z+Z};
        Chunk.Density[(X+1)+35*((Y+1)+35*(Z+1))]=int16(FMath::Clamp((2*Coords[NormalAxis]-PlaneTimesTwo)*256,-32768,32767));
    }
    return Chunk;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM2MeshTest, "VoxelLand.M2.AnalyticMesh", Flags)
bool FVoxelLandM2MeshTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("unknown bounds"), int32(VoxelLandMeshing::ClassifyBeforeSampling()), int32(EVoxelLandChunkClassification::NeedsSamples));
    FVoxelLandChunkData Uniform; Uniform.Density.Init(0,FVoxelLandChunkData::SampleCount);
    TestEqual(TEXT("zero is air"), int32(VoxelLandMeshing::Classify(Uniform)), int32(EVoxelLandChunkClassification::Empty));
    Uniform.Density.Init(-1,FVoxelLandChunkData::SampleCount);
    TestEqual(TEXT("negative is solid"), int32(VoxelLandMeshing::Classify(Uniform)), int32(EVoxelLandChunkClassification::Solid));
    Uniform.Density[0]=0;
    TestEqual(TEXT("padding contributes"), int32(VoxelLandMeshing::Classify(Uniform)), int32(EVoxelLandChunkClassification::Surface));
    TestEqual(TEXT("negative floor"), VoxelLandCoordinates::FloorDivide(-1,10), -1);
    TestEqual(TEXT("negative boundary"), VoxelLandCoordinates::FloorDivide(-10,10), -1);
    TestEqual(TEXT("negative past boundary"), VoxelLandCoordinates::FloorDivide(-11,10), -2);
    // A minimal signed feature must survive quantization and produce nonzero faces.
    for (float Scale : {50.f,100.f})
    {
        FVoxelLandChunkData Tiny; Tiny.Density.Init(32767,FVoxelLandChunkData::SampleCount);
        Tiny.Density[(16+1)+35*((16+1)+35*(16+1))]=-1;
        FVoxelLandMeshingScratch Scratch; FVoxelLandMeshData Mesh;
        TestEqual(TEXT("tiny feature builds"), int32(VoxelLandMeshing::BuildSurfaceNets(Tiny,Scale,Scratch,Mesh,[] {return false;})), int32(EVoxelLandMeshResult::Success));
        TestTrue(TEXT("tiny feature survives"), Mesh.Indices.Num()>0);
    }
    {
        FVoxelLandMeshingScratch Scratch; FVoxelLandMeshData Mesh;
        TestEqual(TEXT("unsupported meshing scale is rejected"), int32(VoxelLandMeshing::BuildSurfaceNets(Uniform,1.f,Scratch,Mesh,[] {return false;})), int32(EVoxelLandMeshResult::InvalidInput));
    }
    for (int32 Axis=0; Axis<3; ++Axis)
    {
        FVoxelLandMeshingScratch Scratch; FVoxelLandMeshData Mesh;
        const auto Result=VoxelLandMeshing::BuildSurfaceNets(Plane(FIntVector::ZeroValue,Axis,1),100.f,Scratch,Mesh,[] { return false; });
        TestEqual(TEXT("plane succeeds"), int32(Result), int32(EVoxelLandMeshResult::Success));
        TestEqual(TEXT("one quad per edge"), Mesh.Indices.Num(), 32*32*6);
        for (uint32 Index : Mesh.Indices) TestTrue(TEXT("valid index"), Index < uint32(Mesh.Vertices.Num()));
        for (const FVoxelLandMeshVertex& Vertex : Mesh.Vertices)
        {
            TestTrue(TEXT("unit normal"), FMath::IsNearlyEqual(Vertex.Normal.SizeSquared(),1.f,1.e-4f));
            TestTrue(TEXT("plane normal"), Vertex.Normal[Axis] > 0.999f);
            TestTrue(TEXT("plane coordinate"), FMath::IsNearlyEqual(Vertex.Position[Axis],50.f,0.001f));
        }
        FVoxelLandMeshData Cancelled; Cancelled.Indices.Add(7);
        TestEqual(TEXT("cancelled"), int32(VoxelLandMeshing::BuildSurfaceNets(Plane(FIntVector::ZeroValue,Axis,1),100.f,Scratch,Cancelled,[] {return true;})), int32(EVoxelLandMeshResult::Cancelled));
        TestEqual(TEXT("no partial publication"), Cancelled.Indices[0], uint32(7));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM2SeamTest, "VoxelLand.M2.NegativeChunkSeam", Flags)
bool FVoxelLandM2SeamTest::RunTest(const FString& Parameters)
{
    FVoxelLandMeshingScratch S1,S2; FVoxelLandMeshData A,B;
    auto NeverCancel=[] {return false;};
    VoxelLandMeshing::BuildSurfaceNets(Plane(FIntVector(-1,0,0),2,1),100.f,S1,A,NeverCancel);
    VoxelLandMeshing::BuildSurfaceNets(Plane(FIntVector(0,0,0),2,1),100.f,S2,B,NeverCancel);
    int32 Shared=0;
    for (const auto& VA : A.Vertices) for (const auto& VB : B.Vertices)
    {
        const FVector3f WorldA=VA.Position+FVector3f(-3200,0,0);
        if (WorldA.Equals(VB.Position,0.001f)) { ++Shared; TestTrue(TEXT("shared normal"), VA.Normal.Equals(VB.Normal,1.e-6f)); }
    }
    TestTrue(TEXT("shared halo vertices"), Shared > 0);
    // An asymmetric crossing must agree exactly after translating the negative chunk.
    for (float Scale : {50.f,100.f})
    {
        FVoxelLandChunkData Left=Plane(FIntVector(-1,0,0),2,1), Right=Plane(FIntVector(0,0,0),2,1);
        for (int32 Z=-1; Z<=33; ++Z) for (int32 Y=-1; Y<=33; ++Y) for (int32 X=-1; X<=33; ++X)
        {
            const int32 I=(X+1)+35*((Y+1)+35*(Z+1));
            Left.Density[I]=int16(FMath::Clamp((2*Z-1)*341+(X+32)*3,-32768,32767));
            Right.Density[I]=int16(FMath::Clamp((2*Z-1)*341+X*3,-32768,32767));
        }
        FVoxelLandMeshData L,R;
        VoxelLandMeshing::BuildSurfaceNets(Left,Scale,S1,L,NeverCancel);
        VoxelLandMeshing::BuildSurfaceNets(Right,Scale,S2,R,NeverCancel);
        int32 Exact=0;
        for (const auto& LV : L.Vertices) for (const auto& RV : R.Vertices)
            if (LV.Position+FVector3f(-32.f*Scale,0,0)==RV.Position) ++Exact;
        TestTrue(TEXT("asymmetric seam has exact shared coordinates"), Exact>0);
    }
    return true;
}
#endif
