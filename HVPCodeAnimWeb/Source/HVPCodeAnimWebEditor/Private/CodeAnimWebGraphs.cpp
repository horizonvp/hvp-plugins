#include "CodeAnimWebGraphs.h"

#include "BlueprintEditor.h"
#include "CodeAnimationWeb.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "CodeAnimWebGraphs"

namespace CodeAnimWebGraphs
{
	static const FText GraphCategory = LOCTEXT("GraphCategory", "Code Animation Web");

	/** Text safe as part of a function name. */
	static FString Sanitize(const FString& In)
	{
		FString Out;
		for (const TCHAR Char : In)
		{
			Out.AppendChar(FChar::IsAlnum(Char) ? Char : TEXT('_'));
		}
		return Out.IsEmpty() ? TEXT("Unnamed") : Out;
	}

	static FText StateName(const UCodeAnimationWeb* Defaults, FName Key)
	{
		TArray<TPair<FName, FText>> States;
		Defaults->GetStateList(States);
		for (const TPair<FName, FText>& State : States)
		{
			if (State.Key == Key)
			{
				return State.Value;
			}
		}
		return FText::FromName(Key);
	}

	static const TCHAR* StatePrefix = TEXT("State_");
	static const TCHAR* TransitionPrefix = TEXT("Transition_");
	static const TCHAR* LerpPrefix = TEXT("Lerp_");

	/** The name each kind of graph is given, from what it belongs to. */
	static FString StateGraphName(const UCodeAnimationWeb* Defaults, FName StateKey)
	{
		return StatePrefix + Sanitize(StateName(Defaults, StateKey).ToString());
	}

	static FString TransitionGraphName(const UCodeAnimationWeb* Defaults, const FCodeAnimWebTransition& Transition)
	{
		return FString::Printf(TEXT("%s%s_%s_%s"), TransitionPrefix,
			*Sanitize(StateName(Defaults, Transition.From.Key).ToString()),
			Transition.bTwoWay ? TEXT("And") : TEXT("To"),
			*Sanitize(StateName(Defaults, Transition.To.Key).ToString()));
	}

	static FString LerpGraphName(FName OutputName)
	{
		return LerpPrefix + Sanitize(OutputName.ToString());
	}

	/** Name is Wanted, or Wanted made unique with a number - FindUniqueKismetName's Wanted_1, Wanted_2... */
	static bool IsNameFor(const FString& Name, const FString& Wanted)
	{
		if (Name == Wanted)
		{
			return true;
		}
		if (!Name.StartsWith(Wanted + TEXT("_")))
		{
			return false;
		}
		const FString Suffix = Name.RightChop(Wanted.Len() + 1);
		return !Suffix.IsEmpty() && Suffix.IsNumeric();
	}

	/** A new function graph whose entry has the given float inputs, in the order the Web fills them. */
	static UEdGraph* CreateGraph(UBlueprint* Blueprint, const FString& BaseName, TConstArrayView<FName> Inputs, const FText& Tooltip)
	{
		const FName Name = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, BaseName);
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, /*bIsUserCreated*/ true, nullptr);

		TArray<UK2Node_FunctionEntry*> Entries;
		Graph->GetNodesOfClass(Entries);
		if (Entries.Num() > 0)
		{
			UK2Node_FunctionEntry* Entry = Entries[0];
			FEdGraphPinType Real;
			Real.PinCategory = UEdGraphSchema_K2::PC_Real;
			Real.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			for (const FName& Input : Inputs)
			{
				Entry->CreateUserDefinedPin(Input, Real, EGPD_Output, /*bUseUniqueName*/ false);
			}
			Entry->MetaData.Category = GraphCategory;
			Entry->MetaData.ToolTip = Tooltip;
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return Graph;
	}

	static void Open(UEdGraph* Graph)
	{
		if (Graph)
		{
			FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Graph);
		}
	}

	/** The Web's class defaults, fetched fresh: a compile can replace them. */
	static UCodeAnimationWeb* DefaultsOf(const UBlueprint* Blueprint)
	{
		return Blueprint && Blueprint->GeneratedClass
			? Cast<UCodeAnimationWeb>(Blueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
	}
}

UEdGraph* CodeAnimWebGraphs::FindGraph(const UBlueprint* Blueprint, const FGuid& Guid)
{
	if (!Blueprint || !Guid.IsValid())
	{
		return nullptr;
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GraphGuid == Guid)
		{
			return Graph;
		}
	}
	return nullptr;
}

FText CodeAnimWebGraphs::ButtonLabel(const UBlueprint* Blueprint, const FGuid& Guid)
{
	return FindGraph(Blueprint, Guid) ? LOCTEXT("OpenGraph", "Open Graph") : LOCTEXT("AddGraph", "+ Graph");
}

