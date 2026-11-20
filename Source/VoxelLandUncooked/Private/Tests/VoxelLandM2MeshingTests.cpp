#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/VoxelLandMeshing.h"
#include "Data/Structs/VoxelLandPublication.h"
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
    for (float Scale : {1.f,50.f,75.3f,100.f})
    {
        FVoxelLandChunkData Tiny; Tiny.Density.Init(32767,FVoxelLandChunkData::SampleCount);
        Tiny.Density[(16+1)+35*((16+1)+35*(16+1))]=-1;
        FVoxelLandMeshingScratch Scratch; FVoxelLandMeshData Mesh;
        TestEqual(TEXT("tiny feature builds"), int32(VoxelLandMeshing::BuildSurfaceNets(Tiny,Scale,Scratch,Mesh,[] {return false;})), int32(EVoxelLandMeshResult::Success));
        TestTrue(TEXT("tiny feature survives"), Mesh.Indices.Num()>0);
    }

    TestTrue(TEXT("precision range includes negative edge"),VoxelLandMeshCoordinates::IsKeyInPrecisionRange(FIntVector(-32766,0,0)));
    TestFalse(TEXT("precision range rejects far key"),VoxelLandMeshCoordinates::IsKeyInPrecisionRange(FIntVector(32767,0,0)));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM2PublicationTest, "VoxelLand.M2.StalePublication", Flags)
