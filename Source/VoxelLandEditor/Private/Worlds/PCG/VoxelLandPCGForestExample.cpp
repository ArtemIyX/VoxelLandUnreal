#include "VoxelLandPCGForestExample.h"

#include "Data/Assets/Graphs/VoxelLandTerrainGraph.h"
#include "Data/Graph/Schema/VoxelLandBuiltinCatalog.h"
#include "Data/Graph/Sources/VoxelLandGraphOutputs.h"
#include "Elements/PCGAttributeFilter.h"
#include "Engine/StaticMesh.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "VoxelLandPCGComponent.h"
#include "VoxelLandPCGGetVoxelTerrainSettings.h"
#include "VoxelLandPCGVoxelMaskSampleSettings.h"
#include "VoxelLandPCGVoxelSurfaceSamplerSettings.h"

class FVoxelLandForestGraphWiring final
{
public:
	static bool Connect(UPCGGraph& Graph, UPCGNode* From, FName Output, UPCGNode* To, FName Input)
	{
		const UPCGPin* SourcePin = From->GetOutputPin(Output);
		const UPCGPin* TargetPin = To->GetInputPin(Input);
		if (!SourcePin || !TargetPin)
		{
			UE_LOG(LogTemp, Error, TEXT("Forest graph pin is missing for %s.%s → %s.%s"), *From->GetName(),
				*Output.ToString(), *To->GetName(), *Input.ToString());
			return false;
		}
		Graph.AddEdge(From, Output, To, Input);
		for (const UPCGEdge* Edge : TargetPin->Edges)
		{
			if (Edge && Edge->GetOtherPin(TargetPin) == SourcePin)
			{
				return true;
			}
		}
		UE_LOG(LogTemp, Error, TEXT("Forest graph could not connect %s.%s → %s.%s"), *From->GetName(),
			*Output.ToString(), *To->GetName(), *Input.ToString());
		return false;
	}
};

class FVoxelLandForestMaskBuilder final
{
public:
	explicit FVoxelLandForestMaskBuilder(UVoxelLandTerrainGraph& InGraph)
		: Graph(InGraph)
	{
	}

	FGuid Build()
	{
		using ENode = EVoxelLandBuiltinNode;
		Graph.GraphRole = EVoxelLandGraphRole::Mask;
		const int32 Noise = Add(ENode::WorldNoise2D);
		constexpr float GroveSizeCm = 12000.0f;
		Graph.Nodes[Noise].Pins[2].FloatDefault = 1.0f / GroveSizeCm;
		const int32 Scaled = Add(ENode::Multiply);
		Graph.Nodes[Scaled].Pins[1].FloatDefault = 0.5f;
		Link(Noise, Scaled, 0);
		const int32 Bias = Add(ENode::Add);
		Graph.Nodes[Bias].Pins[1].FloatDefault = 0.5f;
		Link(Scaled, Bias, 0);

		const int32 Slope = Add(ENode::SurfaceSlope);
		const int32 Penalty = Add(ENode::Multiply);
		Graph.Nodes[Penalty].Pins[1].FloatDefault = 0.01f;
		Link(Slope, Penalty, 0);
		const int32 Score = Add(ENode::Subtract);
		Link(Bias, Score, 0);
		Link(Penalty, Score, 1);

		const int32 Output = Add(ENode::FloatOutput);
		FVoxelLandGraphOutputs::SetName(Graph.Nodes[Output], TEXT("Forest Density"));
		Link(Score, Output, 0);
		return Graph.Nodes[Output].Id;
	}

private:
	int32 Add(const EVoxelLandBuiltinNode Kind)
	{
		FVoxelLandBuiltinNodeSpec Spec;
		verify(FVoxelLandBuiltinCatalog::Find(Kind, Spec));
		const int32 Index = Graph.Nodes.Num();
		FVoxelLandGraphNodeRecord& Node = Graph.Nodes.AddDefaulted_GetRef();
		Node.Id = FGuid(0x564c5047, 1, 1, Index + 1);
		Node.OperatorId = Spec.OperatorId;
		Node.SeedSalt = static_cast<uint32>(Index + 1);
		for (const FVoxelLandBuiltinPinSpec& Input : Spec.Pins)
		{
			FVoxelLandGraphPinRecord& Pin = Node.Pins.AddDefaulted_GetRef();
			Pin.Id = FGuid(0x564c5047, 1, Index + 1, Node.Pins.Num() + 1);
			Pin.Name = Input.Name;
			Pin.Type = Input.Type;
			Pin.bOutput = Input.bOutput;
			Pin.FloatDefault = FCString::Atof(*Input.DefaultLiteral);
			Pin.IntDefault = FCString::Atoi(*Input.DefaultLiteral);
		}
		return Index;
	}

