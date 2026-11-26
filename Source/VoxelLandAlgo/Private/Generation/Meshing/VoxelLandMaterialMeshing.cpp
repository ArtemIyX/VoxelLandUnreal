#include "Generation/Meshing/VoxelLandMaterialMeshing.h"
#include "Data/Meshing/VoxelLandMaterialAppearance.h"
#include "Data/Meshing/VoxelLandMeshData.h"
#include "VoxelLandMaterialClassifier.h"

class FVoxelLandMaterialMeshingWork final
{
public:
	FVoxelLandMaterialMeshingWork(
		const FVoxelLandMeshData& InSource, const FVoxelLandMaterialAppearance& InAppearance, const FVector3d& InOrigin)
		: Source(InSource)
		, Appearance(InAppearance)
		, Origin(InOrigin)
	{
	}

	[[nodiscard]] bool Build(TFunctionRef<bool()> IsCancelled)
	{
		const bool bHasSlopeRules = FVoxelLandMaterialClassifier::HasSlopeRules(Appearance);
		Samples.Reserve(Source.Vertices.Num());
		for (int32 Index = 0; Index < Source.Vertices.Num(); ++Index)
		{
			if ((Appearance.Program || Index % CancellationStride == 0) && IsCancelled())
			{
				return false;
			}
			const FVoxelLandMaterialCandidates Sample =
				FVoxelLandMaterialClassifier::Sample(Source.Vertices[Index], Appearance, Origin, bHasSlopeRules);
			if (Sample.Count == 0)
			{
				return false;
			}
			Samples.Add(Sample);
		}

		Indices.Reserve(Source.Indices.Num());
		Blends.SetNum(Source.Vertices.Num());
		for (int32 Start = 0; Start < Source.Indices.Num(); Start += TriangleCorners)
		{
			if (Start % CancellationStride == 0 && IsCancelled())
			{
				return false;
			}
			if (!AddSourceTriangle(Source.Indices.GetData() + Start))
			{
				return false;
			}
		}

		for (FVoxelLandMaterialBlend& Blend : Blends)
		{
			if (IsUnassigned(Blend))
			{
				Blend.Weights[0] = FVoxelLandMaterialBlend::WeightSum;
			}
		}
		return !IsCancelled();
	}

	void Publish(FVoxelLandMeshData& Mesh)
	{
		Mesh.Vertices.Append(MoveTemp(AddedVertices));
		Mesh.Indices = MoveTemp(Indices);
		Mesh.MaterialBlend = MoveTemp(Blends);
	}

	static bool CanPack(
		const FVoxelLandMeshData& Mesh, const FVoxelLandMaterialAppearance& Appearance, const FVector3d& LogicalOrigin)
	{
		return !LogicalOrigin.ContainsNaN() && Mesh.Indices.Num() % TriangleCorners == 0 && Mesh.MaterialBlend.IsEmpty()
			&& FVoxelLandMaterialClassifier::IsValid(Appearance);
	}

	static bool NeverCancelled() { return false; }

private:
	static constexpr int32 TriangleCorners = 3;
	static constexpr int32 CancellationStride = 128;
	static constexpr float InnerA = 0.45f;
	static constexpr float InnerC = 0.1f;

	using FSlotSet = TArray<uint8, TInlineAllocator<TriangleCorners * FVoxelLandMaterialCandidates::MaxCount>>;

	[[nodiscard]] bool AddSourceTriangle(const uint32* Triangle)
	{
		const uint32 A = Triangle[0];
		const uint32 B = Triangle[1];
		const uint32 C = Triangle[2];
		const uint32 VertexCount = Source.Vertices.Num();
		if (A >= VertexCount || B >= VertexCount || C >= VertexCount)
		{
			return false;
		}

		const FSlotSet Slots = GetTriangleSlots(A, B, C);
		if (Slots.Num() <= FVoxelLandMaterialBlend::LayerCount)
		{
			Emit(A, B, C, Slots);
		}
		else
		{
			SplitInterior(A, B, C);
		}
		return true;
	}

