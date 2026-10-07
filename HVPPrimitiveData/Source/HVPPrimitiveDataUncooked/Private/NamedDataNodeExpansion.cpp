#include "NamedDataNodeExpansion.h"

#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Self.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/CompilerResultsLog.h"
#include "KismetCompiler.h"

#define LOCTEXT_NAMESPACE "NamedDataNodeExpansion"

bool NamedDataNodeExpansion::CheckSelfTarget(FKismetCompilerContext& CompilerContext, UK2Node* Node, UEdGraphPin* Target,
	const UClass* TargetClass, const FText& Message)
{
	if (Target->LinkedTo.Num() > 0)
	{
		return true;
	}
	const UClass* Context = CompilerContext.Blueprint ? CompilerContext.Blueprint->ParentClass : nullptr;
	if (Context && Context->IsChildOf(TargetClass))
	{
		return true;
	}
	CompilerContext.MessageLog.Error(*Message.ToString(), Node);
	return false;
}

bool NamedDataNodeExpansion::GatherTargets(FKismetCompilerContext& CompilerContext, UK2Node* Node, UEdGraph* SourceGraph,
	UEdGraphPin* Target, UEdGraphPin* TargetsInput, bool& bWired)
{
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	// The loop happens in C++ rather than in script.
	if (Target->LinkedTo.Num() == 1 && Target->LinkedTo[0]->PinType.IsArray())
	{
		bWired &= CompilerContext.MovePinLinksToIntermediate(*Target, *TargetsInput).CanSafeConnect();
		return true;
	}

	TArray<UEdGraphPin*> Sources = Target->LinkedTo;
	if (Sources.ContainsByPredicate([](const UEdGraphPin* Source) { return Source->PinType.IsArray(); }))
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("MixedTargets",
			"@@: Target takes one array, or any number of single meshes, but not both.").ToString(), Node);
		return false;
	}
	if (Sources.Num() == 0)
	{
		UK2Node_Self* Self = CompilerContext.SpawnIntermediateNode<UK2Node_Self>(Node, SourceGraph);
		Self->AllocateDefaultPins();
		Sources.Add(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self));
	}
	Target->BreakAllPinLinks();

	UK2Node_MakeArray* Meshes = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(Node, SourceGraph);
	Meshes->AllocateDefaultPins();
	bWired &= Schema->TryCreateConnection(Meshes->GetOutputPin(), TargetsInput);
	Meshes->PinConnectionListChanged(Meshes->GetOutputPin());
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		if (Index > 0)
		{
			Meshes->AddInputPin();
		}
		bWired &= Schema->TryCreateConnection(Sources[Index], Meshes->FindPinChecked(Meshes->GetPinName(Index)));
	}
	return true;
}

bool NamedDataNodeExpansion::WireValues(FKismetCompilerContext& CompilerContext, UK2Node* Node, UEdGraph* SourceGraph,
	UEdGraphPin* ValuesInput, int32 Count, TConstArrayView<FValueWrite> Writes)
{
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	UK2Node_MakeArray* Values = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(Node, SourceGraph);
	Values->AllocateDefaultPins();
	bool bWired = Schema->TryCreateConnection(Values->GetOutputPin(), ValuesInput);
	Values->PinConnectionListChanged(Values->GetOutputPin());
	for (int32 Index = 1; Index < Count; ++Index)
	{
		Values->AddInputPin();
	}
	auto Element = [Values](int32 Index) { return Values->FindPinChecked(Values->GetPinName(Index)); };

	for (const FValueWrite& Write : Writes)
	{
		if (Write.Width == 1)
		{
			// Moves the default too, when nothing is connected.
			bWired &= CompilerContext.MovePinLinksToIntermediate(*Write.Pin, *Element(Write.Offset)).CanSafeConnect();
			continue;
		}

		UK2Node_CallFunction* Break = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(Node, SourceGraph);
		Break->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BreakColor), UKismetMathLibrary::StaticClass());
		Break->AllocateDefaultPins();
		bWired &= CompilerContext.MovePinLinksToIntermediate(*Write.Pin, *Break->FindPinChecked(TEXT("InColor"))).CanSafeConnect();
		const TCHAR* Channels[] = { TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A") };
		for (int32 Channel = 0; Channel < FMath::Min(Write.Width, 4); ++Channel)
		{
			bWired &= Schema->TryCreateConnection(Break->FindPinChecked(Channels[Channel]), Element(Write.Offset + Channel));
		}
	}
	return bWired;
}

#undef LOCTEXT_NAMESPACE
