#include "HVPGraphSelect.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "GraphEditor.h"
#include "HVPGraphSelectSettings.h"
#include "ToolMenu.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "HVPGraphSelect"

namespace HVPGraphSelect
{
	static bool IsExecPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
	}

	/**
	 * Pure means "carries no execution", tested by the absence of exec pins rather than by asking
	 * UK2Node::IsNodePure. The pin test also gets reroute nodes right: a knot on a data wire has no
	 * exec pins and travels with the maths, while a knot on an exec wire has them and is part of
	 * the run.
	 */
	static bool IsPureNode(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return false;
		}
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (IsExecPin(Pin))
			{
				return false;
			}
		}
		return true;
	}

	/** Connected exec wires in one direction - the count the fork and merge limits are compared to. */
	static int32 CountExecWires(const UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		int32 Count = 0;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (IsExecPin(Pin) && Pin->Direction == Direction)
			{
				Count += Pin->LinkedTo.Num();
			}
		}
		return Count;
	}

	static void GatherExecNeighbours(
		const UEdGraphNode* Node, EEdGraphPinDirection Direction, TArray<UEdGraphNode*>& Out)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!IsExecPin(Pin) || Pin->Direction != Direction)
			{
				continue;
			}
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode())
				{
					Out.AddUnique(Linked->GetOwningNode());
				}
			}
		}
	}

	/** Every pure node feeding Node's data inputs, and everything feeding those, transitively. */
	static void AddPureInputClosure(UEdGraphNode* Node, TSet<UEdGraphNode*>& Selected, int32 MaxNodes)
	{
		TArray<UEdGraphNode*> Pending;
		Pending.Add(Node);

		while (Pending.Num() > 0 && Selected.Num() < MaxNodes)
		{
			const UEdGraphNode* Current = Pending.Pop(EAllowShrinking::No);
			for (const UEdGraphPin* Pin : Current->Pins)
			{
				if (IsExecPin(Pin) || Pin->Direction != EGPD_Input)
				{
					continue;
				}
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					UEdGraphNode* Source = Linked ? Linked->GetOwningNode() : nullptr;
					// Only PURE feeders are absorbed. An impure node upstream on a data wire is a
					// call with its own place in the execution order; dragging it along would move
					// something that belongs to another part of the graph.
					if (!Source || !IsPureNode(Source) || Selected.Contains(Source))
					{
						continue;
					}
					Selected.Add(Source);
					Pending.Add(Source);
				}
			}
		}
	}

	/**
	 * Does this pure node ultimately feed a non-pure node that is NOT selected?
	 *
	 * Pure consumers are transited rather than judged: what matters is where the maths eventually
	 * lands. A Get feeding an Add feeding an unselected Set is shared, and so is the Add.
	 */
	static bool FeedsUnselectedWork(
		const UEdGraphNode* Node, const TSet<UEdGraphNode*>& Selected, TSet<const UEdGraphNode*>& Visited)
	{
		if (Visited.Contains(Node))
		{
			return false;	// already accounted for, and guards against a malformed cycle
		}
		Visited.Add(Node);

		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (IsExecPin(Pin) || Pin->Direction != EGPD_Output)
			{
				continue;
			}
			for (const UEdGraphPin* Linked : Pin->LinkedTo)
			{
				UEdGraphNode* Consumer = Linked ? Linked->GetOwningNode() : nullptr;
				if (!Consumer)
				{
					continue;
				}
				if (IsPureNode(Consumer))
				{
					if (FeedsUnselectedWork(Consumer, Selected, Visited))
					{
						return true;
					}
				}
				else if (!Selected.Contains(Consumer))
				{
					return true;	// real work, outside the selection - this node is shared
				}
			}
		}
		return false;
	}

	/**
	 * Removes pure nodes that serve anything outside the selection.
	 *
	 * ONE PASS is enough, which is worth stating because a fixpoint loop looks necessary here and
	 * is not: the test only asks whether NON-pure consumers are selected, and non-pure nodes are
	 * never removed. So dropping one pure node can never change the verdict for another.
	 */
	static void DropSharedPureInputs(TSet<UEdGraphNode*>& Selected, const UEdGraphNode* StartNode)
	{
		TArray<UEdGraphNode*> Candidates = Selected.Array();
		for (UEdGraphNode* Node : Candidates)
		{
			// The node under the cursor stays whatever it feeds - it is the thing being asked about.
			if (Node == StartNode || !IsPureNode(Node))
			{
				continue;
			}
			TSet<const UEdGraphNode*> Visited;
			if (FeedsUnselectedWork(Node, Selected, Visited))
			{
				Selected.Remove(Node);
			}
		}
	}

	/** Exec traversal in a single direction, unbounded - used by Downstream and Upstream. */
	static void WalkExec(
		UEdGraphNode* Start, EEdGraphPinDirection Direction, TSet<UEdGraphNode*>& Selected, int32 MaxNodes)
	{
		TArray<UEdGraphNode*> Pending;
		Pending.Add(Start);

		while (Pending.Num() > 0 && Selected.Num() < MaxNodes)
		{
			const UEdGraphNode* Current = Pending.Pop(EAllowShrinking::No);

			TArray<UEdGraphNode*> Neighbours;
			GatherExecNeighbours(Current, Direction, Neighbours);
			for (UEdGraphNode* Neighbour : Neighbours)
			{
				if (!Selected.Contains(Neighbour))
				{
					Selected.Add(Neighbour);
					Pending.Add(Neighbour);
				}
			}
		}
	}

	/** Bidirectional exec traversal that stops at forks and merges - Local Branch. */
	static void WalkLocalBranch(
		UEdGraphNode* Start, TSet<UEdGraphNode*>& Selected, const UHVPGraphSelectSettings& Settings)
	{
		TSet<UEdGraphNode*> Visited;
		Visited.Add(Start);

		TArray<UEdGraphNode*> Pending;
		Pending.Add(Start);

		auto IsBoundary = [&Settings](const UEdGraphNode* Node)
		{
			return CountExecWires(Node, EGPD_Output) > Settings.ExecFanOutLimit
				|| CountExecWires(Node, EGPD_Input) > Settings.ExecMergeLimit;
		};

		while (Pending.Num() > 0 && Selected.Num() < Settings.MaxNodes)
		{
			UEdGraphNode* Current = Pending.Pop(EAllowShrinking::No);

			// The gate is per DIRECTION, which is what makes right-clicking the switch itself behave:
			// its own fan-out blocks the forward walk while the run feeding into it still comes in.
			const bool bCanGoForward = CountExecWires(Current, EGPD_Output) <= Settings.ExecFanOutLimit;
			const bool bCanGoBackward = CountExecWires(Current, EGPD_Input) <= Settings.ExecMergeLimit;

			TArray<UEdGraphNode*> Neighbours;
			if (bCanGoForward)
			{
				GatherExecNeighbours(Current, EGPD_Output, Neighbours);
			}
			if (bCanGoBackward)
			{
				GatherExecNeighbours(Current, EGPD_Input, Neighbours);
			}

			for (UEdGraphNode* Neighbour : Neighbours)
			{
				if (Visited.Contains(Neighbour))
				{
					continue;
				}
				Visited.Add(Neighbour);

				if (IsBoundary(Neighbour))
				{
					// Selected only if asked for, and never walked through either way.
					if (Settings.bIncludeBoundaryNodes)
					{
						Selected.Add(Neighbour);
					}
					continue;
				}

				Selected.Add(Neighbour);
				Pending.Add(Neighbour);
			}
		}
	}
}