	void SplitInterior(uint32 A, uint32 B, uint32 C)
	{
		// Each outer edge keeps its original two endpoint samples. The inset vertices
		// use one material from their edge so every new triangle fits in four slots.
		const uint32 AB = AddInteriorVertex(A, B, C);
		const uint32 BC = AddInteriorVertex(B, C, A);
		const uint32 CA = AddInteriorVertex(C, A, B);

		Emit(A, B, AB, GetTriangleSlots(A, B, AB));
		Emit(B, C, BC, GetTriangleSlots(B, C, BC));
		Emit(C, A, CA, GetTriangleSlots(C, A, CA));
		Emit(A, AB, CA, GetTriangleSlots(A, AB, CA));
		Emit(B, BC, AB, GetTriangleSlots(B, BC, AB));
		Emit(C, CA, BC, GetTriangleSlots(C, CA, BC));
		Emit(AB, BC, CA, GetTriangleSlots(AB, BC, CA));
	}

	[[nodiscard]] uint32 AddInteriorVertex(uint32 A, uint32 B, uint32 C)
	{
		const FVoxelLandMeshVertex& VA = VertexAt(A);
		const FVoxelLandMeshVertex& VB = VertexAt(B);
		const FVoxelLandMeshVertex& VC = VertexAt(C);

		FVoxelLandMeshVertex Vertex;
		Vertex.Position = Inset(VA.Position, VB.Position, VC.Position);
		Vertex.Normal = Inset(VA.Normal, VB.Normal, VC.Normal);
		Vertex.Tangent = Inset(VA.Tangent, VB.Tangent, VC.Tangent);
		Vertex.UV = Inset(VA.UV, VB.UV, VC.UV);
		Vertex.Color =
			FColor(InsetByte(VA.Color.R, VB.Color.R, VC.Color.R), InsetByte(VA.Color.G, VB.Color.G, VC.Color.G),
				InsetByte(VA.Color.B, VB.Color.B, VC.Color.B), InsetByte(VA.Color.A, VB.Color.A, VC.Color.A));

		FVoxelLandMaterialCandidates Sample;
		Sample.Slots[0] = Samples[A].Slots[0];
		Sample.Weights[0] = FVoxelLandMaterialBlend::WeightSum;
		Sample.Count = 1;
		return AddVertex(Vertex, Sample, FVoxelLandMaterialBlend{});
	}

	template <typename VertexAttribute>
	static VertexAttribute Inset(const VertexAttribute& A, const VertexAttribute& B, const VertexAttribute& C)
	{
		return A + (B - A) * InnerA + (C - A) * InnerC;
	}

	static uint8 InsetByte(uint8 A, uint8 B, uint8 C)
	{
		return static_cast<uint8>(
			FMath::RoundToInt(Inset(static_cast<float>(A), static_cast<float>(B), static_cast<float>(C))));
	}

	const FVoxelLandMeshVertex& VertexAt(uint32 Index) const
	{
		const uint32 SourceCount = Source.Vertices.Num();
		return Index < SourceCount ? Source.Vertices[Index] : AddedVertices[Index - SourceCount];
	}

	uint32 AddVertex(const FVoxelLandMeshVertex& Vertex, const FVoxelLandMaterialCandidates& Sample,
		const FVoxelLandMaterialBlend& Blend)
	{
		const uint32 Index = Samples.Num();
		AddedVertices.Add(Vertex);
		Samples.Add(Sample);
		Blends.Add(Blend);
		return Index;
	}

	FSlotSet GetTriangleSlots(uint32 A, uint32 B, uint32 C) const
	{
		FSlotSet Slots;
		const uint32 Corners[] = {A, B, C};
		for (const uint32 Corner : Corners)
		{
			const FVoxelLandMaterialCandidates& Sample = Samples[Corner];
			for (int32 Index = 0; Index < Sample.Count; ++Index)
			{
				Slots.AddUnique(Sample.Slots[Index]);
			}
		}
		Slots.Sort();
		return Slots;
	}

