#include "VoxelLandMaterialPreviewRaster.h"

#include "Data/Meshing/VoxelLandMaterialAppearance.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "Graph/Compilation/VoxelLandMaterialGraphPoint.h"
#include "Graph/Compilation/VoxelLandMaterialGraphProgram.h"
#include "Graph/Compilation/VoxelLandMaskGraphProgram.h"

bool FVoxelLandMaterialPreviewRaster::Build(TConstArrayView<FPreviewMesh> Meshes, float VoxelSize,
	const FVoxelLandMaterialAppearance& Appearance, FGuid Node, TFunctionRef<bool()> IsCancelled, TArray<uint8>& Pixels,
	double& Min, double& Max)
{
	if (!Appearance.Program)
	{
		return false;
	}
	FVoxelLandMaterialGraphPoint Context;
	Context.Seed = Appearance.Seed;
	Context.Signals = Appearance.Signals.Get();
	return BuildRaster(
		Meshes, VoxelSize, Context,
		[&Appearance, Node](const FVoxelLandMaterialGraphPoint& Point, double& Out) {
			return Appearance.Program->EvaluatePreview(Node, Point, Out);
		},
		IsCancelled, false, Pixels, Min, Max);
}

bool FVoxelLandMaterialPreviewRaster::BuildMask(TConstArrayView<FPreviewMesh> Meshes, float VoxelSize,
	const FVoxelLandMaskGraphProgram& Program, const FVoxelLandMaterialGraphPoint& Context, FGuid Output,
	TFunctionRef<bool()> IsCancelled, TArray<uint8>& Pixels, double& Min, double& Max)
{
	return BuildRaster(
		Meshes, VoxelSize, Context,
		[&Program, Output](
			const FVoxelLandMaterialGraphPoint& Point, double& Out) { return Program.Evaluate(Output, Point, Out); },
		IsCancelled, true, Pixels, Min, Max);
}