TSet<UEdGraphNode*> FHVPGraphSelect::Gather(UEdGraphNode* StartNode, EHVPGraphSelectMode Mode)
{
	using namespace HVPGraphSelect;

	TSet<UEdGraphNode*> Selected;
	if (!StartNode)
	{
		return Selected;
	}

	const UHVPGraphSelectSettings& Settings = *GetDefault<UHVPGraphSelectSettings>();

	// Always in, whatever it is. A fork or merge under the cursor is still the thing being asked about.
	Selected.Add(StartNode);

	switch (Mode)
	{
	case EHVPGraphSelectMode::Downstream:
		WalkExec(StartNode, EGPD_Output, Selected, Settings.MaxNodes);
		break;
	case EHVPGraphSelectMode::Upstream:
		WalkExec(StartNode, EGPD_Input, Selected, Settings.MaxNodes);
		break;
	case EHVPGraphSelectMode::LocalBranch:
		WalkLocalBranch(StartNode, Selected, Settings);
		break;
	case EHVPGraphSelectMode::NodeAndContext:
		break;	// the pure closure below is the whole of it
	}

	// Last, and over a copy of the exec-selected set: the closure must not itself become something
	// the exec walk then continues from.
	if (Settings.bIncludePureInputs)
	{
		const TArray<UEdGraphNode*> ExecSelected = Selected.Array();
		for (UEdGraphNode* Node : ExecSelected)
		{
			AddPureInputClosure(Node, Selected, Settings.MaxNodes);
		}

		// After the closure, never during it: whether a pure node is shared depends on the finished
		// selection, and a node pulled in later can be exactly what makes an earlier one contained.
		if (Settings.bExcludeSharedPureInputs)
		{
			DropSharedPureInputs(Selected, StartNode);
		}
	}

	return Selected;
}

void FHVPGraphSelect::SelectFrom(UEdGraphNode* StartNode, EHVPGraphSelectMode Mode)
{
	if (!StartNode || !StartNode->GetGraph())
	{
		return;
	}

	TSharedPtr<SGraphEditor> Editor = SGraphEditor::FindGraphEditorForGraph(StartNode->GetGraph());
	if (!Editor.IsValid())
	{
		return;
	}

	const TSet<UEdGraphNode*> Selected = Gather(StartNode, Mode);

	Editor->ClearSelectionSet();
	for (UEdGraphNode* Node : Selected)
	{
		Editor->SetNodeSelection(Node, true);
	}
}

