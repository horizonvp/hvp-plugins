#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CodeAnimWebTestFixture.h"
#include "CodeAnimWebTestListener.h"
#include "CodeAnimWebEvents.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebEvaluationTest, "HVP.CodeAnimWeb.Evaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodeAnimWebEvaluationTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;

	FFixture Fixture;
	TestEqual(TEXT("Three outputs baked on sync"), Fixture.Defaults()->GetOutputNames().Num(), 3);
	TestEqual(TEXT("One State Values row per enumerator"), Fixture.Defaults()->States.Num(), 3);

	const TStrongObjectPtr<UCodeAnimationWeb> Holder(Fixture.NewWeb());
	UCodeAnimationWeb* Web = Holder.Get();
	auto Scale = [Web]() { return Read<double>(Web, TEXT("Scale")); };

	Web->UpdateAtTime(0.0);
	TestEqual(TEXT("Starts in the first state, at defaults"), Scale(), 1.0);
	TestEqual(TEXT("GetState reports the first state"), static_cast<int32>(Web->GetState()), 0);

	// 0 -> 1 on the default transition: 1s, linear.
	Web->SetStateAtTime(1, 0.0);
	Web->UpdateAtTime(0.25);
	TestEqual(TEXT("Quarter way: lerped"), Scale(), 1.25, 1e-6);
	TestTrue(TEXT("Quarter way: the bool has not switched yet"), Read<bool>(Web, TEXT("Visible")));
	Web->UpdateAtTime(0.5);
	TestEqual(TEXT("Halfway: colour lerped"), Read<FLinearColor>(Web, TEXT("Tint")).R, 0.5f, 1e-5f);
	TestFalse(TEXT("Halfway: the bool switches"), Read<bool>(Web, TEXT("Visible")));
	Web->UpdateAtTime(1.0);
	TestEqual(TEXT("Arrived"), Scale(), 2.0, 1e-6);
	TestEqual(TEXT("GetState reports the new state"), static_cast<int32>(Web->GetState()), 1);

	// Head back to 0, then change mind halfway to 2: the interrupt starts from exactly where it is.
	Web->SetStateAtTime(0, 1.0);
	Web->UpdateAtTime(1.5);
	const double BeforeInterrupt = Scale();
	TestEqual(TEXT("Halfway back"), BeforeInterrupt, 1.5, 1e-6);
	Web->SetStateAtTime(2, 1.5);
	TestEqual(TEXT("No jump on interrupt"), Scale(), BeforeInterrupt, 1e-6);
	// At 2.0 the 1->0 layer finishes (value 1.0) and the interrupt layer is half done towards 0.5.
	Web->UpdateAtTime(2.0);
	TestEqual(TEXT("Nested blend, live underneath"), Scale(), 0.75, 1e-6);
	Web->UpdateAtTime(2.5);
	TestEqual(TEXT("Interrupt arrives"), Scale(), 0.5, 1e-6);
	TestFalse(TEXT("Settled"), Web->IsTransitioning());

	// An authored two-way transition 0 <-> 2 over 2s: used from 2 (reached), played in reverse.
	FCodeAnimWebTransition& Authored = Fixture.Defaults()->Transitions.AddDefaulted_GetRef();
	Authored.From.Key = FName(Fixture.Enum->GetNameStringByIndex(0));
	Authored.To.Key = FName(Fixture.Enum->GetNameStringByIndex(2));
	Authored.bTwoWay = true;
	Authored.Timing.Duration = 2.0f;
	Authored.Timing.Easing = EEasingFunc::EaseIn;
	Authored.Timing.BlendExponent = 2.0f;

	Web->SetStateAtTime(0, 3.0);
	Web->UpdateAtTime(4.0);
	// Reversed ease-in: alpha = 1 - (1 - 0.5)^2 = 0.75, so 0.5 + (1 - 0.5) * 0.75.
	TestEqual(TEXT("Two-way transition played in reverse"), Scale(), 0.875, 1e-6);
	Web->UpdateAtTime(5.0);
	Web->SetStateAtTime(2, 5.0);
	Web->UpdateAtTime(6.0);
	// Forward ease-in: alpha = 0.5^2 = 0.25, so 1 + (0.5 - 1) * 0.25.
	TestEqual(TEXT("Two-way transition played forwards"), Scale(), 0.875, 1e-6);

	// Hammering state changes past MaxBlendDepth freezes the oldest layers without jumping.
	Fixture.Defaults()->MaxBlendDepth = 2;
	double Time = 7.0;
	for (int32 Step = 0; Step < 8; ++Step)
	{
		Web->UpdateAtTime(Time);
		const double Before = Scale();
		Web->SetStateAtTime(static_cast<uint8>(Step % 3), Time);
		TestEqual(FString::Printf(TEXT("No jump on rapid change %d"), Step), Scale(), Before, 1e-6);
		Time += 0.1;
	}

	Web->SetStateAtTime(1, Time, /*bInstant*/ true);
	TestEqual(TEXT("Instant"), Scale(), 2.0, 1e-6);
	TestFalse(TEXT("Instant leaves nothing running"), Web->IsTransitioning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebSyncTest, "HVP.CodeAnimWeb.Sync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodeAnimWebSyncTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;

	FFixture Fixture;
	UUserDefinedEnum* Enum = Fixture.Enum.Get();
	const FName Key1 = FName(Enum->GetNameStringByIndex(1));

	// Renaming an enumerator changes its display name only; its values follow.
	FEnumEditorUtils::SetEnumeratorDisplayName(Enum, 1, FText::FromString(TEXT("Blocked")));
	TestEqual(TEXT("Renamed state keeps its overrides"), Fixture.Entry(1).OverriddenOutputs.Num(), 3);
	TestEqual(TEXT("Renamed state shows its new name"), Fixture.Entry(1).DisplayName.ToString(), FString(TEXT("Blocked")));

	// Adding one gives a new row with nothing set.
	FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
	TestEqual(TEXT("New enumerator, new row"), Fixture.Defaults()->States.Num(), 4);
	TestEqual(TEXT("New row sets nothing"), Fixture.Entry(3).OverriddenOutputs.Num(), 0);

	// Removing one keeps its values, flagged, until cleaned up.
	FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, 1);
	const FCodeAnimWebStateEntry* Orphan = Fixture.Defaults()->States.FindByPredicate(
		[Key1](const FCodeAnimWebStateEntry& E) { return E.Key == Key1; });
	TestTrue(TEXT("Removed state kept"), Orphan && Orphan->bOrphaned);
	TestTrue(TEXT("Removed state keeps its values"), Orphan && Orphan->OverriddenOutputs.Num() == 3);
	Fixture.Defaults()->RemoveOrphanedStates();
	TestEqual(TEXT("Orphans removed on request"), Fixture.Defaults()->States.Num(), 3);

	// Renaming an output variable keeps each state's value for it: overrides are keyed by variable GUID.
	FBlueprintEditorUtils::RenameMemberVariable(Fixture.Blueprint.Get(), TEXT("Scale"), TEXT("Size"));
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint.Get());
	const FCodeAnimWebStateEntry& State2 = Fixture.Entry(1);	// was index 2 before the removal
	const TValueOrError<double, EPropertyBagResult> Size = State2.Values.GetValueDouble(TEXT("Size"));
	TestTrue(TEXT("Renamed output still overridden"), State2.OverriddenOutputs.Num() == 1);
	TestTrue(TEXT("Renamed output keeps its value"), Size.HasValue() && FMath::IsNearlyEqual(Size.GetValue(), 0.5));

	const TStrongObjectPtr<UCodeAnimationWeb> Holder(Fixture.NewWeb());
	Holder->SetStateAtTime(static_cast<uint8>(Enum->GetValueByIndex(1)), 0.0, /*bInstant*/ true);
	TestEqual(TEXT("Renamed output drives the renamed variable"), Read<double>(Holder.Get(), TEXT("Size")), 0.5, 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebTraversalTest, "HVP.CodeAnimWeb.Traversal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodeAnimWebTraversalTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;

	FFixture Fixture;
	UCodeAnimationWeb* Defaults = Fixture.Defaults();
	auto Key = [&Fixture](int32 Index) { return FName(Fixture.Enum->GetNameStringByIndex(Index)); };
	auto AddTransition = [Defaults, &Key](int32 From, int32 To, float Duration)
	{
		FCodeAnimWebTransition& Transition = Defaults->Transitions.AddDefaulted_GetRef();
		Transition.From.Key = Key(From);
		Transition.To.Key = Key(To);
		Transition.Timing.Duration = Duration;
		Transition.Timing.Easing = EEasingFunc::Linear;
	};

	// 0 -> 1 -> 2, one second each, one-way. Scale: state 0 = 1, state 1 = 2, state 2 = 0.5.
	AddTransition(0, 1, 1.0f);
	AddTransition(1, 2, 1.0f);
	Defaults->bTransitionTraversalOnly = true;

	{
		const TStrongObjectPtr<UCodeAnimationWeb> Web(Fixture.NewWeb());
		auto Scale = [&Web]() { return Read<double>(Web.Get(), TEXT("Scale")); };
		Web->UpdateAtTime(0.0);

		TestTrue(TEXT("A state two transitions away is accepted"), Web->SetStateAtTime(2, 0.0));
		Web->UpdateAtTime(0.5);
		TestEqual(TEXT("First leg: 0 -> 1"), Scale(), 1.5, 1e-6);
		TestEqual(TEXT("GetState reports the destination"), static_cast<int32>(Web->GetState()), 2);
		Web->UpdateAtTime(1.5);
		TestEqual(TEXT("Second leg: 1 -> 2, starting as the first ends"), Scale(), 1.25, 1e-6);
		Web->UpdateAtTime(2.0);
		TestEqual(TEXT("Arrived"), Scale(), 0.5, 1e-6);
		TestFalse(TEXT("Nothing left to play"), Web->IsTransitioning());

		TestFalse(TEXT("No transitions lead back: refused"), Web->SetStateAtTime(0, 2.0));
		Web->UpdateAtTime(3.0);
		TestEqual(TEXT("...and the Web stays put"), Scale(), 0.5, 1e-6);
		TestEqual(TEXT("...in the state it was in"), static_cast<int32>(Web->GetState()), 2);
	}

	// A direct 0 -> 2 taking five seconds: fewest transitions takes it, shortest time goes round.
	AddTransition(0, 2, 5.0f);
	{
		const TStrongObjectPtr<UCodeAnimationWeb> Web(Fixture.NewWeb());
		Web->UpdateAtTime(0.0);
		Web->SetStateAtTime(2, 0.0);
		Web->UpdateAtTime(0.5);
		TestEqual(TEXT("Fewest transitions: the direct one"), Read<double>(Web.Get(), TEXT("Scale")), 0.95, 1e-6);
	}

	Defaults->PathCost = ECodeAnimWebPathCost::ShortestTime;
	{
		const TStrongObjectPtr<UCodeAnimationWeb> Web(Fixture.NewWeb());
		Web->UpdateAtTime(0.0);
		Web->SetStateAtTime(2, 0.0);
		Web->UpdateAtTime(0.5);
		TestEqual(TEXT("Shortest time: via state 1"), Read<double>(Web.Get(), TEXT("Scale")), 1.5, 1e-6);

		// Change of plan mid-route: the leg in progress finishes, and that is where it stops.
		TestTrue(TEXT("Re-target mid-route"), Web->SetStateAtTime(1, 0.5));
		Web->UpdateAtTime(1.5);
		TestEqual(TEXT("Stopped at the new target"), Read<double>(Web.Get(), TEXT("Scale")), 2.0, 1e-6);
		TestFalse(TEXT("...and stays there"), Web->IsTransitioning());
	}

	{
		// One long frame plays a whole route through, each leg timed from the end of the last.
		const TStrongObjectPtr<UCodeAnimationWeb> Web(Fixture.NewWeb());
		Web->UpdateAtTime(0.0);
		Web->SetStateAtTime(2, 0.0);
		Web->UpdateAtTime(10.0);
		TestEqual(TEXT("Whole route in one step"), Read<double>(Web.Get(), TEXT("Scale")), 0.5, 1e-6);
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebOutputChangedTest, "HVP.CodeAnimWeb.OutputChanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Web Blueprint's On Outputs Changed: at most once per update, naming exactly the outputs that
 * moved. (The C++ On Output Changed is fired from the same place with the same names.)
 */
bool FCodeAnimWebOutputChangedTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;

	FFixture Fixture;
	UBlueprint* BP = Fixture.Blueprint.Get();
	CodeAnimWebEvents::Reconcile(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	const FMulticastDelegateProperty* Event = FindFProperty<FMulticastDelegateProperty>(BP->GeneratedClass, TEXT("OnOutputsChanged"));
	if (!TestNotNull(TEXT("The Web class has On Outputs Changed"), Event))
	{
		return false;
	}

	const TStrongObjectPtr<UCodeAnimationWeb> Holder(Fixture.NewWeb());
	UCodeAnimationWeb* Web = Holder.Get();
	const TStrongObjectPtr<UCodeAnimWebTestListener> Listener(NewObject<UCodeAnimWebTestListener>());
	// Bound the way a Blueprint's Bind Event node binds: onto the dispatcher property of this instance.
	FScriptDelegate Handler;
	Handler.BindUFunction(Listener.Get(), GET_FUNCTION_NAME_CHECKED(UCodeAnimWebTestListener, HandleOutputChanged));
	Event->AddDelegate(MoveTemp(Handler), Web);
	TArray<TArray<FName>>& Calls = Listener->OutputChangedCalls;

	// And the C++ delegate, which must agree.
	const TStrongObjectPtr<UCodeAnimWebTestListener> NativeListener(NewObject<UCodeAnimWebTestListener>());
	Web->OnOutputChanged.AddDynamic(NativeListener.Get(), &UCodeAnimWebTestListener::HandleOutputChanged);
	auto Names = [](const TArray<FName>& Changed)
	{
		TArray<FString> Strings;
		for (const FName Name : Changed)
		{
			Strings.Add(Name.ToString());
		}
		Strings.Sort();
		return FString::Join(Strings, TEXT(","));
	};

	Web->UpdateAtTime(0.0);
	Calls.Reset();

	// 0 -> 1 over 1s. A quarter in, Scale and Tint have moved; Visible snaps only at half.
	Web->SetStateAtTime(1, 0.0);
	Calls.Reset();
	Web->UpdateAtTime(0.25);
	if (TestEqual(TEXT("One call for two changed outputs"), Calls.Num(), 1))
	{
		TestEqual(TEXT("Names the two that moved"), Names(Calls[0]), FString(TEXT("Scale,Tint")));
	}

	Calls.Reset();
	Web->UpdateAtTime(0.5);
	if (TestEqual(TEXT("One call for three changed outputs"), Calls.Num(), 1))
	{
		TestEqual(TEXT("Names all three"), Names(Calls[0]), FString(TEXT("Scale,Tint,Visible")));
	}

	// Arrived and settled: nothing moves, nothing fires.
	Web->UpdateAtTime(1.0);
	Calls.Reset();
	Web->UpdateAtTime(1.5);
	TestEqual(TEXT("No call when nothing changed"), Calls.Num(), 0);

	Web->RebroadcastOutputs();
	if (TestEqual(TEXT("Rebroadcast: one call"), Calls.Num(), 1))
	{
		TestEqual(TEXT("Rebroadcast names every output"), Names(Calls[0]), FString(TEXT("Scale,Tint,Visible")));
	}

	// Three calls with changes since the native listener was bound (0.25, 0.5, the rebroadcast) plus
	// the arrival at 1.0, each matching.
	const TArray<TArray<FName>>& Native = NativeListener->OutputChangedCalls;
	TestTrue(TEXT("The C++ delegate fired alongside"), Native.Num() >= 4 && Names(Native.Last()) == TEXT("Scale,Tint,Visible"));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebPauseTest, "HVP.CodeAnimWeb.Pause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Paused: the clock stops mid-transition, nothing fires, Set State is refused; unpaused, it carries on without a jump. */
bool FCodeAnimWebPauseTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;

	FFixture Fixture;
	const TStrongObjectPtr<UCodeAnimationWeb> Holder(Fixture.NewWeb());
	UCodeAnimationWeb* Web = Holder.Get();
	const TStrongObjectPtr<UCodeAnimWebTestListener> Listener(NewObject<UCodeAnimWebTestListener>());
	Web->OnOutputChanged.AddDynamic(Listener.Get(), &UCodeAnimWebTestListener::HandleOutputChanged);
	auto Scale = [Web]() { return Read<double>(Web, TEXT("Scale")); };

	// 0 -> 1 over 1s; pause a quarter of the way in.
	Web->UpdateAtTime(0.0);
	Web->SetStateAtTime(1, 0.0);
	Web->UpdateAtTime(0.25);
	TestEqual(TEXT("A quarter in"), Scale(), 1.25, 1e-6);
	Web->SetPausedAtTime(true, 0.25);
	TestTrue(TEXT("Reports paused"), Web->IsPaused());

	Listener->OutputChangedCalls.Reset();
	Web->UpdateAtTime(0.75);
	TestEqual(TEXT("Held where it paused"), Scale(), 1.25, 1e-6);
	TestEqual(TEXT("Nothing fired while paused"), Listener->OutputChangedCalls.Num(), 0);
	TestTrue(TEXT("Still mid-transition"), Web->IsTransitioning());
	TestFalse(TEXT("Set State refused while paused"), Web->SetStateAtTime(2, 0.75));
	TestFalse(TEXT("Instant Set State refused too"), Web->SetStateAtTime(2, 0.75, /*bInstant*/ true));
	TestEqual(TEXT("Still headed for 1"), static_cast<int32>(Web->GetState()), 1);

	// A second paused: unpause at 1.25, and the transition resumes from a quarter, not from 1.25s in.
	Web->SetPausedAtTime(false, 1.25);
	TestFalse(TEXT("Reports unpaused"), Web->IsPaused());
	Web->UpdateAtTime(1.25);
	TestEqual(TEXT("No jump on unpause"), Scale(), 1.25, 1e-6);
	Web->UpdateAtTime(1.5);
	TestEqual(TEXT("Carries on: halfway"), Scale(), 1.5, 1e-6);
	Web->UpdateAtTime(2.0);
	TestEqual(TEXT("Arrives a paused second late"), Scale(), 2.0, 1e-6);
	TestTrue(TEXT("Set State works again"), Web->SetStateAtTime(0, 2.0));

	// Paused before it ever ran.
	const TStrongObjectPtr<UCodeAnimationWeb> Fresh(Fixture.NewWeb());
	Fresh->SetPausedAtTime(true, 0.0);
	TestFalse(TEXT("A Web paused from the start refuses Set State"), Fresh->SetStateAtTime(1, 0.0));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebPauseInStateTest, "HVP.CodeAnimWeb.PauseInState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Pause In State: locked on the way, frozen on the arrival values, released by unpausing. */
bool FCodeAnimWebPauseInStateTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebTests;

	FFixture Fixture;
	const TStrongObjectPtr<UCodeAnimationWeb> Holder(Fixture.NewWeb());
	UCodeAnimationWeb* Web = Holder.Get();
	auto Scale = [Web]() { return Read<double>(Web, TEXT("Scale")); };

	// Head for 1 (1s) and pause there.
	Web->UpdateAtTime(0.0);
	TestTrue(TEXT("Pause In State accepted"), Web->PauseInStateAtTime(1, 0.0));
	TestTrue(TEXT("Locked on the way"), Web->IsStateLocked());
	TestFalse(TEXT("Not frozen yet"), Web->IsPaused());
	TestFalse(TEXT("Set State refused on the way"), Web->SetStateAtTime(2, 0.1));
	TestFalse(TEXT("A second Pause In State refused"), Web->PauseInStateAtTime(2, 0.1));
	Web->UpdateAtTime(0.5);
	TestEqual(TEXT("Still travelling"), Scale(), 1.5, 1e-6);

	// The first update past arrival (at 1.0) freezes on the arrival values, not later ones.
	Web->UpdateAtTime(1.25);
	TestTrue(TEXT("Frozen on arrival"), Web->IsPaused());
	TestEqual(TEXT("On the state's values"), Scale(), 2.0, 1e-6);
	TestEqual(TEXT("In the state asked for"), static_cast<int32>(Web->GetState()), 1);
	Web->UpdateAtTime(3.0);
	TestEqual(TEXT("Stays put"), Scale(), 2.0, 1e-6);

	// Unpausing releases it.
	Web->SetPausedAtTime(false, 3.0);
	TestFalse(TEXT("Released"), Web->IsStateLocked());
	TestTrue(TEXT("Set State works again"), Web->SetStateAtTime(0, 3.0));
	Web->UpdateAtTime(4.0);
	TestEqual(TEXT("Back in 0"), Scale(), 1.0, 1e-6);

	// Released before arriving: it arrives and carries on, unfrozen.
	TestTrue(TEXT("Pause In State again"), Web->PauseInStateAtTime(1, 4.0));
	Web->SetPausedAtTime(false, 4.5);
	TestFalse(TEXT("Released on the way"), Web->IsStateLocked());
	Web->UpdateAtTime(5.5);
	TestEqual(TEXT("Arrived"), Scale(), 2.0, 1e-6);
	TestFalse(TEXT("Not frozen: released before arrival"), Web->IsPaused());

	// Already there: freezes at once.
	TestTrue(TEXT("Pause In State where it already is"), Web->PauseInStateAtTime(1, 6.0));
	TestTrue(TEXT("Frozen at once"), Web->IsPaused());
	TestFalse(TEXT("Refused while paused"), Web->PauseInStateAtTime(0, 6.5));

	// Instant, on a Web that has never run: frozen at once, and on the state's values.
	const TStrongObjectPtr<UCodeAnimationWeb> Fresh(Fixture.NewWeb());
	TestTrue(TEXT("Instant Pause In State"), Fresh->PauseInStateAtTime(1, 0.0, /*bInstant*/ true));
	TestTrue(TEXT("Instant: frozen at once"), Fresh->IsPaused());
	TestEqual(TEXT("Instant: on the state's values"), Read<double>(Fresh.Get(), TEXT("Scale")), 2.0, 1e-6);
	return true;
}

#endif