bool FVoxelLandM2PublicationTest::RunTest(const FString& Parameters)
{
    using VoxelLandPublication::IsCurrent;
    TestTrue(TEXT("current completion"),IsCurrent(true,4,4,9,9,true,true));
    TestFalse(TEXT("old epoch"),IsCurrent(true,3,4,9,9,true,true));
    TestFalse(TEXT("old ticket after reuse"),IsCurrent(true,4,4,8,9,true,true));
    TestFalse(TEXT("evicted record"),IsCurrent(false,4,4,9,9,true,true));
    TestFalse(TEXT("replaced generator"),IsCurrent(true,4,4,9,9,false,true));
    TestFalse(TEXT("no longer wanted"),IsCurrent(true,4,4,9,9,true,false));
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
    for (float Scale : {1.f,50.f,75.3f,100.f})
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
        int32 Shared=0;
        const FVector OriginL=VoxelLandMeshCoordinates::RenderOrigin(FIntVector(-1,0,0),Scale);
        const FVector OriginR=VoxelLandMeshCoordinates::RenderOrigin(FIntVector(0,0,0),Scale);
        for (const auto& LV : L.Vertices)
        {
            const FVector WorldL=FVector(LV.Position)+OriginL;
            if (WorldL.X < -Scale || WorldL.X > Scale) continue;
            bool bMatch=false;
            for (const auto& RV : R.Vertices) if (WorldL==FVector(RV.Position)+OriginR) { bMatch=true; break; }
            TestTrue(TEXT("every overlapping asymmetric vertex matches"),bMatch); ++Shared;
        }
        TestTrue(TEXT("overlapping vertices examined"),Shared>0);
    }
    // Exercise both ends of the conservative world-coordinate envelope.
    for (int32 LeftKey : {-32766,32765}) for (float Scale : {1.f,75.3f,100.f})
    {
        const FIntVector LeftCoord(LeftKey,0,0), RightCoord(LeftKey+1,0,0);
        FVoxelLandChunkData Left=Plane(LeftCoord,2,1), Right=Plane(RightCoord,2,1);
        for (int32 Z=-1; Z<=33; ++Z) for (int32 Y=-1; Y<=33; ++Y) for (int32 X=-1; X<=33; ++X)
        {
            const int32 I=(X+1)+35*((Y+1)+35*(Z+1));
            Left.Density[I]=int16(FMath::Clamp((2*Z-1)*341+(X-32)*3,-32768,32767));
            Right.Density[I]=int16(FMath::Clamp((2*Z-1)*341+X*3,-32768,32767));
        }
        FVoxelLandMeshData L,R;
        VoxelLandMeshing::BuildSurfaceNets(Left,Scale,S1,L,NeverCancel);
        VoxelLandMeshing::BuildSurfaceNets(Right,Scale,S2,R,NeverCancel);
        const FVector OL=VoxelLandMeshCoordinates::RenderOrigin(LeftCoord,Scale), OR=VoxelLandMeshCoordinates::RenderOrigin(RightCoord,Scale);
        const double Boundary=double(LeftKey+1)*32.0*Scale;
        int32 Checked=0;
        for (const auto& V : L.Vertices)
        {
            const FVector W=FVector(V.Position)+OL;
            if (FMath::Abs(W.X-Boundary)>Scale) continue;
            bool bFound=false;
            for (const auto& Other : R.Vertices) if (W==FVector(Other.Position)+OR) { bFound=true; break; }
            TestTrue(TEXT("far seam shared vertex"),bFound); ++Checked;
        }
        TestTrue(TEXT("far seam tested"),Checked>0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM2CornerOwnershipTest, "VoxelLand.M2.CornerOwnership", Flags)
bool FVoxelLandM2CornerOwnershipTest::RunTest(const FString& Parameters)
{
    for (int32 NormalAxis=0; NormalAxis<3; ++NormalAxis)
    {
    FVoxelLandMeshData Meshes[2][2][2];
    for (int32 Z=0; Z<2; ++Z) for (int32 Y=0; Y<2; ++Y) for (int32 X=0; X<2; ++X)
    {
        const FIntVector Key(X-1,Y-1,Z-1); FVoxelLandMeshingScratch Scratch;
        TestEqual(TEXT("corner mesh built"),int32(VoxelLandMeshing::BuildSurfaceNets(Plane(Key,NormalAxis,-1),100.f,Scratch,Meshes[X][Y][Z],[] {return false;})),int32(EVoxelLandMeshResult::Success));
        const int32 Coords[3]={X,Y,Z};
        TestEqual(TEXT("only owning chunks emit plane faces"),Meshes[X][Y][Z].Indices.Num(),Coords[NormalAxis]==0 ? 32*32*6 : 0);
    }
    for (int32 Axis=0; Axis<3; ++Axis) for (int32 Z=0; Z<2; ++Z) for (int32 Y=0; Y<2; ++Y) for (int32 X=0; X<2; ++X)
    {
        int32 A[3]={X,Y,Z}; if (A[Axis]!=0) continue;
        int32 B[3]={X,Y,Z}; B[Axis]=1;
        const FIntVector KA(A[0]-1,A[1]-1,A[2]-1), KB(B[0]-1,B[1]-1,B[2]-1);
        const FVector OA=VoxelLandMeshCoordinates::RenderOrigin(KA,100.f), OB=VoxelLandMeshCoordinates::RenderOrigin(KB,100.f);
        int32 Count=0;
        for (const auto& VA : Meshes[A[0]][A[1]][A[2]].Vertices)
        {
            const FVector WA=FVector(VA.Position)+OA;
            if (FMath::Abs(WA[Axis])>100.0) continue;
            bool bFound=false;
            for (const auto& VB : Meshes[B[0]][B[1]][B[2]].Vertices)
                if (WA==FVector(VB.Position)+OB) { bFound=true; TestTrue(TEXT("shared normal"),VA.Normal.Equals(VB.Normal,1.e-6f)); break; }
            TestTrue(TEXT("shared face/edge/corner vertex"),bFound); ++Count;
        }
        TestTrue(TEXT("shared border checked"),Count>0);
    }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM2SphereTest, "VoxelLand.M2.SphereAndWinding", Flags)
bool FVoxelLandM2SphereTest::RunTest(const FString& Parameters)
{
    FVoxelLandChunkData Chunk; Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
    for (int32 Z=-1; Z<=33; ++Z) for (int32 Y=-1; Y<=33; ++Y) for (int32 X=-1; X<=33; ++X)
    {
        const FVector3f D(float(X-16),float(Y-16),float(Z-16));
        Chunk.Density[(X+1)+35*((Y+1)+35*(Z+1))]=int16(FMath::Clamp(FMath::RoundToInt((D.Size()-8.f)*1024.f),-32768,32767));
    }
    FVoxelLandMeshingScratch Scratch; FVoxelLandMeshData Mesh;
    TestEqual(TEXT("sphere builds"),int32(VoxelLandMeshing::BuildSurfaceNets(Chunk,100.f,Scratch,Mesh,[] {return false;})),int32(EVoxelLandMeshResult::Success));
    TestTrue(TEXT("sphere emits faces"),Mesh.Indices.Num()>0);
    for (const auto& V : Mesh.Vertices)
    {
        const FVector3f Out=(V.Position-FVector3f(1600,1600,1600)).GetSafeNormal();
        TestTrue(TEXT("sphere normal points outward"),FVector3f::DotProduct(Out,V.Normal)>0.85f);
    }
    for (int32 I=0; I+2<Mesh.Indices.Num(); I+=3)
    {
        const auto& A=Mesh.Vertices[Mesh.Indices[I]]; const auto& B=Mesh.Vertices[Mesh.Indices[I+1]]; const auto& C=Mesh.Vertices[Mesh.Indices[I+2]];
        const FVector3f Cross=FVector3f::CrossProduct(B.Position-A.Position,C.Position-A.Position);
        TestTrue(TEXT("face orientation follows gradient"),FVector3f::DotProduct(Cross,A.Normal+B.Normal+C.Normal)>0.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM2CsgTest, "VoxelLand.M2.SphereWithTunnel", Flags)
bool FVoxelLandM2CsgTest::RunTest(const FString& Parameters)
{
    FVoxelLandChunkData Chunk; Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
    for (int32 Z=-1; Z<=33; ++Z) for (int32 Y=-1; Y<=33; ++Y) for (int32 X=-1; X<=33; ++X)
    {
        const FVector3f P(float(X-16),float(Y-16),float(Z-16));
        const float Sphere=P.Size()-10.f;
        const float Box=FMath::Max3(FMath::Abs(P.X)-12.f,FMath::Abs(P.Y)-2.f,FMath::Abs(P.Z)-2.f);
        const int32 Q=FMath::RoundToInt(FMath::Max(Sphere,-Box)*1024.f);
        Chunk.Density[(X+1)+35*((Y+1)+35*(Z+1))]=int16(FMath::Clamp(Q,-32768,32767));
    }
    FVoxelLandMeshingScratch Scratch; FVoxelLandMeshData Mesh;
    TestEqual(TEXT("CSG mesh builds"),int32(VoxelLandMeshing::BuildSurfaceNets(Chunk,100.f,Scratch,Mesh,[] {return false;})),int32(EVoxelLandMeshResult::Success));
    TestTrue(TEXT("CSG emits triangles"),Mesh.Indices.Num()>0);
    int32 TunnelWallVertices=0;
    for (const auto& V : Mesh.Vertices)
    {
        if (FMath::Abs(V.Position.X-1600.f)<400.f && FMath::Abs(V.Position.Y-1800.f)<1.f && FMath::Abs(V.Position.Z-1600.f)<100.f)
        {
            TestTrue(TEXT("tunnel wall normal points into air"),V.Normal.Y < -0.85f);
            ++TunnelWallVertices;
        }
    }
    TestTrue(TEXT("tunnel wall sampled"),TunnelWallVertices>0);
    for (uint32 Index : Mesh.Indices) TestTrue(TEXT("CSG index valid"),Index<uint32(Mesh.Vertices.Num()));
    return true;
}
#endif