	void Link(const int32 Source, const int32 Target, const int32 Pin)
	{
		FVoxelLandGraphLinkRecord& Wire = Graph.Links.AddDefaulted_GetRef();
		Wire.SourceNode = Graph.Nodes[Source].Id;
		Wire.SourcePin = Graph.Nodes[Source].Pins.Last().Id;
		Wire.TargetNode = Graph.Nodes[Target].Id;
		Wire.TargetPin = Graph.Nodes[Target].Pins[Pin].Id;
	}

	UVoxelLandTerrainGraph& Graph;
};

FGuid FVoxelLandPCGForestExample::BuildMask(UVoxelLandTerrainGraph& Graph)
{
	return FVoxelLandForestMaskBuilder(Graph).Build();
}

bool FVoxelLandPCGForestExample::BuildGraph(UPCGGraph& Graph, const FGuid MaskId)
{
	UVoxelLandPCGGetVoxelTerrainSettings* GetSettings = nullptr;
	UVoxelLandPCGVoxelSurfaceSamplerSettings* SampleSettings = nullptr;
	UVoxelLandPCGVoxelMaskSampleSettings* MaskSettings = nullptr;
	UPCGAttributeFilteringSettings* FilterSettings = nullptr;
	UPCGNode* Get = Graph.AddNodeOfType(GetSettings);
	UPCGNode* Sample = Graph.AddNodeOfType(SampleSettings);
	UPCGNode* Mask = Graph.AddNodeOfType(MaskSettings);
	UPCGNode* Filter = Graph.AddNodeOfType(FilterSettings);
	UPCGNode* Output = Graph.GetOutputNode();
	if (!Get || !Sample || !Mask || !Filter || !Output)
	{
		return false;
	}
	Get->SetNodePosition(0, 0);
	Sample->SetNodePosition(300, 0);
	Mask->SetNodePosition(600, 0);
	Filter->SetNodePosition(900, 0);
	Output->SetNodePosition(1200, 0);
	SampleSettings->MinimumSpacingCm = 900.0;
	SampleSettings->MaximumSlopeDegrees = 35.0;
	const FName TerrainPin(TEXT("Terrain"));
	const FName DensityAttribute(TEXT("ForestDensity"));
	FVoxelLandPCGMaskSampleSettingsBinding& Binding = MaskSettings->Bindings.AddDefaulted_GetRef();
	Binding.OutputId = MaskId;
	Binding.Attribute = DensityAttribute;
	FilterSettings->TargetAttribute.SetAttributeName(DensityAttribute);
	FilterSettings->bUseConstantThreshold = true;
	FilterSettings->AttributeTypes.Type = EPCGMetadataTypes::Double;
	FilterSettings->AttributeTypes.DoubleValue = 0.32;
	using FWiring = FVoxelLandForestGraphWiring;
	return FWiring::Connect(Graph, Get, TerrainPin, Sample, TerrainPin)
		&& FWiring::Connect(Graph, Get, TerrainPin, Mask, TerrainPin)
		&& FWiring::Connect(
			Graph, Sample, PCGPinConstants::DefaultOutputLabel, Mask, PCGPinConstants::DefaultInputLabel)
		&& FWiring::Connect(
			Graph, Mask, PCGPinConstants::DefaultOutputLabel, Filter, PCGPinConstants::DefaultInputLabel)
		&& FWiring::Connect(
			Graph, Filter, PCGPinConstants::DefaultInFilterLabel, Output, PCGPinConstants::DefaultOutputLabel);
}

bool FVoxelLandPCGForestExample::ConfigureLayer(UVoxelLandPCGComponent& Placement, UPCGGraph& Graph)
{
	UStaticMesh* Marker = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	if (!Marker)
	{
		return false;
	}
	FVoxelLandPCGLayer& Layer = Placement.Layers.AddDefaulted_GetRef();
	Layer.LayerId = TEXT("Forest");
	Layer.Graph = &Graph;
	Layer.StaticMesh = Marker;
	Layer.GenerationRadiusCm = 20000.0;
	Layer.RetirementMarginCm = 6400.0;
	Layer.VisibilityDistanceCm = 32000.0;
	return true;
}