bool FVoxelLandMaterialPreviewRaster::BuildRaster(TConstArrayView<FPreviewMesh> Meshes, float VoxelSize,
	const FVoxelLandMaterialGraphPoint& Context,
	TFunctionRef<bool(const FVoxelLandMaterialGraphPoint&, double&)> Evaluate, TFunctionRef<bool()> IsCancelled,
	bool bAbsoluteGray, TArray<uint8>& Pixels, double& Min, double& Max)
{
	if (Meshes.IsEmpty())
	{
		return false;
	}
	FBox Bounds(ForceInit);
	for (const FPreviewMesh& Entry : Meshes)
	{
		const FVector Offset = VoxelLandMeshCoordinates::RenderOrigin(Entry.Key, VoxelSize);
		Bounds += FVector(Entry.Value->Bounds.Min) + Offset;
		Bounds += FVector(Entry.Value->Bounds.Max) + Offset;
	}
	const FVector2d Corner(Bounds.Min.X, Bounds.Min.Y);
	const FVector2d Extent(Bounds.Max.X - Bounds.Min.X, Bounds.Max.Y - Bounds.Min.Y);
	if (Extent.X <= UE_SMALL_NUMBER || Extent.Y <= UE_SMALL_NUMBER)
	{
		return false;
	}
	const FVector2d ProjectScale(Resolution / Extent.X, Resolution / Extent.Y);
	const auto Project = [&Corner, &ProjectScale](const FVector& Position) {
		return (FVector2d(Position.X, Position.Y) - Corner) * ProjectScale;
	};
	constexpr int32 NumPixels = Resolution * Resolution;
	constexpr double EmptyHeight = -TNumericLimits<double>::Max();
	TArray<double> Height;
	Height.Init(EmptyHeight, NumPixels);
	TArray<FVector3f> Normal;
	Normal.SetNumZeroed(NumPixels);

	for (const FPreviewMesh& Entry : Meshes)
	{
		const FVoxelLandMeshData& Mesh = *Entry.Value;
		const FVector Origin = VoxelLandMeshCoordinates::RenderOrigin(Entry.Key, VoxelSize);
		for (int32 Base = 0; Base + 2 < Mesh.Indices.Num(); Base += 3)
		{
			if (IsCancelled())
			{
				return false;
			}
			const FVoxelLandMeshVertex& VA = Mesh.Vertices[Mesh.Indices[Base]];
			const FVoxelLandMeshVertex& VB = Mesh.Vertices[Mesh.Indices[Base + 1]];
			const FVoxelLandMeshVertex& VC = Mesh.Vertices[Mesh.Indices[Base + 2]];
			const FVector A = FVector(VA.Position) + Origin;
			const FVector B = FVector(VB.Position) + Origin;
			const FVector C = FVector(VC.Position) + Origin;
			const FVector2d PA = Project(A);
			const FVector2d PB = Project(B);
			const FVector2d PC = Project(C);
			const double Det = FVector2d::CrossProduct(PB - PA, PC - PA);
			if (FMath::Abs(Det) <= UE_SMALL_NUMBER)
			{
				continue;
			}
			const int32 MinX = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(PA.X, PB.X, PC.X)), 0, Resolution - 1);
			const int32 MaxX = FMath::Clamp(FMath::FloorToInt32(FMath::Max3(PA.X, PB.X, PC.X)), 0, Resolution - 1);
			const int32 MinY = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(PA.Y, PB.Y, PC.Y)), 0, Resolution - 1);
			const int32 MaxY = FMath::Clamp(FMath::FloorToInt32(FMath::Max3(PA.Y, PB.Y, PC.Y)), 0, Resolution - 1);
			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					const FVector2d AP(X + 0.5 - PA.X, Y + 0.5 - PA.Y);
					const double WB = FVector2d::CrossProduct(AP, PC - PA) / Det;
					const double WC = FVector2d::CrossProduct(PB - PA, AP) / Det;
					const double WA = 1.0 - WB - WC;
					const double Z = A.Z * WA + B.Z * WB + C.Z * WC;
					const int32 Index = Y * Resolution + X;
					if (WA >= -UE_SMALL_NUMBER && WB >= -UE_SMALL_NUMBER && WC >= -UE_SMALL_NUMBER && Z > Height[Index])
					{
						Height[Index] = Z;
						Normal[Index] = VA.Normal * float(WA) + VB.Normal * float(WB) + VC.Normal * float(WC);
					}
				}
			}
		}
	}

	TArray<double> Values;
	Values.SetNumZeroed(NumPixels);
	Min = TNumericLimits<double>::Max();
	Max = -Min;
	for (int32 Index = 0; Index < NumPixels; ++Index)
	{
		if (IsCancelled())
		{
			return false;
		}
		if (Height[Index] == EmptyHeight)
		{
			continue;
		}
		const FVector2d XY = Corner + FVector2d(Index % Resolution + 0.5, Index / Resolution + 0.5) / ProjectScale;
		FVoxelLandMaterialGraphPoint Point = Context;
		Point.Position = FVector3d(XY.X, XY.Y, Height[Index]);
		Point.SlopeDegrees =
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(double(Normal[Index].GetSafeNormal().Z), -1.0, 1.0)));
		if (!Evaluate(Point, Values[Index]))
		{
			return false;
		}
		Min = FMath::Min(Min, Values[Index]);
		Max = FMath::Max(Max, Values[Index]);
	}
	if (Min > Max)
	{
		return false;
	}

	Pixels.SetNumZeroed(NumPixels * sizeof(FColor));
	const double Range = Max - Min;
	for (int32 Index = 0; Index < NumPixels; ++Index)
	{
		if (Height[Index] != EmptyHeight)
		{
			const double Alpha =
				bAbsoluteGray ? Values[Index]
							  : (Range > UE_SMALL_NUMBER ? (Values[Index] - Min) / Range : FMath::Clamp(Min, 0.0, 1.0));
			const uint8 Shade = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Alpha * 255.0), 0, 255));
			const FColor Color(Shade, Shade, Shade, 255);
			FMemory::Memcpy(Pixels.GetData() + Index * sizeof(FColor), &Color, sizeof(FColor));
		}
	}
	return true;
}
