#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CodeAnimWebTestFixture.h"
#include "CodeAnimWebGraphTestHelpers.h"

#include "HAL/PlatformTime.h"

/**
 * Load benchmark, not a correctness test: many Webs evaluated frame after frame, timed. Tagged as a
 * stress test so it stays out of ordinary runs; run it by name. Numbers are desktop editor numbers -
 * a Quest runs this kind of game-thread work several times slower.
 */
namespace CodeAnimWebPerf
{
	using namespace CodeAnimWebTests;
	using namespace CodeAnimWebGraphTests;

	constexpr double FrameTime = 1.0 / 72.0;	// Quest refresh
	constexpr int32 Frames = 720;				// ten seconds

	/**
	 * The heavier end of a realistic device Web: on top of the fixture's three outputs and table, a live
	 * state graph on state 2 (Scale from TimeInState, like a wiggle), a transition graph on 0 -> 1 that
	 * reads the from-end, and a Custom Lerp on Scale - so every frame of a transition runs two graphs.
	 */
	void AddGraphs(FAutomationTestBase& Test, FFixture& Fixture)
	{
		UBlueprint* BP = Fixture.Blueprint.Get();
		UUserDefinedEnum* Enum = Fixture.Enum.Get();
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, TEXT("Scale"), nullptr, CodeAnimWeb::LerpMetaKey, TEXT("Custom"));
		Compile(Test, BP);