UEdGraph* CodeAnimWebGraphs::OpenOrCreateStateGraph(UCodeAnimationWeb* Defaults, FName StateKey, bool bOpen)
{
	UBlueprint* Blueprint = Defaults ? Defaults->GetWebBlueprint() : nullptr;
	FCodeAnimWebStateEntry* Entry = Blueprint
		? Defaults->States.FindByPredicate([StateKey](const FCodeAnimWebStateEntry& E) { return E.Key == StateKey; })
		: nullptr;
	if (!Entry)
	{
		return nullptr;
	}
	if (UEdGraph* Existing = FindGraph(Blueprint, Entry->GraphGuid))
	{
		if (bOpen)
		{
			Open(Existing);
		}
		return Existing;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddStateGraph", "Add State Graph"));
	Blueprint->Modify();
	UEdGraph* Graph = CreateGraph(Blueprint, StateGraphName(Defaults, StateKey),
		{ CodeAnimWeb::TimeInStateParam },
		LOCTEXT("StateGraphTooltip",
			"Runs every frame this state is in play, after its State Values are applied. Set outputs from TimeInState "
			"(seconds since the Web headed for this state); any output left alone keeps its State Value."));

	if (UCodeAnimationWeb* Fresh = DefaultsOf(Blueprint))
	{
		if (FCodeAnimWebStateEntry* FreshEntry = Fresh->States.FindByPredicate(
			[StateKey](const FCodeAnimWebStateEntry& E) { return E.Key == StateKey; }))
		{
			Fresh->Modify();
			FreshEntry->GraphGuid = Graph->GraphGuid;
			FreshEntry->GraphFunction = Graph->GetFName();
			Fresh->OwnedGraphs.AddUnique(Graph->GraphGuid);
		}
	}
	if (bOpen)
	{
		Open(Graph);
	}
	return Graph;
}

UEdGraph* CodeAnimWebGraphs::OpenOrCreateTransitionGraph(UCodeAnimationWeb* Defaults, int32 TransitionIndex, bool bOpen)
{
	UBlueprint* Blueprint = Defaults ? Defaults->GetWebBlueprint() : nullptr;
	if (!Blueprint || !Defaults->Transitions.IsValidIndex(TransitionIndex))
	{
		return nullptr;
	}
	const FCodeAnimWebTransition& Transition = Defaults->Transitions[TransitionIndex];
	if (UEdGraph* Existing = FindGraph(Blueprint, Transition.GraphGuid))
	{
		if (bOpen)
		{
			Open(Existing);
		}
		return Existing;
	}

	const FString Name = TransitionGraphName(Defaults, Transition);

	const FScopedTransaction Transaction(LOCTEXT("AddTransitionGraph", "Add Transition Graph"));
	Blueprint->Modify();
	UEdGraph* Graph = CreateGraph(Blueprint, Name, { CodeAnimWeb::AlphaParam, CodeAnimWeb::ProgressParam },
		LOCTEXT("TransitionGraphTooltip",
			"Runs every frame of this transition, after the automatic blend. Alpha is eased, Progress linear, both 0-1; "
			"use Get From Output / Get To Output for the two ends. Outputs left alone keep the automatic blend. "
			"A two-way transition played backwards runs this with the ends swapped and alpha reversed."));

	if (UCodeAnimationWeb* Fresh = DefaultsOf(Blueprint))
	{
		if (Fresh->Transitions.IsValidIndex(TransitionIndex))
		{
			Fresh->Modify();
			Fresh->Transitions[TransitionIndex].GraphGuid = Graph->GraphGuid;
			Fresh->Transitions[TransitionIndex].GraphFunction = Graph->GetFName();
			Fresh->OwnedGraphs.AddUnique(Graph->GraphGuid);
		}
	}
	if (bOpen)
	{
		Open(Graph);
	}
	return Graph;
}

FGuid CodeAnimWebGraphs::FindCustomLerpGraph(UBlueprint* Blueprint, FName OutputName)
{
	const UCodeAnimationWeb* Defaults = DefaultsOf(Blueprint);
	const FGuid Output = Blueprint ? FBlueprintEditorUtils::FindMemberVariableGuidByName(Blueprint, OutputName) : FGuid();
	const FCodeAnimCustomLerp* Lerp = Defaults
		? Defaults->CustomLerps.FindByPredicate([Output](const FCodeAnimCustomLerp& L) { return L.Output == Output; })
		: nullptr;
	return Lerp ? Lerp->GraphGuid : FGuid();
}

UEdGraph* CodeAnimWebGraphs::OpenOrCreateCustomLerpGraph(UBlueprint* Blueprint, FName OutputName, bool bOpen)
{
	UCodeAnimationWeb* Defaults = DefaultsOf(Blueprint);
	const FGuid Output = Blueprint ? FBlueprintEditorUtils::FindMemberVariableGuidByName(Blueprint, OutputName) : FGuid();
	if (!Defaults || !Output.IsValid())
	{
		return nullptr;
	}
	if (UEdGraph* Existing = FindGraph(Blueprint, FindCustomLerpGraph(Blueprint, OutputName)))
	{
		if (bOpen)
		{
			Open(Existing);
		}
		return Existing;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddCustomLerpGraph", "Add Custom Lerp Graph"));
	Blueprint->Modify();
	UEdGraph* Graph = CreateGraph(Blueprint, LerpGraphName(OutputName),
		{ CodeAnimWeb::AlphaParam, CodeAnimWeb::ProgressParam },
		FText::Format(LOCTEXT("LerpGraphTooltip",
			"Runs every frame of every transition, after the automatic blend and any transition graph, to decide {0}. "
			"Alpha is eased, Progress linear, both 0-1; use Get From Output / Get To Output for the two ends. "
			"It may set other outputs too - a mesh swap driving a scale, say."), FText::FromName(OutputName)));

	if (UCodeAnimationWeb* Fresh = DefaultsOf(Blueprint))
	{
		Fresh->Modify();
		FCodeAnimCustomLerp* Lerp = Fresh->CustomLerps.FindByPredicate([Output](const FCodeAnimCustomLerp& L) { return L.Output == Output; });
		if (!Lerp)
		{
			Lerp = &Fresh->CustomLerps.AddDefaulted_GetRef();
			Lerp->Output = Output;
		}
		Lerp->GraphGuid = Graph->GraphGuid;
		Lerp->GraphFunction = Graph->GetFName();
		Fresh->OwnedGraphs.AddUnique(Graph->GraphGuid);
	}
	if (bOpen)
	{
		Open(Graph);
	}
	return Graph;
}

bool CodeAnimWebGraphs::RemoveUnusedGraphs(UBlueprint* Blueprint)
{
	UCodeAnimationWeb* Defaults = DefaultsOf(Blueprint);
	if (!Defaults)
	{
		return false;
	}

	// A Custom Lerp only counts while its output is still an output set to Custom.
	auto IsCustomOutput = [Blueprint](const FGuid& Output)
	{
		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (Variable.VarGuid == Output)
			{
				// GetMetaData asserts on a missing key, and an output on Auto has no Lerp key at all.
				if (!Variable.HasMetaData(CodeAnimWeb::OutputMetaKey) || !Variable.HasMetaData(CodeAnimWeb::LerpMetaKey))
				{
					return false;
				}
				const int64 Lerp = StaticEnum<ECodeAnimLerpPolicy>()->GetValueByNameString(Variable.GetMetaData(CodeAnimWeb::LerpMetaKey));
				return Lerp == static_cast<int64>(ECodeAnimLerpPolicy::Custom);
			}
		}
		return false;
	};

	const bool bDeadLerps = Defaults->CustomLerps.ContainsByPredicate(
		[&IsCustomOutput](const FCodeAnimCustomLerp& Lerp) { return !IsCustomOutput(Lerp.Output); });

	TSet<FGuid> Live;
	// Orphaned state rows (enumerator removed) keep their graphs: the enumerator may come back, and
	// Remove Orphaned States is the explicit way to let them go.
	for (const FCodeAnimWebStateEntry& Entry : Defaults->States)
	{
		Live.Add(Entry.GraphGuid);
	}
	for (const FCodeAnimWebTransition& Transition : Defaults->Transitions)
	{
		Live.Add(Transition.GraphGuid);
	}
	for (const FCodeAnimCustomLerp& Lerp : Defaults->CustomLerps)
	{
		if (IsCustomOutput(Lerp.Output))
		{
			Live.Add(Lerp.GraphGuid);
		}
	}
	Live.Remove(FGuid());

	// Graphs in use that the record does not list yet: made before the Web kept one.
	bool bAdopt = false;
	for (const FGuid& Guid : Live)
	{
		bAdopt |= !Defaults->OwnedGraphs.Contains(Guid) && FindGraph(Blueprint, Guid) != nullptr;
	}

	TArray<FGuid> Dead;
	for (const FGuid& Guid : Defaults->OwnedGraphs)
	{
		if (!Live.Contains(Guid))
		{
			Dead.Add(Guid);
		}
	}
	if (!bDeadLerps && !bAdopt && Dead.Num() == 0)
	{
		return false;
	}

	// Joins the caller's transaction when there is one; stands as its own undo step when there is not.
	const FScopedTransaction Transaction(LOCTEXT("RemoveUnusedGraphs", "Remove Unused Web Graphs"));
	Defaults->Modify();
	Defaults->CustomLerps.RemoveAll([&IsCustomOutput](const FCodeAnimCustomLerp& Lerp) { return !IsCustomOutput(Lerp.Output); });
	for (const FGuid& Guid : Live)
	{
		if (FindGraph(Blueprint, Guid))
		{
			Defaults->OwnedGraphs.AddUnique(Guid);
		}
	}

	FBlueprintEditor* Editor = nullptr;
	if (GEditor)
	{
		IAssetEditorInstance* Instance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, false);
		if (Instance && Instance->GetEditorName() == TEXT("BlueprintEditor"))
		{
			Editor = static_cast<FBlueprintEditor*>(Instance);
		}
	}

	bool bRemoved = false;
	for (const FGuid& Guid : Dead)
	{
		Defaults->OwnedGraphs.Remove(Guid);
		if (UEdGraph* Graph = FindGraph(Blueprint, Guid))
		{
			Blueprint->Modify();
			Graph->Modify();
			// No recompile per graph; the Blueprint is marked once below.
			FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::MarkTransient);
			if (Editor)
			{
				Editor->CloseDocumentTab(Graph);
			}
			bRemoved = true;
		}
	}
	if (bRemoved)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	return bRemoved;
}