void FHVPGraphSelect::RegisterMenus()
{
	// THE SCHEMA menu, not a node-class menu - and that distinction is the whole trick.
	//
	// SGraphEditorImpl does build a per-node-class menu chain, but only for classes that opt in:
	// UEdGraphNode::IncludeParentNodeContextMenu() returns FALSE by default and barely a dozen node
	// types override it. For everything else - K2Node_CallFunction included, which is most of a
	// graph - the class walk stops immediately and the node's menu is parented straight to
	// "GraphEditor.GraphContextMenu.EdGraphSchema". Extending the UK2Node menu therefore reaches
	// almost nothing; extending the schema's menu reaches every node in every graph.
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(
		UEdGraphSchema::GetContextMenuName(UEdGraphSchema::StaticClass()));
	if (!Menu)
	{
		return;
	}

	// Dynamic, because this one menu also backs the right-click on empty graph space, where there
	// is no node to select from. The engine hangs its own node and pin sections here the same way.
	Menu->AddDynamicSection(TEXT("HVPGraphSelect"), FNewToolMenuDelegate::CreateLambda(
		[](UToolMenu* InMenu)
		{
			const UGraphNodeContextMenuContext* NodeContext =
				InMenu->FindContext<UGraphNodeContextMenuContext>();
			if (!NodeContext || !NodeContext->Node || !NodeContext->Graph)
			{
				return;	// empty-space right-click: contribute nothing
			}

			// Blueprint graphs only. The traversal reads exec pins, which a material or Niagara
			// graph does not have - every node there would look pure and "local branch" would mean
			// something quite different from what it says.
			const UEdGraphSchema* Schema = NodeContext->Graph->GetSchema();
			if (!Schema || !Schema->IsA<UEdGraphSchema_K2>())
			{
				return;
			}

			FToolMenuSection& Section = InMenu->FindOrAddSection(
				TEXT("HVPGraphSelect"), LOCTEXT("SectionLabel", "HVP"));

			Section.AddSubMenu(
				TEXT("SelectConnected"),
				LOCTEXT("SelectConnectedLabel", "Select Connected"),
				LOCTEXT("SelectConnectedTooltip",
					"Select this node together with the nodes it is connected to, by one of several rules."),
				FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
				{
					FToolMenuSection& SubSection =
						SubMenu->FindOrAddSection(TEXT("HVPGraphSelectModes"));

					auto AddMode = [&SubSection](const TCHAR* Name, const FText& Label,
						const FText& Tooltip, EHVPGraphSelectMode Mode)
					{
						SubSection.AddMenuEntry(
							Name, Label, Tooltip, FSlateIcon(),
							FToolUIActionChoice(FToolMenuExecuteAction::CreateLambda(
								[Mode](const FToolMenuContext& Context)
								{
									const UGraphNodeContextMenuContext* Ctx =
										Context.FindContext<UGraphNodeContextMenuContext>();
									if (Ctx && Ctx->Node)
									{
										// The context hands out a const node; selection needs a
										// mutable one, and nothing here mutates the graph itself.
										FHVPGraphSelect::SelectFrom(
											const_cast<UEdGraphNode*>(Ctx->Node.Get()), Mode);
									}
								})));
					};

					AddMode(TEXT("Downstream"),
						LOCTEXT("DownstreamLabel", "Downstream"),
						LOCTEXT("DownstreamTooltip",
							"This node and everything its execution eventually reaches, with the "
							"pure nodes feeding all of them."),
						EHVPGraphSelectMode::Downstream);

					AddMode(TEXT("Upstream"),
						LOCTEXT("UpstreamLabel", "Upstream"),
						LOCTEXT("UpstreamTooltip",
							"This node and everything that eventually executes into it, with the "
							"pure nodes feeding all of them."),
						EHVPGraphSelectMode::Upstream);

					AddMode(TEXT("LocalBranch"),
						LOCTEXT("LocalBranchLabel", "Local Branch"),
						LOCTEXT("LocalBranchTooltip",
							"The straight run this node sits in - both directions along execution, "
							"stopping at a fork or a merge. Grab one arm of a switch without "
							"grabbing the switch."),
						EHVPGraphSelectMode::LocalBranch);

					AddMode(TEXT("NodeAndContext"),
						LOCTEXT("NodeAndContextLabel", "Node + Context"),
						LOCTEXT("NodeAndContextTooltip",
							"Just this node and the pure nodes feeding it, recursively. No "
							"execution traversal - the node and the maths that makes it work."),
						EHVPGraphSelectMode::NodeAndContext);
				}));
		}));
}

#undef LOCTEXT_NAMESPACE
