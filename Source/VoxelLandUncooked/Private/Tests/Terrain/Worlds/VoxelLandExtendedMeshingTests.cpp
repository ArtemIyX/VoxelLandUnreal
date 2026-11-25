#if WITH_DEV_AUTOMATION_TESTS
#include "Generation/Meshing/VoxelLandMeshing.h"
#include "Misc/AutomationTest.h"

namespace
{
FVoxelLandChunkData M4FarMeshChunk(FIntVector Coordinate, int32 Axis, bool bLeft)
{
	FVoxelLandChunkData Chunk;
	Chunk.Key.Coordinate = Coordinate;
	Chunk.Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
	for (int32 Z = -1; Z <= 33; ++Z)
	{
		for (int32 Y = -1; Y <= 33; ++Y)
		{
			for (int32 X = -1; X <= 33; ++X)
			{
				const int32 Normal = Axis == 0 ? X : Y;
				Chunk.Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] =
					int16((2 * Z - 1) * 341 + (Normal - (bLeft ? 32 : 0)) * 3);
			}
		}
	}
	return Chunk;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4ExtendedMeshingTest, "VoxelLand.M4.Unlimited.ExtendedMeshing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4ExtendedMeshingTest::RunTest(const FString& Parameters)
{
	constexpr int32 Limit = (1 << 22) - 2;
	TestFalse(TEXT("legacy bound unchanged"),
		VoxelLandMeshCoordinates::IsKeyInPrecisionRange(FIntVector(32767, 0, 0), 1.f, false));
	TestTrue(TEXT("opt-in extends exact integer voxel coordinates"),
		VoxelLandMeshCoordinates::IsKeyInPrecisionRange(FIntVector(Limit, 0, 0), 1.f, true));
	TestFalse(TEXT("larger envelope rejected"),
		VoxelLandMeshCoordinates::IsKeyInPrecisionRange(FIntVector(Limit + 1, 0, 0), 1.f, true));
	TestFalse(TEXT("fractional voxel size rejected"),
		VoxelLandMeshCoordinates::IsKeyInPrecisionRange(FIntVector::ZeroValue, 1.5f, true));
	TestFalse(TEXT("oversized voxel rejected"),
		VoxelLandMeshCoordinates::IsKeyInPrecisionRange(FIntVector::ZeroValue, 134218.f, true));
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		for (int32 First : {-Limit, Limit - 1})
		{
			for (float Size : {1.f, 100.f, 134217.f})
			{
				FIntVector Left = FIntVector::ZeroValue, Right = FIntVector::ZeroValue;
				Left[Axis] = First;
				Right[Axis] = First + 1;
				FVoxelLandChunkData A = M4FarMeshChunk(Left, Axis, true), B = M4FarMeshChunk(Right, Axis, false);
				FVoxelLandMeshingScratch SA, SB;
				FVoxelLandMeshData MA, MB;
				if (!TestEqual(TEXT("far left mesh succeeds"),
						int32(VoxelLandMeshing::BuildSurfaceNets(
							A, Size, SA, MA, [] { return false; }, true)),
						int32(EVoxelLandMeshResult::Success))
					|| !TestEqual(TEXT("far right mesh succeeds"),
						int32(VoxelLandMeshing::BuildSurfaceNets(
							B, Size, SB, MB, [] { return false; }, true)),
						int32(EVoxelLandMeshResult::Success)))
				{
					return false;
				}
				const FVector OriginA = VoxelLandMeshCoordinates::RenderOrigin(Left, Size);
				const FVector OriginB = VoxelLandMeshCoordinates::RenderOrigin(Right, Size);
				const double Boundary = double(First + 1) * 32.0 * Size;
				int32 Shared = 0;
				for (const FVoxelLandMeshVertex& VA : MA.Vertices)
				{
					const FVector WorldA = OriginA + FVector(VA.Position);
					if (FMath::Abs(WorldA[Axis] - Boundary) > Size)
					{
						continue;
					}
					bool bFound = false;
					for (const FVoxelLandMeshVertex& VB : MB.Vertices)
					{
						if (WorldA == OriginB + FVector(VB.Position))
						{
							bFound = true;
							break;
						}
					}
					TestTrue(TEXT("neighbor meshes share identical world position"), bFound);
					++Shared;
				}
				TestTrue(TEXT("nonempty shared border tested"), Shared > 0);
			}
		}
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVoxelLandM4ExtendedMeshingZTest, "VoxelLand.M4.Unlimited.ExtendedMeshingZSeam",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVoxelLandM4ExtendedMeshingZTest::RunTest(const FString& Parameters)
{
	constexpr int32 Limit = (1 << 22) - 2;
	for (int32 FarX : {-Limit, Limit})
	{
		for (int32 FarY : {-Limit, Limit})
		{
			for (int32 LowerZ : {-1, 0})
			{
				for (float Size : {1.f, 100.f})
				{
					const FIntVector Lower(FarX, FarY, LowerZ), Upper(FarX, FarY, LowerZ + 1);
					FVoxelLandChunkData Chunks[2];
					for (int32 Side = 0; Side < 2; ++Side)
					{
						Chunks[Side].Key.Coordinate = Side == 0 ? Lower : Upper;
						Chunks[Side].Density.SetNumUninitialized(FVoxelLandChunkData::SampleCount);
						for (int32 Z = -1; Z <= 33; ++Z)
						{
							for (int32 Y = -1; Y <= 33; ++Y)
							{
								for (int32 X = -1; X <= 33; ++X)
								{
									Chunks[Side].Density[(X + 1) + 35 * ((Y + 1) + 35 * (Z + 1))] =
										int16((2 * X - 1) * 341 + (Z - (Side == 0 ? 32 : 0)) * 3);
								}
							}
						}
					}
					FVoxelLandMeshData Meshes[2];
					FVoxelLandMeshingScratch Scratch[2];
					for (int32 Side = 0; Side < 2; ++Side)
					{
						if (!TestEqual(TEXT("Z neighbor mesh succeeds"),
								int32(VoxelLandMeshing::BuildSurfaceNets(
									Chunks[Side], Size, Scratch[Side], Meshes[Side], [] { return false; }, true)),
								int32(EVoxelLandMeshResult::Success)))
						{
							return false;
						}
					}
					const FVector Origins[] = {VoxelLandMeshCoordinates::RenderOrigin(Lower, Size),
						VoxelLandMeshCoordinates::RenderOrigin(Upper, Size)};
					const double Boundary = double(LowerZ + 1) * 32.0 * Size;
					int32 Shared = 0;
					for (const FVoxelLandMeshVertex& A : Meshes[0].Vertices)
					{
						const FVector Position = Origins[0] + FVector(A.Position);
						if (FMath::Abs(Position.Z - Boundary) > Size)
						{
							continue;
						}
						bool bFound = false;
						for (const FVoxelLandMeshVertex& B : Meshes[1].Vertices)
						{
							if (Position == Origins[1] + FVector(B.Position))
							{
								bFound = true;
								TestTrue(TEXT("shared Z border normals match"), A.Normal.Equals(B.Normal, 1.e-6f));
								break;
							}
						}
						TestTrue(TEXT("shared Z border positions match"), bFound);
						++Shared;
					}
					TestTrue(TEXT("nonempty Z border compared"), Shared > 0);
				}
			}
		}
	}
	return true;
}

#endif
