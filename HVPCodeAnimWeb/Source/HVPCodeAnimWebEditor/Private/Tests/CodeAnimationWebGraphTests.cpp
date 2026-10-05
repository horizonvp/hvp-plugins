#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CodeAnimWebTestFixture.h"

#include "CodeAnimWebGraphTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebGraphsTest, "HVP.CodeAnimWeb.Graphs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodeAnimWebGraphsTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;
	using namespace CodeAnimWebGraphTests;

	FFixture Fixture;
	UBlueprint* BP = Fixture.Blueprint.Get();
	UUserDefinedEnum* Enum = Fixture.Enum.Get();

	// Scale is a Custom lerp: its graph runs last in every transition.
	FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, TEXT("Scale"), nullptr, CodeAnimWeb::LerpMetaKey, TEXT("Custom"));
	if (!Compile(*this, BP))
	{
		return false;
	}

	// State 2's graph: Scale = TimeInState.
	{
		UEdGraph* Graph = CodeAnimWebGraphs::OpenOrCreateStateGraph(Fixture.Defaults(), FName(Enum->GetNameStringByIndex(2)), false);
		TestNotNull(TEXT("State graph created"), Graph);
		if (!Graph) { return false; }
		UK2Node_FunctionEntry* Entry = EntryOf(Graph);
		UK2Node_VariableSet* Set = SetVar(Graph, TEXT("Scale"));
		Link(*this, Then(Entry), Exec(Set));
		Link(*this, Entry->FindPin(CodeAnimWeb::TimeInStateParam), Set->FindPin(TEXT("Scale")));
	}

	// An authored 0 -> 1 transition whose graph holds Tint at the from-end, and sets Scale to 7 (which the
	// Custom Lerp, running after it, must win over).
	{
		FCodeAnimWebTransition& Authored = Fixture.Defaults()->Transitions.AddDefaulted_GetRef();
		Authored.From.Key = FName(Enum->GetNameStringByIndex(0));
		Authored.To.Key = FName(Enum->GetNameStringByIndex(1));
		Authored.Timing.Duration = 1.0f;
		Authored.Timing.Easing = EEasingFunc::Linear;

		UEdGraph* Graph = CodeAnimWebGraphs::OpenOrCreateTransitionGraph(Fixture.Defaults(), 0, false);
		TestNotNull(TEXT("Transition graph created"), Graph);
		if (!Graph) { return false; }
		UK2Node_FunctionEntry* Entry = EntryOf(Graph);
		UK2Node_VariableSet* SetTint = SetVar(Graph, TEXT("Tint"));
		UK2Node_VariableSet* SetScale = SetVar(Graph, TEXT("Scale"));
		UK2Node_GetTransitionOutput* FromTint = GetEnd(Graph, false, TEXT("Tint"));
		Link(*this, Then(Entry), Exec(SetTint));
		Link(*this, FromTint->FindPin(UK2Node_GetTransitionOutput::ValuePinName), SetTint->FindPin(TEXT("Tint")));
		Link(*this, Then(SetTint), Exec(SetScale));
		SetScale->FindPinChecked(TEXT("Scale"))->DefaultValue = TEXT("7.0");
	}

	// Scale's Custom Lerp: Scale = Alpha * 100.
	{
		UEdGraph* Graph = CodeAnimWebGraphs::OpenOrCreateCustomLerpGraph(BP, TEXT("Scale"), false);
		TestNotNull(TEXT("Custom Lerp graph created"), Graph);
		if (!Graph) { return false; }
		UK2Node_FunctionEntry* Entry = EntryOf(Graph);
		UK2Node_CallFunction* Multiply = Call(Graph, MathFunction(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Multiply_DoubleDouble)));
		UK2Node_VariableSet* Set = SetVar(Graph, TEXT("Scale"));
		Link(*this, Then(Entry), Exec(Set));
		Link(*this, Entry->FindPin(CodeAnimWeb::AlphaParam), Multiply->FindPin(TEXT("A")));
		Multiply->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("100.0");
		Link(*this, Multiply->GetReturnValuePin(), Set->FindPin(TEXT("Scale")));
	}

	if (!Compile(*this, BP))
	{
		return false;
	}
	TestFalse(TEXT("State graph linked on compile"), Fixture.Entry(2).GraphFunction.IsNone());
	TestFalse(TEXT("Transition graph linked on compile"), Fixture.Defaults()->Transitions[0].GraphFunction.IsNone());
	TestTrue(TEXT("Custom Lerp linked on compile"),
		Fixture.Defaults()->CustomLerps.Num() == 1 && !Fixture.Defaults()->CustomLerps[0].GraphFunction.IsNone());

	const TStrongObjectPtr<UCodeAnimationWeb> Holder(Fixture.NewWeb());
	UCodeAnimationWeb* Web = Holder.Get();
	auto Scale = [Web]() { return Read<double>(Web, TEXT("Scale")); };

	Web->SetStateAtTime(2, 0.0, /*bInstant*/ true);
	Web->UpdateAtTime(0.5);
	TestEqual(TEXT("State graph animates a settled state"), Scale(), 0.5, 1e-6);
	Web->UpdateAtTime(0.75);
	TestEqual(TEXT("...and keeps animating"), Scale(), 0.75, 1e-6);

	// 2 -> 0 on the default transition: Scale belongs to its Custom Lerp.
	Web->SetStateAtTime(0, 1.0);
	Web->UpdateAtTime(1.25);
	TestEqual(TEXT("Custom Lerp decides its output mid-transition"), Scale(), 25.0, 1e-6);
	Web->UpdateAtTime(2.0);
	TestEqual(TEXT("Arrived: the state's own value"), Scale(), 1.0, 1e-6);

	// 0 -> 1 on the authored transition with a graph.
	Web->SetStateAtTime(1, 2.0);
	Web->UpdateAtTime(2.5);
	const FLinearColor Tint = Read<FLinearColor>(Web, TEXT("Tint"));
	TestTrue(TEXT("Transition graph holds Tint at the from-end"), FMath::IsNearlyEqual(Tint.B, 1.0f) && FMath::IsNearlyZero(Tint.R));
	TestEqual(TEXT("Custom Lerp runs after the transition graph"), Scale(), 50.0, 1e-6);
	Web->UpdateAtTime(3.0);
	TestEqual(TEXT("Transition graph done: target colour"), Read<FLinearColor>(Web, TEXT("Tint")).R, 1.0f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebEventsTest, "HVP.CodeAnimWeb.Events",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodeAnimWebEventsTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;
	using namespace CodeAnimWebGraphTests;

	FFixture Fixture;
	UBlueprint* BP = Fixture.Blueprint.Get();

	auto Params = [BP](FName Event)
	{
		TArray<TPair<FName, FEdGraphPinType>> Out;
		UEdGraph* Graph = FBlueprintEditorUtils::GetDelegateSignatureGraphByName(BP, Event);
		TArray<UK2Node_FunctionEntry*> Entries;
		if (Graph)
		{
			Graph->GetNodesOfClass(Entries);
		}
		if (Entries.Num() > 0)
		{
			for (const TSharedPtr<FUserPinInfo>& Pin : Entries[0]->UserDefinedPins)
			{
				Out.Emplace(Pin->PinName, Pin->PinType);
			}
		}
		return Out;
	};
	auto Has = [BP](const TCHAR* Name) { return FBlueprintEditorUtils::FindNewVariableIndex(BP, Name) != INDEX_NONE; };

	TestTrue(TEXT("Dispatchers made for the outputs"), CodeAnimWebEvents::Reconcile(BP));
	TestTrue(TEXT("One per output"), Has(TEXT("OnScaleChanged")) && Has(TEXT("OnTintChanged")) && Has(TEXT("OnVisibleChanged")));
	TestFalse(TEXT("Reconciling again changes nothing"), CodeAnimWebEvents::Reconcile(BP));

	const TArray<TPair<FName, FEdGraphPinType>> AllParams = Params(TEXT("OnOutputsChanged"));
	TestTrue(TEXT("On Outputs Changed is (Web as its own class, array of names)"), AllParams.Num() == 2
		&& AllParams[0].Key == TEXT("Web") && AllParams[0].Value.PinSubCategoryObject.Get() == BP->GeneratedClass.Get()
		&& AllParams[1].Key == TEXT("ChangedOutputs") && AllParams[1].Value.PinCategory == UEdGraphSchema_K2::PC_Name
		&& AllParams[1].Value.IsArray());

	const TArray<TPair<FName, FEdGraphPinType>> ScaleParams = Params(TEXT("OnScaleChanged"));
	TestTrue(TEXT("Signature is (Web as its own class, new value typed)"), ScaleParams.Num() == 2
		&& ScaleParams[0].Key == TEXT("Web") && ScaleParams[0].Value.PinSubCategoryObject.Get() == BP->GeneratedClass.Get()
		&& ScaleParams[1].Key == TEXT("NewScale") && ScaleParams[1].Value.PinCategory == UEdGraphSchema_K2::PC_Real);

	// Renaming an output renames its dispatcher and parameter.
	FBlueprintEditorUtils::RenameMemberVariable(BP, TEXT("Scale"), TEXT("Size"));
	TestTrue(TEXT("Rename reconciled"), CodeAnimWebEvents::Reconcile(BP));
	TestTrue(TEXT("Dispatcher follows the rename"), Has(TEXT("OnSizeChanged")) && !Has(TEXT("OnScaleChanged")));
	const TArray<TPair<FName, FEdGraphPinType>> SizeParams = Params(TEXT("OnSizeChanged"));
	TestTrue(TEXT("Parameter follows the rename"), SizeParams.Num() == 2 && SizeParams[1].Key == TEXT("NewSize"));

	// Retyping an output retypes the value parameter.
	FEdGraphPinType Int;
	Int.PinCategory = UEdGraphSchema_K2::PC_Int;
	FBlueprintEditorUtils::ChangeMemberVariableType(BP, TEXT("Visible"), Int);
	TestTrue(TEXT("Retype reconciled"), CodeAnimWebEvents::Reconcile(BP));
	const TArray<TPair<FName, FEdGraphPinType>> VisibleParams = Params(TEXT("OnVisibleChanged"));
	TestTrue(TEXT("Parameter follows the type"), VisibleParams.Num() == 2 && VisibleParams[1].Value.PinCategory == UEdGraphSchema_K2::PC_Int);

	// No longer an output: its dispatcher goes.
	FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BP, TEXT("Tint"), nullptr, CodeAnimWeb::OutputMetaKey);
	TestTrue(TEXT("Removal reconciled"), CodeAnimWebEvents::Reconcile(BP));
	TestFalse(TEXT("Dispatcher removed with its output"), Has(TEXT("OnTintChanged")));
	TestTrue(TEXT("On Outputs Changed stays"), Has(TEXT("OnOutputsChanged")));

	return Compile(*this, BP);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebNodesTest, "HVP.CodeAnimWeb.Nodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodeAnimWebNodesTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;
	using namespace CodeAnimWebGraphTests;

	FFixture Fixture;
	CodeAnimWebEvents::Reconcile(Fixture.Blueprint.Get());
	if (!Compile(*this, Fixture.Blueprint.Get()))
	{
		return false;
	}
	UClass* WebClass = Fixture.Blueprint->GeneratedClass;
	UUserDefinedEnum* Enum = Fixture.Enum.Get();
	FMulticastDelegateProperty* OnScaleChanged = FindFProperty<FMulticastDelegateProperty>(WebClass, TEXT("OnScaleChanged"));
	TestNotNull(TEXT("The Web class has On Scale Changed"), OnScaleChanged);
	if (!OnScaleChanged) { return false; }

	// A plain object Blueprint that listens to a Web the ordinary way: Bind Event to On Scale Changed,
	// its red delegate pin wired to a custom event with the dispatcher's signature.
	UPackage* Package = GetTransientPackage();
	const TStrongObjectPtr<UBlueprint> Listener(FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package,
		MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("CodeAnimWebListener")), BPTYPE_Normal));
	UBlueprint* BP = Listener.Get();

	FEdGraphPinType WebType;
	WebType.PinCategory = UEdGraphSchema_K2::PC_Object;
	WebType.PinSubCategoryObject = WebClass;
	FEdGraphPinType Real;
	Real.PinCategory = UEdGraphSchema_K2::PC_Real;
	Real.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	FEdGraphPinType StateType;
	StateType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	StateType.PinSubCategoryObject = Enum;
	FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Web"), WebType);
	FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("LastWeb"), WebType);
	FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Got"), Real, TEXT("-1.0"));
	FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("GotState"), StateType);
	if (!Compile(*this, BP))
	{
		return false;
	}

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
	TestNotNull(TEXT("Event graph"), EventGraph);
	if (!EventGraph) { return false; }

	// Setup: Bind Event to On Scale Changed -> Rebroadcast Outputs.  HandleScale(Web, NewScale): LastWeb = Web, Got = NewScale.
	{
		UK2Node_CustomEvent* Setup = Spawn<UK2Node_CustomEvent>(EventGraph,
			[](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = TEXT("Setup"); });
		UK2Node_AddDelegate* Bind = Spawn<UK2Node_AddDelegate>(EventGraph,
			[OnScaleChanged, WebClass](UK2Node_AddDelegate* Node) { Node->SetFromProperty(OnScaleChanged, false, WebClass); });
		UK2Node_CallFunction* Rebroadcast = Call(EventGraph,
			UCodeAnimationWeb::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UCodeAnimationWeb, RebroadcastOutputs)));
		Link(*this, Then(Setup), Exec(Bind));
		Link(*this, GetVar(EventGraph, TEXT("Web"))->FindPin(TEXT("Web")), Bind->FindPin(UEdGraphSchema_K2::PN_Self));
		Link(*this, Then(Bind), Exec(Rebroadcast));
		Link(*this, GetVar(EventGraph, TEXT("Web"))->FindPin(TEXT("Web")), Rebroadcast->FindPin(UEdGraphSchema_K2::PN_Self));

		UK2Node_CustomEvent* Handler = Spawn<UK2Node_CustomEvent>(EventGraph,
			[](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = TEXT("HandleScale"); });
		Handler->CreateUserDefinedPin(TEXT("Web"), WebType, EGPD_Output, false);
		Handler->CreateUserDefinedPin(TEXT("NewScale"), Real, EGPD_Output, false);
		Link(*this, Handler->FindPin(UK2Node_CustomEvent::DelegateOutputName), Bind->GetDelegatePin());

		UK2Node_VariableSet* SetLast = SetVar(EventGraph, TEXT("LastWeb"));
		UK2Node_VariableSet* SetGot = SetVar(EventGraph, TEXT("Got"));
		Link(*this, Then(Handler), Exec(SetLast));
		Link(*this, Handler->FindPin(TEXT("Web")), SetLast->FindPin(TEXT("LastWeb")));
		Link(*this, Then(SetLast), Exec(SetGot));
		Link(*this, Handler->FindPin(TEXT("NewScale")), SetGot->FindPin(TEXT("Got")));
	}

	// Go: Set Web State (state 1, instant) -> GotState = Get Web State.
	{
		UK2Node_CustomEvent* Go = Spawn<UK2Node_CustomEvent>(EventGraph,
			[](UK2Node_CustomEvent* Node) { Node->CustomFunctionName = TEXT("Go"); });
		UK2Node_SetWebState* Set = Spawn<UK2Node_SetWebState>(EventGraph, [](UK2Node_SetWebState*) {});
		UK2Node_GetWebState* Get = Spawn<UK2Node_GetWebState>(EventGraph, [](UK2Node_GetWebState*) {});
		Link(*this, Then(Go), Exec(Set));
		Link(*this, GetVar(EventGraph, TEXT("Web"))->FindPin(TEXT("Web")), Set->FindPin(UK2Node_CodeAnimWebBase::WebPinName));
		Link(*this, GetVar(EventGraph, TEXT("Web"))->FindPin(TEXT("Web")), Get->FindPin(UK2Node_CodeAnimWebBase::WebPinName));

		UEdGraphPin* State = Set->FindPinChecked(UK2Node_SetWebState::StatePinName);
		TestTrue(TEXT("State pin typed as the Web's enum"), State->PinType.PinSubCategoryObject == Enum);
		State->DefaultValue = Enum->GetNameStringByIndex(1);
		Set->FindPinChecked(UK2Node_SetWebState::InstantPinName)->DefaultValue = TEXT("true");

		UK2Node_VariableSet* SetGotState = SetVar(EventGraph, TEXT("GotState"));
		Link(*this, Then(Set), Exec(SetGotState));
		Link(*this, Get->FindPin(UEdGraphSchema_K2::PN_ReturnValue), SetGotState->FindPin(TEXT("GotState")));
	}

	if (!Compile(*this, BP))
	{
		return false;
	}

	const TStrongObjectPtr<UCodeAnimationWeb> WebHolder(Fixture.NewWeb());
	const TStrongObjectPtr<UObject> ListenerHolder(NewObject<UObject>(Package, BP->GeneratedClass));
	UObject* Instance = ListenerHolder.Get();
	*BP->GeneratedClass->FindPropertyByName(TEXT("Web"))->ContainerPtrToValuePtr<UObject*>(Instance) = WebHolder.Get();

	Instance->ProcessEvent(Instance->FindFunctionChecked(TEXT("Setup")), nullptr);
	TestEqual(TEXT("Bound, rebroadcast: the event got the current value"), Read<double>(Instance, TEXT("Got")), 1.0, 1e-6);
	TestEqual(TEXT("The event's Web parameter is the Web, typed"),
		*BP->GeneratedClass->FindPropertyByName(TEXT("LastWeb"))->ContainerPtrToValuePtr<UObject*>(Instance),
		static_cast<UObject*>(WebHolder.Get()));

	Instance->ProcessEvent(Instance->FindFunctionChecked(TEXT("Go")), nullptr);
	TestEqual(TEXT("Set Web State drove the Web, and the bound event heard the change"), Read<double>(Instance, TEXT("Got")), 2.0, 1e-6);
	TestEqual(TEXT("Get Web State read it back as the enum"),
		static_cast<int32>(Read<uint8>(Instance, TEXT("GotState"))), static_cast<int32>(Enum->GetValueByIndex(1)));
	return true;
}

#endif
