#include "World/VoxelLandM7RenderWork.h"

#include "Generation/VoxelLandGenerator.h"
#include "Generation/VoxelLandM7Clipmap.h"
#include "Generation/VoxelLandM7NormalFrames.h"
#include "Generation/VoxelLandM7Seams.h"
#include "Generation/VoxelLandMeshing.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace VoxelLandWorldPrivate
{
namespace
{
constexpr int32 MaxRefinementPasses = 12;
// Combined retained triangle buffers for one revision, including ambient
// source candidates, clipped terrain and curtains. Publishing can coexist
// with one replacement (256 MiB total), plus one bounded leaf/patch and
// scratch being evaluated. Engine-side vertex/RHI copies are separate.
constexpr int64 MaxRevisionMeshBytes = 128ll * 1024 * 1024;

struct FOverride
{
	FVoxelLandMeshData Terrain;
	FVoxelLandMeshData Curtain;
};

struct FEnd
{
	FVoxelLandChunkKey Coarse;
	double MinZ = 0.0;
	double MaxZ = 0.0;
	FVoxelLandM7CurtainEnd Curtain;
};

FVoxelLandError Failed(const TCHAR* Message)
{
	return {EVoxelLandErrorCode::InvalidParameter, Message};
}

FVoxelLandError Cancelled()
{
	return {EVoxelLandErrorCode::Cancelled, TEXT("M7 clipmap revision cancelled")};
}

int64 MeshBytes(const FVoxelLandMeshData& Mesh)
{
	return int64(Mesh.Vertices.GetAllocatedSize()) + int64(Mesh.Indices.GetAllocatedSize());
}

int64 SourceBytes(const FVoxelLandM7SeamMesh& Mesh)
{
	return MeshBytes(Mesh.Terrain) + int64(Mesh.PositiveOwnerIndices.GetAllocatedSize())
		+ int64(Mesh.PositiveOwnerEdges.GetAllocatedSize());
}

FVoxelLandError PrepareLeaf(const FVoxelLandChunkKey& Key, const FVoxelLandGenerator& Generator,
	FVoxelLandWorkerContext& Worker, FVoxelLandMeshingScratch& Scratch, TFunctionRef<bool()> IsCancelled,
	FM7SourcePtr& OutMesh)
{
	const float BaseVoxel = Generator.GetProfile().VoxelSizeCm;
	if (!VoxelLandMeshCoordinates::IsMeshKeyInPrecisionRange(Key, BaseVoxel, Generator.IsM4Unlimited()))
	{
		return Failed(TEXT("M7 leaf exceeds the mesh precision envelope"));
	}
	if (const FVoxelLandError Error = Generator.ValidateM4ChunkDemand(Key))
	{
		return Error;
	}
	TSharedPtr<const FVoxelLandChunkData, ESPMode::ThreadSafe> Chunk;
	if (const FVoxelLandError Error = Generator.GenerateChunkCancellable(Key, Worker, Chunk, IsCancelled))
	{
		return Error;
	}
	if (!Chunk)
	{
		return Failed(TEXT("M7 leaf source returned no field"));
	}
	auto Mesh = MakeShared<FVoxelLandM7SeamMesh, ESPMode::ThreadSafe>();
	switch (VoxelLandMeshing::BuildM7SeamMesh(*Chunk, int64(BaseVoxel), Scratch, *Mesh, IsCancelled, Generator.IsM4Unlimited()))
	{
	case EVoxelLandMeshResult::Cancelled:
		return Cancelled();
	case EVoxelLandMeshResult::InvalidInput:
		return Failed(TEXT("M7 leaf could not build admitted Surface Nets topology"));
	case EVoxelLandMeshResult::Success:
		OutMesh = Mesh;
		return {};
	}
	return Failed(TEXT("Unexpected M7 mesh result"));
}

bool FaceHasUniformProof(const FVoxelLandM7ClipmapFace& Face, const FVoxelLandM7SeamMesh& Coarse,
	TConstArrayView<const FVoxelLandM7SeamMesh*> Fine)
{
	const int32 CoarseSide = Face.Axis * 2 + int32(Face.Sign > 0);
	const int32 FineSide = Face.Axis * 2 + int32(Face.Sign < 0);
	const EVoxelLandChunkClassification Sign = Coarse.FaceSigns[CoarseSide];
	if (Sign != EVoxelLandChunkClassification::Solid && Sign != EVoxelLandChunkClassification::Empty)
	{
		return false;
	}
	for (const FVoxelLandM7SeamMesh* Child : Fine)
	{
		if (Child->FaceSigns[FineSide] != Sign)
		{
			return false;
		}
	}
	return true;
}

FVoxelLandError EvaluateFrames(const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker,
	TFunctionRef<bool()> IsCancelled, TConstArrayView<FVoxelLandM7NormalAnchor> Anchors, FVoxelLandM7NormalFrameBatch& Out)
{
	// At high LOD every face knot can occupy a distinct base cell. Break the
	// ordered anchor list into bounded worker batches without changing samples.
	constexpr int32 BatchSize = FVoxelLandM7NormalFrames::MaxStencilCellsPerBatch;
	FVoxelLandM7NormalFrameBatch Frames;
	Frames.Frames.Reserve(Anchors.Num());
	for (int32 Start = 0; Start < Anchors.Num(); Start += BatchSize)
	{
		FVoxelLandM7NormalFrameBatch Batch;
		const TConstArrayView<FVoxelLandM7NormalAnchor> Slice = Anchors.Slice(Start, FMath::Min(BatchSize, Anchors.Num() - Start));
		if (const FVoxelLandError Error = Generator.GenerateM7NormalFrames(Slice, Worker, Batch, IsCancelled))
		{
			return Error;
		}
		Frames.Frames.Append(Batch.Frames);
		Frames.FallbackCount += Batch.FallbackCount;
	}
	Out = MoveTemp(Frames);
	return {};
}

void RecordCurtainEnds(const FVoxelLandM7ClipmapFace& Face, float BaseVoxel, const FVoxelLandM7SeamPatch& Patch,
	TMap<FInt64Vector, TArray<FEnd>>& Ends, TSet<FVoxelLandChunkKey>& Refine)
{
	const int64 Width = int64(BaseVoxel) * 32 * (int64(1) << Face.Coarse.LOD);
	const FInt64Vector Min = FInt64Vector(Face.Coarse.Coordinate) * Width;
	FInt64Vector Point(Min.X, Min.Y, 0);
	if (Face.Sign > 0)
	{
		Point[Face.Axis] += Width;
	}
	const int32 Across = 1 - Face.Axis;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FEnd End{Face.Coarse, double(Min.Z), double(Min.Z + Width), Index == 0 ? Patch.Start : Patch.End};
		TArray<FEnd>& Incident = Ends.FindOrAdd(Point);
		for (const FEnd& Other : Incident)
		{
			if (End.MinZ > Other.MaxZ || Other.MinZ > End.MaxZ)
			{
				continue;
			}
			// Corner intervals are compared in global logical coordinates. If one
			// face has a curtain but the other has none, do not guess a corner fill.
			if (End.Curtain.bPresent != Other.Curtain.bPresent
				|| (End.Curtain.bPresent && (End.Curtain.Top != Other.Curtain.Top || End.Curtain.Bottom != Other.Curtain.Bottom)))
			{
				Refine.Add(End.Coarse);
				Refine.Add(Other.Coarse);
			}
		}
		Incident.Add(End);
		Point[Across] += Width;
	}
}

FVoxelLandError PrepareSeams(const FVoxelLandM7ClipmapLayout& Layout, const FM7Sources& Sources,
	const FVoxelLandGenerator& Generator, FVoxelLandWorkerContext& Worker, TFunctionRef<bool()> IsCancelled,
	int64 RevisionBytes, TMap<FVoxelLandChunkKey, FOverride>& Overrides, TSet<FVoxelLandChunkKey>& Refine)
{
	TMap<FVoxelLandChunkKey, FVoxelLandChunkKey> PatchOwner;
	TMap<FInt64Vector, TArray<FEnd>> Ends;
	for (const FVoxelLandM7ClipmapFace& Face : Layout.MixedFaces)
	{
		if (IsCancelled())
		{
			return Cancelled();
		}
		const FVoxelLandM7SeamMesh& Coarse = *Sources.FindChecked(Face.Coarse);
		const FVoxelLandM7SeamMesh* Fine[] = {Sources.FindChecked(Face.Fine[0]).Get(), Sources.FindChecked(Face.Fine[1]).Get(),
			Sources.FindChecked(Face.Fine[2]).Get(), Sources.FindChecked(Face.Fine[3]).Get()};
		if (FaceHasUniformProof(Face, Coarse, Fine))
		{
			continue;
		}
		if (Face.Axis == 2)
		{
			Refine.Add(Face.Coarse);
			continue;
		}
		const FVoxelLandChunkKey Participants[] = {Face.Coarse, Face.Fine[0], Face.Fine[1], Face.Fine[2], Face.Fine[3]};
		bool bConflict = false;
		for (const FVoxelLandChunkKey& Key : Participants)
		{
			if (const FVoxelLandChunkKey* Previous = PatchOwner.Find(Key))
			{
				bConflict = true;
				Refine.Add(*Previous);
			}
			else
			{
				PatchOwner.Add(Key, Face.Coarse);
			}
		}
		if (bConflict)
		{
			// A participant cannot be overwritten by two patches independently
			// clipped from its original geometry. Refine both incident groups.
			Refine.Add(Face.Coarse);
			continue;
		}
		const FVoxelLandM7VerticalFace Cut{Face.Axis, Face.Sign > 0};
		FVoxelLandM7SeamPatch Patch;
		const auto FrameSource = [&](TConstArrayView<FVoxelLandM7NormalAnchor> Anchors, FVoxelLandM7NormalFrameBatch& Result)
		{
			return EvaluateFrames(Generator, Worker, IsCancelled, Anchors, Result);
		};
		if (const FVoxelLandError Error = VoxelLandM7Seams::BuildVerticalFace(Coarse, Fine, Cut, FrameSource, IsCancelled, Patch))
		{
			if (Error.Code == EVoxelLandErrorCode::Cancelled || IsCancelled())
			{
				return Cancelled();
			}
			Refine.Add(Face.Coarse);
			continue;
		}
		RecordCurtainEnds(Face, Layout.Settings.BaseVoxelSizeCm, Patch, Ends, Refine);
		RevisionBytes += MeshBytes(Patch.CoarseTerrain) + MeshBytes(Patch.Curtain);
		for (const FVoxelLandMeshData& Mesh : Patch.FineTerrain)
		{
			RevisionBytes += MeshBytes(Mesh);
		}
		if (RevisionBytes > MaxRevisionMeshBytes)
		{
			return Failed(TEXT("M7 combined source, clipped terrain and curtain CPU mesh budget exceeded"));
		}
		Overrides.Add(Face.Coarse, FOverride{MoveTemp(Patch.CoarseTerrain), MoveTemp(Patch.Curtain)});
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Overrides.Add(Face.Fine[Index], FOverride{MoveTemp(Patch.FineTerrain[Index]), {}});
		}
	}
	return {};
}

