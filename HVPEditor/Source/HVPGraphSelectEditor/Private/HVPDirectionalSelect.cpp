#include "HVPDirectionalSelect.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"

EHVPCardinal FHVPDirectionalSelect::CardinalForDelta(const FVector2D& Delta)
{
	if (FMath::Abs(Delta.X) > FMath::Abs(Delta.Y))
	{
		return Delta.X > 0.0 ? EHVPCardinal::Right : EHVPCardinal::Left;
	}
	return Delta.Y > 0.0 ? EHVPCardinal::Down : EHVPCardinal::Up;
}

FVector2D FHVPDirectionalSelect::AxisOf(EHVPCardinal Cardinal)
{
	switch (Cardinal)
	{
	case EHVPCardinal::Right: return FVector2D(1.0, 0.0);
	case EHVPCardinal::Left:  return FVector2D(-1.0, 0.0);
	case EHVPCardinal::Down:  return FVector2D(0.0, 1.0);
	case EHVPCardinal::Up:    return FVector2D(0.0, -1.0);
	}
	return FVector2D::ZeroVector;
}

TArray<UEdGraphNode*> FHVPDirectionalSelect::NodesBeyond(
	const SGraphPanel& Panel, const FVector2D& GraphLinePoint, EHVPCardinal Cardinal)
{
	TArray<UEdGraphNode*> Result;

	UEdGraph* Graph = Panel.GetGraphObj();
	if (!Graph)
	{
		return Result;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		// Size comes from the node's widget because UEdGraphNode only stores NodeWidth/NodeHeight
		// meaningfully for resizable nodes like comments. A widget that has not been laid out yet
		// reports zero, which degrades to a point test at the node's own position - still the right
		// side of the line, just without its extent.
		FVector2D Size = FVector2D::ZeroVector;
		if (TSharedPtr<SGraphNode> Widget = Panel.GetNodeWidgetFromGuid(Node->NodeGuid))
		{
			Size = FVector2D(Widget->GetDesiredSize());
		}

		const double MinX = static_cast<double>(Node->NodePosX);
		const double MinY = static_cast<double>(Node->NodePosY);
		const double MaxX = MinX + Size.X;
		const double MaxY = MinY + Size.Y;

		bool bBeyond = false;
		switch (Cardinal)
		{
		case EHVPCardinal::Right: bBeyond = MinX >= GraphLinePoint.X; break;
		case EHVPCardinal::Left:  bBeyond = MaxX <= GraphLinePoint.X; break;
		case EHVPCardinal::Down:  bBeyond = MinY >= GraphLinePoint.Y; break;
		case EHVPCardinal::Up:    bBeyond = MaxY <= GraphLinePoint.Y; break;
		}

		if (bBeyond)
		{
			Result.Add(Node);
		}
	}

	return Result;
}
