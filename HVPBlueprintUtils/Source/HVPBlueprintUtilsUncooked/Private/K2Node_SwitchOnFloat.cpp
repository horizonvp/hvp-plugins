#include "K2Node_SwitchOnFloat.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_TemporaryVariable.h"
#include "Kismet/KismetMathLibrary.h"
#include "KismetCompiler.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "K2Node_SwitchOnFloat"

namespace HVPSwitchOnFloat
{
	static const FName SelectionPinName(TEXT("Selection"));
	static const FName DefaultPinName(TEXT("Default"));
	static const FName AlphaPinName(TEXT("Alpha"));

	/** Trailing zeroes make an interval label unreadable at a glance; this keeps it tight. */
	static FString FormatBound(double Value)
	{
		FString Text = FString::SanitizeFloat(Value);
		if (Text.Contains(TEXT(".")))
		{
			Text.RemoveFromEnd(TEXT("0"));
			Text.RemoveFromEnd(TEXT("."));
		}
		return Text;
	}
}

UK2Node_SwitchOnFloat::UK2Node_SwitchOnFloat()
{
	// Two half-open bands out of the box, so a fresh node already demonstrates the tiling the
	// inclusivity defaults are chosen for.
	FHVPFloatSwitchRange Low;
	Low.Min = 0.0;
	Low.Max = 0.5;
	FHVPFloatSwitchRange High;
	High.Min = 0.5;
	High.Max = 1.0;
	Ranges.Add(Low);
	Ranges.Add(High);
}

FName UK2Node_SwitchOnFloat::RangePinName(int32 Index)
{
	return *FString::Printf(TEXT("Range_%d"), Index);
}

FText UK2Node_SwitchOnFloat::RangePinFriendlyName(int32 Index) const
{
	if (!Ranges.IsValidIndex(Index))
	{
		return FText::GetEmpty();
	}
	const FHVPFloatSwitchRange& Range = Ranges[Index];
	if (!Range.Label.IsEmpty())
	{
		return FText::FromString(Range.Label);
	}
	// Standard interval notation, so inclusivity is readable off the pin without opening details.
	return FText::FromString(FString::Printf(TEXT("%s%s, %s%s"),
		Range.bIncludeMin ? TEXT("[") : TEXT("("),
		*HVPSwitchOnFloat::FormatBound(Range.Min),
		*HVPSwitchOnFloat::FormatBound(Range.Max),
		Range.bIncludeMax ? TEXT("]") : TEXT(")")));
}

void UK2Node_SwitchOnFloat::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double,
		HVPSwitchOnFloat::SelectionPinName);

	for (int32 Index = 0; Index < Ranges.Num(); ++Index)
	{
		UEdGraphPin* Pin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, RangePinName(Index));
		Pin->PinFriendlyName = RangePinFriendlyName(Index);
	}

	if (bHasDefaultPin)
	{
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, HVPSwitchOnFloat::DefaultPinName);
	}

	// Below the exec outputs, since it describes whichever of them just fired.
	UEdGraphPin* Alpha = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Real,
		UEdGraphSchema_K2::PC_Double, HVPSwitchOnFloat::AlphaPinName);
	Alpha->PinFriendlyName = LOCTEXT("AlphaPin", "Step Alpha");

	Super::AllocateDefaultPins();
}

UEdGraphPin* UK2Node_SwitchOnFloat::GetSelectionPin() const
{
	return FindPin(HVPSwitchOnFloat::SelectionPinName, EGPD_Input);
}

UEdGraphPin* UK2Node_SwitchOnFloat::GetDefaultPin() const
{
	return bHasDefaultPin ? FindPin(HVPSwitchOnFloat::DefaultPinName, EGPD_Output) : nullptr;
}

UEdGraphPin* UK2Node_SwitchOnFloat::GetAlphaPin() const
{
	return FindPin(HVPSwitchOnFloat::AlphaPinName, EGPD_Output);
}

UEdGraphPin* UK2Node_SwitchOnFloat::GetRangePin(int32 Index) const
{
	return FindPin(RangePinName(Index), EGPD_Output);
}

void UK2Node_SwitchOnFloat::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Any edit to the ranges changes either the pin set or a pin's label, and both live in the pins
	// rather than being read at draw time. Reconstruct reuses pins by name, so retuning a range's
	// bounds keeps its wiring - only adding or removing entries moves anything.
	ReconstructNode();
}

FText UK2Node_SwitchOnFloat::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Switch on Float");
}

FText UK2Node_SwitchOnFloat::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip",
		"Fires one output per numeric range containing the value.\n\n"
		"Unlike a real switch, EVERY matching range fires, in pin order - overlapping ranges are "
		"intended rather than ambiguous. The optional Default output fires only when nothing "
		"matched at all.\n\n"
		"Step Alpha reports where the value sat inside the range that fired - 0 at its Min, 1 at "
		"its Max - and stays at 0 when nothing matched.\n\n"
		"Edit the ranges in the details panel; the array is the pin list.");
}