	void Emit(uint32 A, uint32 B, uint32 C, const FSlotSet& Slots)
	{
		check(Slots.Num() > 0 && Slots.Num() <= FVoxelLandMaterialBlend::LayerCount);
		const uint32 Corners[] = {A, B, C};
		for (const uint32 Corner : Corners)
		{
			Indices.Add(EncodeVertex(Corner, Slots));
		}
	}

	uint32 EncodeVertex(uint32 SourceIndex, const FSlotSet& Slots)
	{
		const FVoxelLandMaterialCandidates& Sample = Samples[SourceIndex];
		FVoxelLandMaterialBlend Blend;
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			Blend.Slots[Index] = Slots[Index];
			for (int32 Entry = 0; Entry < Sample.Count; ++Entry)
			{
				if (Slots[Index] == Sample.Slots[Entry])
				{
					Blend.Weights[Index] = Sample.Weights[Entry];
				}
			}
		}
		return FindOrAddEncodedVertex(SourceIndex, Blend);
	}

	uint32 FindOrAddEncodedVertex(uint32 SourceIndex, const FVoxelLandMaterialBlend& Blend)
	{
		FVoxelLandMaterialBlend& OriginalBlend = Blends[SourceIndex];
		if (IsUnassigned(OriginalBlend))
		{
			OriginalBlend = Blend;
			return SourceIndex;
		}
		if (FMemory::Memcmp(OriginalBlend.Slots, Blend.Slots, sizeof(Blend.Slots)) == 0)
		{
			return SourceIndex;
		}

		uint64 Key = SourceIndex;
		for (const uint8 Slot : Blend.Slots)
		{
			Key = (Key << 8) | Slot;
		}
		if (const uint32* Existing = Duplicates.Find(Key))
		{
			return *Existing;
		}

		const FVoxelLandMeshVertex Vertex = VertexAt(SourceIndex);
		const FVoxelLandMaterialCandidates Sample = Samples[SourceIndex];
		const uint32 Index = AddVertex(Vertex, Sample, Blend);
		Duplicates.Add(Key, Index);
		return Index;
	}

	static bool IsUnassigned(const FVoxelLandMaterialBlend& Blend)
	{
		constexpr FVoxelLandMaterialBlend Empty;
		return FMemory::Memcmp(Blend.Weights, Empty.Weights, sizeof(Empty.Weights)) == 0;
	}

	const FVoxelLandMeshData& Source;
	const FVoxelLandMaterialAppearance& Appearance;
	const FVector3d Origin;
	TArray<FVoxelLandMaterialCandidates> Samples;
	TArray<FVoxelLandMeshVertex> AddedVertices;
	TArray<FVoxelLandMaterialBlend> Blends;
	TArray<uint32> Indices;
	TMap<uint64, uint32> Duplicates;
};

bool FVoxelLandMaterialMeshing::Pack(
	FVoxelLandMeshData& Mesh, const FVoxelLandMaterialAppearance& Appearance, const FVector3d& LogicalOrigin)
{
	return Pack(Mesh, Appearance, LogicalOrigin, FVoxelLandMaterialMeshingWork::NeverCancelled);
}

bool FVoxelLandMaterialMeshing::Pack(FVoxelLandMeshData& Mesh, const FVoxelLandMaterialAppearance& Appearance,
	const FVector3d& LogicalOrigin, TFunctionRef<bool()> IsCancelled)
{
	if (IsCancelled() || !FVoxelLandMaterialMeshingWork::CanPack(Mesh, Appearance, LogicalOrigin))
	{
		return false;
	}
	if (Mesh.Indices.IsEmpty())
	{
		return true;
	}

	FVoxelLandMaterialMeshingWork Work(Mesh, Appearance, LogicalOrigin);
	if (!Work.Build(IsCancelled))
	{
		return false;
	}
	Work.Publish(Mesh);
	return true;
}