bool MakeDraws(FM7RenderRevision& Revision, TMap<FVoxelLandChunkKey, FOverride>& Overrides, TFunctionRef<bool()> IsCancelled)
{
	for (const FVoxelLandChunkKey& Key : Revision.Layout.Leaves)
	{
		if (IsCancelled())
		{
			return false;
		}
		const FM7SourcePtr& Source = Revision.Sources.FindChecked(Key);
		FM7Draw Draw;
		Draw.Key = Key;
		float Spacing = 0.0f;
		// Each source key admitted exactly this spacing before meshing.
		verify(VoxelLandMeshCoordinates::TryGetLevelSpacing(Revision.Layout.Settings.BaseVoxelSizeCm, Key.LOD, Spacing));
		Draw.LogicalOrigin = VoxelLandMeshCoordinates::RenderOrigin(Key.Coordinate, Spacing);
		if (FOverride* Override = Overrides.Find(Key))
		{
			if (!Override->Terrain.Indices.IsEmpty())
			{
				Draw.Terrain = MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>(MoveTemp(Override->Terrain));
			}
			if (!Override->Curtain.Indices.IsEmpty())
			{
				Draw.Curtain = MakeShared<FVoxelLandMeshData, ESPMode::ThreadSafe>(MoveTemp(Override->Curtain));
			}
		}
		else if (!Source->Terrain.Indices.IsEmpty())
		{
			Draw.Terrain = FM7MeshPtr(Source, &Source->Terrain);
		}
		if (Draw.Terrain || Draw.Curtain)
		{
			Revision.Draws.Add(MoveTemp(Draw));
		}
	}
	return true;
}
}

