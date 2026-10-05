#pragma once

// Graph-building helpers shared by the Code Animation Web tests. Only included from inside
// WITH_DEV_AUTOMATION_TESTS, after CodeAnimWebTestFixture.h.

#include "CodeAnimWebGraphs.h"
#include "EdGraph/EdGraph.h"
#include "CodeAnimWebEvents.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_GetTransitionOutput.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_WebState.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/CompilerResultsLog.h"

namespace CodeAnimWebGraphTests
{
	template <typename T>
	T* Spawn(UEdGraph* Graph, TFunctionRef<void(T*)> Setup)
	{
		FGraphNodeCreator<T> Creator(*Graph);
		T* Node = Creator.CreateNode();
		Setup(Node);
		Creator.Finalize();
		return Node;
	}

	inline UK2Node_VariableSet* SetVar(UEdGraph* Graph, FName Name)
	{
		return Spawn<UK2Node_VariableSet>(Graph, [Name](UK2Node_VariableSet* Node) { Node->VariableReference.SetSelfMember(Name); });
	}

	inline UK2Node_VariableGet* GetVar(UEdGraph* Graph, FName Name)
	{
		return Spawn<UK2Node_VariableGet>(Graph, [Name](UK2Node_VariableGet* Node) { Node->VariableReference.SetSelfMember(Name); });
	}

	inline UK2Node_CallFunction* Call(UEdGraph* Graph, UFunction* Function)
	{
		return Spawn<UK2Node_CallFunction>(Graph, [Function](UK2Node_CallFunction* Node) { Node->SetFromFunction(Function); });
	}

	inline UK2Node_GetTransitionOutput* GetEnd(UEdGraph* Graph, bool bTo, FName Output)
	{
		UK2Node_GetTransitionOutput* Node = Spawn<UK2Node_GetTransitionOutput>(Graph,
			[bTo](UK2Node_GetTransitionOutput* N) { N->bTo = bTo; });
		UEdGraphPin* OutputPin = Node->FindPinChecked(CodeAnimWebNodes::OutputPinName);
		OutputPin->DefaultValue = Output.ToString();
		Node->PinDefaultValueChanged(OutputPin);
		return Node;
	}

	inline UK2Node_FunctionEntry* EntryOf(UEdGraph* Graph)
	{
		TArray<UK2Node_FunctionEntry*> Entries;
		Graph->GetNodesOfClass(Entries);
		return Entries.Num() > 0 ? Entries[0] : nullptr;
	}

	inline UEdGraphPin* Then(UEdGraphNode* Node) { return Node->FindPinChecked(UEdGraphSchema_K2::PN_Then); }
	inline UEdGraphPin* Exec(UEdGraphNode* Node) { return Node->FindPinChecked(UEdGraphSchema_K2::PN_Execute); }

	inline bool Link(FAutomationTestBase& Test, UEdGraphPin* A, UEdGraphPin* B)
	{
		const bool bLinked = A && B && GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(A, B);
		if (!bLinked)
		{
			Test.AddError(FString::Printf(TEXT("Could not link %s to %s"),
				A ? *A->PinName.ToString() : TEXT("null"), B ? *B->PinName.ToString() : TEXT("null")));
		}
		return bLinked;
	}

	inline bool Compile(FAutomationTestBase& Test, UBlueprint* Blueprint)
	{
		FCompilerResultsLog Log;
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
		for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
		{
			if (Message->GetSeverity() == EMessageSeverity::Error)
			{
				Test.AddError(FString::Printf(TEXT("%s: %s"), *Blueprint->GetName(), *Message->ToText().ToString()));
			}
		}
		return Log.NumErrors == 0;
	}

	inline UFunction* MathFunction(FName Name)
	{
		return UKismetMathLibrary::StaticClass()->FindFunctionByName(Name);
	}
}