FSlateIcon UK2Node_SwitchOnFloat::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Switch_16x");
	return Icon;
}

FText UK2Node_SwitchOnFloat::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "Flow Control");
}

void UK2Node_SwitchOnFloat::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(ActionKey);
		check(Spawner);
		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

void UK2Node_SwitchOnFloat::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();
	UEdGraphPin* ExecPin = FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	UEdGraphPin* SelectionPin = GetSelectionPin();

	if (!ExecPin || !SelectionPin)
	{
		CompilerContext.MessageLog.Error(
			*LOCTEXT("MissingPins", "@@ is missing its Exec or Selection pin.").ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	// No ranges at all: the value cannot match anything, so this is just a pass-through to Default.
	if (Ranges.Num() == 0)
	{
		if (UEdGraphPin* PassThrough = GetDefaultPin())
		{
			CompilerContext.MovePinLinksToIntermediate(*ExecPin, *PassThrough);
		}
		BreakAllNodeLinks();
		return;
	}

	// The Default output needs to know whether ANY range fired, which is only knowable after all of
	// them have been tested - hence an accumulator rather than an else-chain. Skipped entirely when
	// there is no Default pin, so the common case expands to nothing but branches.
	UK2Node_TemporaryVariable* AnyMatched = nullptr;
	if (bHasDefaultPin)
	{
		AnyMatched = CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
		AnyMatched->VariableType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		AnyMatched->AllocateDefaultPins();
	}

	// Where the value sat inside whichever range fired. Built only when something reads it - an
	// unconnected output should not cost a temporary and an assignment per range.
	UEdGraphPin* AlphaPin = GetAlphaPin();
	UK2Node_TemporaryVariable* AlphaVar = nullptr;
	if (AlphaPin && AlphaPin->LinkedTo.Num() > 0)
	{
		AlphaVar = CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
		AlphaVar->VariableType.PinCategory = UEdGraphSchema_K2::PC_Real;
		AlphaVar->VariableType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		AlphaVar->AllocateDefaultPins();
	}

	auto MakeAssignment = [&](UK2Node_TemporaryVariable* Target, const FString& Literal)
	{
		UK2Node_AssignmentStatement* Assignment =
			CompilerContext.SpawnIntermediateNode<UK2Node_AssignmentStatement>(this, SourceGraph);
		Assignment->AllocateDefaultPins();
		Schema->TryCreateConnection(Target->GetVariablePin(), Assignment->GetVariablePin());
		// The assignment's pins are wildcards until something tells them what they are carrying.
		Assignment->PinConnectionListChanged(Assignment->GetVariablePin());
		if (!Literal.IsEmpty())
		{
			Assignment->GetValuePin()->DefaultValue = Literal;
		}
		return Assignment;
	};

	// Exec enters through the accumulator resets, if there are any, and then the sequence.
	UEdGraphPin* EntryTarget = nullptr;
	UEdGraphPin* ChainOut = nullptr;
	auto AppendToPrologue = [&](UK2Node_AssignmentStatement* Assignment)
	{
		UEdGraphPin* In = Assignment->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
		if (!EntryTarget)
		{
			EntryTarget = In;
		}
		else
		{
			Schema->TryCreateConnection(ChainOut, In);
		}
		ChainOut = Assignment->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output);
	};

	if (AnyMatched)
	{
		AppendToPrologue(MakeAssignment(AnyMatched, TEXT("false")));
	}
	if (AlphaVar)
	{
		AppendToPrologue(MakeAssignment(AlphaVar, TEXT("0.0")));
	}

	// Every range is tested in turn, and a sequence is what makes "in turn" survive the user's graph
	// running off the end of one output before the next is reached.
	UK2Node_ExecutionSequence* Sequence =
		CompilerContext.SpawnIntermediateNode<UK2Node_ExecutionSequence>(this, SourceGraph);
	Sequence->AllocateDefaultPins();
	const int32 NeededOutputs = Ranges.Num() + (bHasDefaultPin ? 1 : 0);
	while (Sequence->GetThenPinGivenIndex(NeededOutputs - 1) == nullptr)
	{
		Sequence->AddInputPin();
	}

	UEdGraphPin* SequenceExec = Sequence->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
	if (EntryTarget && ChainOut)
	{
		CompilerContext.MovePinLinksToIntermediate(*ExecPin, *EntryTarget);
		Schema->TryCreateConnection(ChainOut, SequenceExec);
	}
	else
	{
		CompilerContext.MovePinLinksToIntermediate(*ExecPin, *SequenceExec);
	}

	for (int32 Index = 0; Index < Ranges.Num(); ++Index)
	{
		const FHVPFloatSwitchRange& Range = Ranges[Index];

		UK2Node_CallFunction* InRange =
			CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		InRange->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, InRange_FloatFloat),
			UKismetMathLibrary::StaticClass());
		InRange->AllocateDefaultPins();

		// COPY rather than move: every range reads the same selection, and moving would hand the
		// links to the first one and leave the rest reading nothing.
		UEdGraphPin* ValuePin = InRange->FindPinChecked(TEXT("Value"));
		if (SelectionPin->LinkedTo.Num() > 0)
		{
			CompilerContext.CopyPinLinksToIntermediate(*SelectionPin, *ValuePin);
		}
		else
		{
			// An unconnected selection is a literal typed into the node, which links cannot carry.
			ValuePin->DefaultValue = SelectionPin->DefaultValue;
		}

		InRange->FindPinChecked(TEXT("Min"))->DefaultValue = FString::SanitizeFloat(Range.Min);
		InRange->FindPinChecked(TEXT("Max"))->DefaultValue = FString::SanitizeFloat(Range.Max);
		InRange->FindPinChecked(TEXT("InclusiveMin"))->DefaultValue =
			Range.bIncludeMin ? TEXT("true") : TEXT("false");
		InRange->FindPinChecked(TEXT("InclusiveMax"))->DefaultValue =
			Range.bIncludeMax ? TEXT("true") : TEXT("false");

		UK2Node_IfThenElse* Branch =
			CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
		Branch->AllocateDefaultPins();
		Schema->TryCreateConnection(InRange->GetReturnValuePin(), Branch->GetConditionPin());
		Schema->TryCreateConnection(Sequence->GetThenPinGivenIndex(Index),
			Branch->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input));

		UEdGraphPin* FireFrom = Branch->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output);

		// Both accumulators are written BEFORE the user's graph runs, not after: whatever happens
		// downstream - including the flow leaving and never coming back - must not change whether
		// Default fires or what Step Alpha reads.
		if (AnyMatched)
		{
			UK2Node_AssignmentStatement* Mark = MakeAssignment(AnyMatched, TEXT("true"));
			Schema->TryCreateConnection(FireFrom,
				Mark->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input));
			FireFrom = Mark->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		}

		if (AlphaVar)
		{
			// NormalizeToRange is (Value - Min) / (Max - Min), and it answers 0 or 1 rather than
			// dividing by zero when a range has no width - so a degenerate range needs no guard here.
			UK2Node_CallFunction* Normalize =
				CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			Normalize->FunctionReference.SetExternalMember(
				GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, NormalizeToRange),
				UKismetMathLibrary::StaticClass());
			Normalize->AllocateDefaultPins();

			UEdGraphPin* NormalizeValue = Normalize->FindPinChecked(TEXT("Value"));
			if (SelectionPin->LinkedTo.Num() > 0)
			{
				CompilerContext.CopyPinLinksToIntermediate(*SelectionPin, *NormalizeValue);
			}
			else
			{
				NormalizeValue->DefaultValue = SelectionPin->DefaultValue;
			}
			Normalize->FindPinChecked(TEXT("RangeMin"))->DefaultValue =
				FString::SanitizeFloat(Range.Min);
			Normalize->FindPinChecked(TEXT("RangeMax"))->DefaultValue =
				FString::SanitizeFloat(Range.Max);

			UK2Node_AssignmentStatement* SetAlpha = MakeAssignment(AlphaVar, FString());
			Schema->TryCreateConnection(Normalize->GetReturnValuePin(), SetAlpha->GetValuePin());
			Schema->TryCreateConnection(FireFrom,
				SetAlpha->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input));
			FireFrom = SetAlpha->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output);
		}

		if (UEdGraphPin* RangePin = GetRangePin(Index))
		{
			CompilerContext.MovePinLinksToIntermediate(*RangePin, *FireFrom);
		}
	}

	if (AnyMatched)
	{
		UK2Node_CallFunction* NotMatched =
			CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		NotMatched->FunctionReference.SetExternalMember(
			GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Not_PreBool),
			UKismetMathLibrary::StaticClass());
		NotMatched->AllocateDefaultPins();
		Schema->TryCreateConnection(AnyMatched->GetVariablePin(),
			NotMatched->FindPinChecked(TEXT("A")));

		UK2Node_IfThenElse* DefaultBranch =
			CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
		DefaultBranch->AllocateDefaultPins();
		Schema->TryCreateConnection(NotMatched->GetReturnValuePin(),
			DefaultBranch->GetConditionPin());
		// Last in the sequence, so every range has been tested by the time it is reached.
		Schema->TryCreateConnection(Sequence->GetThenPinGivenIndex(Ranges.Num()),
			DefaultBranch->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input));

		if (UEdGraphPin* DefaultPin = GetDefaultPin())
		{
			CompilerContext.MovePinLinksToIntermediate(*DefaultPin,
				*DefaultBranch->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output));
		}
	}

	if (AlphaVar && AlphaPin)
	{
		CompilerContext.MovePinLinksToIntermediate(*AlphaPin, *AlphaVar->GetVariablePin());
	}

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