FVoxelLandError BuildM7RenderRevision(const FM7RenderRequest& Request, const FVoxelLandGenerator& Generator,
	FVoxelLandWorkerContext& Worker, FVoxelLandMeshingScratch& Scratch, TFunctionRef<bool()> IsCancelled,
	TSharedPtr<const FM7RenderRevision, ESPMode::ThreadSafe>& OutRevision)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLand_BuildM7VisualRevision);
	auto Revision = MakeShared<FM7RenderRevision, ESPMode::ThreadSafe>();
	const FVoxelLandM7ClipmapLayout* Previous = Request.Previous ? &Request.Previous->Layout : nullptr;
	if (IsCancelled())
	{
		return Cancelled();
	}
	if (!Generator.SupportsM7CompletedField() || Request.Settings.BaseVoxelSizeCm != Generator.GetProfile().VoxelSizeCm
		|| !FVoxelLandM7Clipmap::Build(Request.Settings, Previous, Revision->Layout))
	{
		return Failed(TEXT("M7 clipmap demand exceeds the configured roots, precision or leaf budget"));
	}
	for (int32 Pass = 0; Pass < MaxRefinementPasses; ++Pass)
	{
		TSet<FVoxelLandChunkKey> Refine;
		TSet<FVoxelLandChunkKey> Wanted;
		Wanted.Reserve(Revision->Layout.Leaves.Num());
		for (const FVoxelLandChunkKey& Key : Revision->Layout.Leaves)
		{
			Wanted.Add(Key);
		}
		for (auto Cursor = Revision->Sources.CreateIterator(); Cursor; ++Cursor)
		{
			if (!Wanted.Contains(Cursor.Key()))
			{
				Cursor.RemoveCurrent();
			}
		}
		int64 BuildBytes = 0;
		for (const FVoxelLandChunkKey& Key : Revision->Layout.Leaves)
		{
			if (IsCancelled())
			{
				return Cancelled();
			}
			FM7SourcePtr Source = Revision->Sources.FindRef(Key);
			if (!Source && Request.Previous)
			{
				Source = Request.Previous->Sources.FindRef(Key);
			}
			if (!Source)
			{
				if (const FVoxelLandError Error = PrepareLeaf(Key, Generator, Worker, Scratch, IsCancelled, Source))
				{
					if (IsCancelled() || Error.Code == EVoxelLandErrorCode::Cancelled)
					{
						return Cancelled();
					}
					if (Key.LOD == 0)
					{
						return {Error.Code, FString::Printf(TEXT("M7 cannot admit required LOD0 coverage (%d,%d,%d): %s"),
							Key.Coordinate.X, Key.Coordinate.Y, Key.Coordinate.Z, *Error.Message)};
					}
					Refine.Add(Key);
					continue;
				}
			}
			BuildBytes += SourceBytes(*Source);
			if (BuildBytes > MaxRevisionMeshBytes)
			{
				return Failed(TEXT("M7 source CPU mesh budget exceeded"));
			}
			Revision->Sources.Add(Key, MoveTemp(Source));
		}

		TMap<FVoxelLandChunkKey, FOverride> Overrides;
		if (Refine.IsEmpty())
		{
			if (const FVoxelLandError Error = PrepareSeams(Revision->Layout, Revision->Sources, Generator,
				Worker, IsCancelled, BuildBytes, Overrides, Refine))
			{
				return Error;
			}
		}
		if (Refine.IsEmpty())
		{
			if (!MakeDraws(*Revision, Overrides, IsCancelled) || IsCancelled())
			{
				return Cancelled();
			}
			OutRevision = Revision;
			return {};
		}
		TArray<FVoxelLandChunkKey> Ordered;
		for (const FVoxelLandChunkKey& Key : Revision->Layout.Leaves)
		{
			if (Refine.Contains(Key))
			{
				Ordered.Add(Key);
			}
		}
		FVoxelLandM7ClipmapLayout Finer;
		if (!FVoxelLandM7Clipmap::Refine(Request.Settings, Revision->Layout, Ordered, Finer))
		{
			return Failed(TEXT("M7 source or seam coverage needs finer leaves beyond the balanced fallback budget"));
		}
		Revision->RefinedLeaves += Ordered.Num();
		Revision->Layout = MoveTemp(Finer);
	}
	return Failed(TEXT("M7 balanced fallback exhausted the admitted refinement passes"));
}
}