bool CodeAnimWebGraphs::RenameGraphsToMatch(UBlueprint* Blueprint)
{
	UCodeAnimationWeb* Defaults = DefaultsOf(Blueprint);
	if (!Defaults)
	{
		return false;
	}

	struct FRename
	{
		UEdGraph* Graph;
		FString Wanted;
	};
	TArray<FRename> Renames;
	auto Consider = [Blueprint, Defaults, &Renames](const FGuid& Guid, const TCHAR* Prefix, const FString& Wanted)
	{
		UEdGraph* Graph = Defaults->OwnedGraphs.Contains(Guid) ? FindGraph(Blueprint, Guid) : nullptr;
		if (!Graph)
		{
			return;
		}
		const FString Current = Graph->GetName();
		// Renamed by hand to something of its own: theirs now.
		if (!Current.StartsWith(Prefix) || IsNameFor(Current, Wanted))
		{
			return;
		}
		Renames.Add({ Graph, Wanted });
	};

	for (const FCodeAnimWebStateEntry& Entry : Defaults->States)
	{
		if (!Entry.bOrphaned)
		{
			Consider(Entry.GraphGuid, StatePrefix, StateGraphName(Defaults, Entry.Key));
		}
	}
	for (const FCodeAnimWebTransition& Transition : Defaults->Transitions)
	{
		if (Transition.From.IsSet() && Transition.To.IsSet())
		{
			Consider(Transition.GraphGuid, TransitionPrefix, TransitionGraphName(Defaults, Transition));
		}
	}
	for (const FCodeAnimCustomLerp& Lerp : Defaults->CustomLerps)
	{
		const FName Output = FBlueprintEditorUtils::FindMemberVariableNameByGuid(Blueprint, Lerp.Output);
		if (!Output.IsNone())
		{
			Consider(Lerp.GraphGuid, LerpPrefix, LerpGraphName(Output));
		}
	}
	if (Renames.Num() == 0)
	{
		return false;
	}

	// Joins the caller's transaction when there is one; stands as its own undo step when there is not.
	const FScopedTransaction Transaction(LOCTEXT("RenameGraphs", "Rename Web Graphs"));
	Blueprint->Modify();
	Defaults->Modify();
	for (const FRename& Rename : Renames)
	{
		Rename.Graph->Modify();
		const FName Unique = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, Rename.Wanted);
		FBlueprintEditorUtils::RenameGraph(Rename.Graph, Unique.ToString());

		// The Web finds its graphs by GUID; the function name is refreshed now rather than at the next
		// compile, so nothing reads the old one in between.
		const FName NewName = Rename.Graph->GetFName();
		for (FCodeAnimWebStateEntry& Entry : Defaults->States)
		{
			if (Entry.GraphGuid == Rename.Graph->GraphGuid) { Entry.GraphFunction = NewName; }
		}
		for (FCodeAnimWebTransition& Transition : Defaults->Transitions)
		{
			if (Transition.GraphGuid == Rename.Graph->GraphGuid) { Transition.GraphFunction = NewName; }
		}
		for (FCodeAnimCustomLerp& Lerp : Defaults->CustomLerps)
		{
			if (Lerp.GraphGuid == Rename.Graph->GraphGuid) { Lerp.GraphFunction = NewName; }
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