		{
			UEdGraph* Graph = CodeAnimWebGraphs::OpenOrCreateStateGraph(Fixture.Defaults(), FName(Enum->GetNameStringByIndex(2)), false);
			UK2Node_FunctionEntry* Entry = EntryOf(Graph);
			UK2Node_VariableSet* Set = SetVar(Graph, TEXT("Scale"));
			Link(Test, Then(Entry), Exec(Set));
			Link(Test, Entry->FindPin(CodeAnimWeb::TimeInStateParam), Set->FindPin(TEXT("Scale")));
		}
		{
			FCodeAnimWebTransition& Authored = Fixture.Defaults()->Transitions.AddDefaulted_GetRef();
			Authored.From.Key = FName(Enum->GetNameStringByIndex(0));
			Authored.To.Key = FName(Enum->GetNameStringByIndex(1));
			Authored.bTwoWay = true;
			Authored.Timing.Duration = 1.0f;
			UEdGraph* Graph = CodeAnimWebGraphs::OpenOrCreateTransitionGraph(Fixture.Defaults(), 0, false);
			UK2Node_FunctionEntry* Entry = EntryOf(Graph);
			UK2Node_VariableSet* SetTint = SetVar(Graph, TEXT("Tint"));
			Link(Test, Then(Entry), Exec(SetTint));
			Link(Test, GetEnd(Graph, false, TEXT("Tint"))->FindPin(UK2Node_GetTransitionOutput::ValuePinName), SetTint->FindPin(TEXT("Tint")));
		}
		{
			UEdGraph* Graph = CodeAnimWebGraphs::OpenOrCreateCustomLerpGraph(BP, TEXT("Scale"), false);
			UK2Node_FunctionEntry* Entry = EntryOf(Graph);
			UK2Node_CallFunction* Multiply = Call(Graph, MathFunction(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Multiply_DoubleDouble)));
			UK2Node_VariableSet* Set = SetVar(Graph, TEXT("Scale"));
			Link(Test, Then(Entry), Exec(Set));
			Link(Test, Entry->FindPin(CodeAnimWeb::AlphaParam), Multiply->FindPin(TEXT("A")));
			Multiply->FindPinChecked(TEXT("B"))->DefaultValue = TEXT("2.0");
			Link(Test, Multiply->GetReturnValuePin(), Set->FindPin(TEXT("Scale")));
		}
		Compile(Test, BP);
	}

	struct FResult
	{
		double SpawnMs = 0.0;
		double AvgFrameMs = 0.0;
		double WorstFrameMs = 0.0;
		double SetStateUs = 0.0;
		int32 SetStates = 0;
	};

	/**
	 * Count Webs, each moved to a new state every Interval seconds (staggered so they do not all change
	 * on the same frame), evaluated for Frames frames. Interval 0 means a new state on every frame.
	 */
	FResult Run(FFixture& Fixture, int32 Count, double Interval)
	{
		TArray<TStrongObjectPtr<UCodeAnimationWeb>> Webs;
		FResult Result;

		const double SpawnStart = FPlatformTime::Seconds();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Webs.Emplace(Fixture.NewWeb());
			Webs.Last()->UpdateAtTime(0.0);
		}
		Result.SpawnMs = (FPlatformTime::Seconds() - SpawnStart) * 1000.0;

		double Total = 0.0;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			const double Now = Frame * FrameTime;
			const double FrameStart = FPlatformTime::Seconds();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				UCodeAnimationWeb* Web = Webs[Index].Get();
				const bool bChange = Interval <= 0.0
					|| FMath::FloorToInt((Now + Index * 0.037) / Interval) != FMath::FloorToInt((Now - FrameTime + Index * 0.037) / Interval);
				if (bChange)
				{
					const double SetStart = FPlatformTime::Seconds();
					Web->SetStateAtTime(static_cast<uint8>((Web->GetState() + 1) % 3), Now);
					Result.SetStateUs += (FPlatformTime::Seconds() - SetStart) * 1000000.0;
					++Result.SetStates;
				}
				Web->UpdateAtTime(Now);
			}
			const double Elapsed = (FPlatformTime::Seconds() - FrameStart) * 1000.0;
			Total += Elapsed;
			Result.WorstFrameMs = FMath::Max(Result.WorstFrameMs, Elapsed);
		}
		Result.AvgFrameMs = Total / Frames;
		Result.SetStateUs = Result.SetStates > 0 ? Result.SetStateUs / Result.SetStates : 0.0;
		return Result;
	}

	void Report(FAutomationTestBase& Test, const TCHAR* Label, int32 Count, const FResult& Result)
	{
		Test.AddInfo(FString::Printf(TEXT("PERF %-24s x%3d  spawn %7.2f ms (%5.1f us/web)  frame avg %6.3f ms (%5.2f us/web)  worst %6.3f ms  SetState %5.2f us x%d"),
			Label, Count, Result.SpawnMs, Result.SpawnMs * 1000.0 / Count,
			Result.AvgFrameMs, Result.AvgFrameMs * 1000.0 / Count, Result.WorstFrameMs, Result.SetStateUs, Result.SetStates));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCodeAnimWebPerfTest, "HVP.CodeAnimWeb.Perf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::StressFilter)

bool FCodeAnimWebPerfTest::RunTest(const FString& Parameters)
{
	using namespace CodeAnimWebPerf;

	FFixture Plain;
	FFixture Graphed;
	AddGraphs(*this, Graphed);

	// A chain 0 - 1 - 2, so going between the ends routes through the middle.
	FFixture Traversal;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FCodeAnimWebTransition& Transition = Traversal.Defaults()->Transitions.AddDefaulted_GetRef();
		Transition.From.Key = FName(Traversal.Enum->GetNameStringByIndex(Index));
		Transition.To.Key = FName(Traversal.Enum->GetNameStringByIndex(Index + 1));
		Transition.bTwoWay = true;
	}
	Traversal.Defaults()->bTransitionTraversalOnly = true;

	for (const int32 Count : { 34, 100, 300 })
	{
		Report(*this, TEXT("plain, transitioning"), Count, Run(Plain, Count, 1.5));
		Report(*this, TEXT("graphs, transitioning"), Count, Run(Graphed, Count, 1.5));
		Report(*this, TEXT("graphs, state every frame"), Count, Run(Graphed, Count, 0.0));
		Report(*this, TEXT("traversal, transitioning"), Count, Run(Traversal, Count, 1.5));
	}
	return true;
}

#endif
