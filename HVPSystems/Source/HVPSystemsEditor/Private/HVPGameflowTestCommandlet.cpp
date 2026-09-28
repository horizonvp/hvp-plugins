#include "HVPGameflowTestCommandlet.h"

#include "GameflowBlueprintLibrary.h"
#include "GameflowControllerComponent.h"
#include "GameflowQueueRules.h"

#include <initializer_list>

DEFINE_LOG_CATEGORY_STATIC(LogHVPGameflowTest, Log, All);

namespace
{
	int32 GFailures = 0;

	void Check(bool bCondition, const TCHAR* What)
	{
		if (bCondition)
		{
			UE_LOG(LogHVPGameflowTest, Log, TEXT("  PASS  %s"), What);
		}
		else
		{
			UE_LOG(LogHVPGameflowTest, Error, TEXT("  FAIL  %s"), What);
			++GFailures;
		}
	}
}

void UHVPGameflowTestCommandlet::OnRequested_GateEntry(const FString& OldState, const FString& NewState)
{
	if (!PendingEntryGate.IsNone())
	{
		Controller->RegisterEntryRequirement(PendingEntryGate);
	}
}

void UHVPGameflowTestCommandlet::OnDemanded_GateEntry(const FString& OldState, const FString& NewState)
{
	if (!PendingEntryGate.IsNone())
	{
		Controller->RegisterEntryRequirement(PendingEntryGate);
	}
}

void UHVPGameflowTestCommandlet::OnRequested_HoldExit(const FString& OldState, const FString& NewState)
{
	if (!PendingExitHold.IsNone())
	{
		Controller->RegisterExitAuthority(PendingExitHold);
	}
}

int32 UHVPGameflowTestCommandlet::Main(const FString& Params)
{
	// --- Hierarchy matching ---
	UE_LOG(LogHVPGameflowTest, Display, TEXT("--- IsStateWithin / GetParentState ---"));
	Check(UGameflowControllerComponent::IsStateWithin(TEXT("PMN"), TEXT("PMN")), TEXT("PMN within PMN"));
	Check(UGameflowControllerComponent::IsStateWithin(TEXT("PMN/Step1"), TEXT("PMN")), TEXT("PMN/Step1 within PMN"));
	Check(UGameflowControllerComponent::IsStateWithin(TEXT("PMN/Step1/Part2"), TEXT("PMN")), TEXT("PMN/Step1/Part2 within PMN"));
	Check(UGameflowControllerComponent::IsStateWithin(TEXT("pmn/step1"), TEXT("PMN")), TEXT("case-insensitive match"));
	Check(!UGameflowControllerComponent::IsStateWithin(TEXT("PMNFoo"), TEXT("PMN")), TEXT("PMNFoo NOT within PMN (boundary)"));
	Check(!UGameflowControllerComponent::IsStateWithin(TEXT("PMN"), TEXT("PMN/Step1")), TEXT("parent NOT within child"));
	Check(!UGameflowControllerComponent::IsStateWithin(TEXT(""), TEXT("PMN")), TEXT("empty state matches nothing"));
	Check(!UGameflowControllerComponent::IsStateWithin(TEXT("PMN"), TEXT("")), TEXT("empty parent matches nothing"));
	Check(UGameflowBlueprintLibrary::GetParentState(TEXT("PMN/Step1/Part2")) == TEXT("PMN/Step1"), TEXT("GetParentState pops one segment"));
	Check(UGameflowBlueprintLibrary::GetParentState(TEXT("PMN")).IsEmpty(), TEXT("GetParentState of root is empty"));

	// --- Controller transitions ---
	Controller = NewObject<UGameflowControllerComponent>(GetTransientPackage());
	Controller->OnStateChangeRequested.AddDynamic(this, &UHVPGameflowTestCommandlet::OnRequested_GateEntry);
	Controller->OnStateChangeRequested.AddDynamic(this, &UHVPGameflowTestCommandlet::OnRequested_HoldExit);
	Controller->OnStateChangeDemanded.AddDynamic(this, &UHVPGameflowTestCommandlet::OnDemanded_GateEntry);

	UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Ungated request flips immediately ---"));
	Controller->RequestStateChange(TEXT("Intro"));
	Check(Controller->CurrentState == TEXT("Intro"), TEXT("request with no gates flips synchronously"));

	UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Entry-gated request ---"));
	PendingEntryGate = TEXT("LevelBinding:PMN");
	Controller->RequestStateChange(TEXT("PMN/Step1"));
	Check(Controller->CurrentState == TEXT("Intro"), TEXT("state held while entry requirement pending"));
	Check(Controller->RequestedState == TEXT("PMN/Step1"), TEXT("request stays pending"));
	Check(Controller->IsEntryPending(), TEXT("IsEntryPending true"));
	PendingEntryGate = NAME_None;
	Controller->ClearEntryRequirement(TEXT("LevelBinding:PMN"));
	Check(Controller->CurrentState == TEXT("PMN/Step1"), TEXT("state flips when last requirement clears"));
	Check(!Controller->IsEntryPending(), TEXT("no requirement left behind"));

	UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Substate move with exit authority held elsewhere ---"));
	PendingExitHold = TEXT("Portal");
	Controller->RequestStateChange(TEXT("PMN/Step1/Part2"));
	Check(Controller->CurrentState == TEXT("PMN/Step1"), TEXT("exit authority holds substate move"));
	Controller->ClearExitAuthority(TEXT("Portal"));
	Check(Controller->CurrentState == TEXT("PMN/Step1/Part2"), TEXT("clearing exit authority completes it"));
	PendingExitHold = NAME_None;

	UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Exit + entry combined ---"));
	PendingExitHold = TEXT("Portal");
	PendingEntryGate = TEXT("LevelBinding:AMR");
	Controller->RequestStateChange(TEXT("AMR"));
	Check(Controller->CurrentState == TEXT("PMN/Step1/Part2"), TEXT("held by both gates"));
	PendingExitHold = NAME_None;
	Controller->ClearExitAuthority(TEXT("Portal"));
	Check(Controller->CurrentState == TEXT("PMN/Step1/Part2"), TEXT("exit cleared but entry still holds"));
	PendingEntryGate = NAME_None;
	Controller->ClearEntryRequirement(TEXT("LevelBinding:AMR"));
	Check(Controller->CurrentState == TEXT("AMR"), TEXT("entry cleared last -> flips"));

	UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Entry-gated demand ---"));
	PendingEntryGate = TEXT("LevelBinding:PMN");
	Controller->DemandStateChange(TEXT("PMN"));
	Check(Controller->CurrentState == TEXT("AMR"), TEXT("demand held while its level loads"));
	Check(Controller->RequestedState.IsEmpty(), TEXT("gated demand is not a 'request'"));
	Controller->RequestStateChange(TEXT("Menu"));
	Check(Controller->CurrentState == TEXT("AMR"), TEXT("request during gated demand is ignored"));
	PendingEntryGate = NAME_None;
	Controller->ClearEntryRequirement(TEXT("LevelBinding:PMN"));
	Check(Controller->CurrentState == TEXT("PMN"), TEXT("demand flips when requirement clears"));

	UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Ungated demand still instant ---"));
	Controller->DemandStateChange(TEXT("Menu"));
	Check(Controller->CurrentState == TEXT("Menu"), TEXT("demand with no gates flips synchronously"));

	UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Queue peek/pop with hierarchy ---"));
	TArray<FString> Queue = { TEXT("PMN"), TEXT("PMN/Step1"), TEXT("Scoreboard") };
	Controller->StartTransitionQueue(Queue);
	Check(Controller->CurrentState == TEXT("PMN"), TEXT("queue start requests first entry"));
	Check(Controller->TransitionQueue.Num() == 2, TEXT("accepted entry popped"));
	Controller->ContinueToNextState();
	Check(Controller->CurrentState == TEXT("PMN/Step1"), TEXT("continue into substate"));
	Controller->SnapToNextState();
	Check(Controller->CurrentState == TEXT("Scoreboard"), TEXT("snap to queue front"));
	Check(Controller->TransitionQueue.IsEmpty(), TEXT("queue drained"));

	// --- Queue rules: blocks (pure, no controller) ---
	{
		using namespace GameflowQueueRules;
		auto Q = [](std::initializer_list<const TCHAR*> Entries)
		{
			TArray<FString> Out;
			for (const TCHAR* E : Entries) { Out.Add(E); }
			return Out;
		};
		auto Same = [](const TArray<FString>& A, std::initializer_list<const TCHAR*> B)
		{
			if (A.Num() != static_cast<int32>(B.size())) { return false; }
			int32 i = 0;
			for (const TCHAR* E : B) { if (!A[i++].Equals(E)) { return false; } }
			return true;
		};

		UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Queue rules: Parse ---"));
		Check(Parse(TEXT("A/B/All/C")).Kind == EBlockKind::All && Parse(TEXT("A/B/All/C")).Hub == TEXT("A/B"), TEXT("All branch parses hub"));
		Check(Parse(TEXT("a/b/ANY/c")).Kind == EBlockKind::Any && Parse(TEXT("a/b/ANY/c")).Hub == TEXT("a/b"), TEXT("Any marker case-insensitive"));
		Check(Parse(TEXT("A/B/All/C/Part2")).Hub == TEXT("A/B"), TEXT("branch substate keeps hub"));
		Check(!Parse(TEXT("A/B")).IsBranch(), TEXT("plain state is not a branch"));
		Check(!Parse(TEXT("All/C")).IsBranch(), TEXT("marker first: no hub, not a branch"));
		Check(!Parse(TEXT("A/All")).IsBranch(), TEXT("marker last: no branch name, not a branch"));
		Check(IsInsideBlockOf(TEXT("A/B/All/C"), TEXT("A/B")), TEXT("inside block of its hub"));
		Check(IsInsideBlockOf(TEXT("A/B/All/C"), TEXT("A")), TEXT("inside block beneath an ancestor"));
		Check(!IsInsideBlockOf(TEXT("A/B"), TEXT("A/B")), TEXT("hub itself is not inside its block"));
		Check(!IsInsideBlockOf(TEXT("A/B/Foo"), TEXT("A/B")), TEXT("plain substate is not inside the block"));

		UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Queue rules: All block, out-of-order visits ---"));
		TArray<FString> All = Q({ TEXT("Intro"), TEXT("Hub"), TEXT("Hub/All/X"), TEXT("Hub/All/Y"), TEXT("Hub/All/Z"), TEXT("Over") });
		Check(NextState(All, TEXT("Menu")) == TEXT("Intro"), TEXT("plain front resolves to itself"));
		Advance(All, TEXT("Intro"), false);
		Check(NextState(All, TEXT("Intro")) == TEXT("Hub"), TEXT("explicit hub entry is next"));
		Advance(All, TEXT("Hub"), false);
		Check(Same(All, { TEXT("Hub/All/X"), TEXT("Hub/All/Y"), TEXT("Hub/All/Z"), TEXT("Over") }), TEXT("explicit hub popped, block at front"));
		Check(NextState(All, TEXT("Hub")) == TEXT("Hub/All/X"), TEXT("from the hub: first remaining branch (skip-walk)"));
		Check(Same(RemainingBranches(All), { TEXT("Hub/All/X"), TEXT("Hub/All/Y"), TEXT("Hub/All/Z") }), TEXT("remaining branches listed"));
		Advance(All, TEXT("Hub/All/Y"), true);
		Check(Same(All, { TEXT("Hub/All/X"), TEXT("Hub/All/Z"), TEXT("Over") }), TEXT("demanding Y removes only Y"));
		Check(NextState(All, TEXT("Hub/All/Y")) == TEXT("Hub"), TEXT("from a branch: back to the derived hub"));
		Advance(All, TEXT("Hub"), false);
		Check(All.Num() == 3, TEXT("returning to the hub leaves the block whole"));
		Advance(All, TEXT("Hub/All/X"), false);
		Check(Same(All, { TEXT("Hub/All/Z"), TEXT("Over") }), TEXT("requesting X removes X"));
		Advance(All, TEXT("Hub/All/Z/Part2"), false);
		Check(Same(All, { TEXT("Over") }), TEXT("a branch substate consumes the branch"));
		Check(NextState(All, TEXT("Hub/All/Z/Part2")) == TEXT("Over"), TEXT("last branch done: falls through past the block"));

		UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Queue rules: implicit hub, jumps, revisits ---"));
		TArray<FString> Implicit = Q({ TEXT("Hub/All/X"), TEXT("Hub/All/Y"), TEXT("Over") });
		Check(NextState(Implicit, TEXT("Intro")) == TEXT("Hub"), TEXT("unlisted hub is derived on arrival"));
		Advance(Implicit, TEXT("Hub"), true);
		Check(Implicit.Num() == 3, TEXT("demanding the derived hub does not clear the queue"));
		TArray<FString> Jump = Q({ TEXT("Intro"), TEXT("Hub"), TEXT("Hub/All/X"), TEXT("Hub/All/Y"), TEXT("Over") });
		Advance(Jump, TEXT("Hub/All/Y"), true);
		Check(Same(Jump, { TEXT("Hub/All/X"), TEXT("Over") }), TEXT("jump into a block drops vaulted entries, keeps siblings"));
		TArray<FString> Revisit = Q({ TEXT("Hub/All/X"), TEXT("Hub"), TEXT("Over") });
		Advance(Revisit, TEXT("Hub/All/X"), false);
		Check(NextState(Revisit, TEXT("Hub/All/X")) == TEXT("Hub"), TEXT("hub listed after the block is the closing beat"));
		Advance(Revisit, TEXT("Hub"), false);
		Check(Same(Revisit, { TEXT("Over") }), TEXT("closing-beat hub pops like a plain entry"));
		TArray<FString> Past = Q({ TEXT("Hub/All/X"), TEXT("Hub/All/Y"), TEXT("Over"), TEXT("End") });
		Advance(Past, TEXT("Over"), true);
		Check(Same(Past, { TEXT("End") }), TEXT("jump past the block drops it"));
		TArray<FString> Unqueued = Q({ TEXT("Hub/All/X"), TEXT("Over") });
		Advance(Unqueued, TEXT("Elsewhere"), false);
		Check(Unqueued.Num() == 2, TEXT("unqueued request leaves the queue alone"));
		Advance(Unqueued, TEXT("Elsewhere"), true);
		Check(Unqueued.IsEmpty(), TEXT("unqueued demand clears the queue"));

		UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Queue rules: Any block ---"));
		TArray<FString> Any = Q({ TEXT("Hub/Any/X"), TEXT("Hub/Any/Y"), TEXT("Over") });
		Check(NextState(Any, TEXT("Intro")) == TEXT("Hub"), TEXT("Any: arrive at the hub first"));
		Check(NextState(Any, TEXT("Hub")) == TEXT("Hub/Any/X"), TEXT("Any: skip-walk picks the first branch"));
		Advance(Any, TEXT("Hub/Any/Y"), true);
		Check(Same(Any, { TEXT("Over") }), TEXT("Any: one choice removes the whole block"));
		Check(NextState(Any, TEXT("Hub/Any/Y")) == TEXT("Over"), TEXT("Any: no return to the hub"));

		UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Queue rules: Validate ---"));
		Check(Validate(Q({ TEXT("A"), TEXT("H/All/X"), TEXT("H/All/Y"), TEXT("H"), TEXT("B") })).IsEmpty(), TEXT("well-formed queue has no warnings"));
		Check(Validate(Q({ TEXT("All/X") })).Num() == 1, TEXT("marker first warns"));
		Check(Validate(Q({ TEXT("H/All") })).Num() == 1, TEXT("marker last warns"));
		Check(Validate(Q({ TEXT("H/All/X"), TEXT("B"), TEXT("H/All/Y") })).Num() == 1, TEXT("non-contiguous block warns"));
		Check(Validate(Q({ TEXT("H/All/X/Any/Y") })).Num() == 1, TEXT("nested markers warn"));
	}

	// --- Controller: Continue/Snap route through the resolver ---
	UE_LOG(LogHVPGameflowTest, Display, TEXT("--- Controller walks an All block ---"));
	Controller->DemandStateChange(TEXT("Reset"));
	Controller->StartTransitionQueue({ TEXT("Intro"), TEXT("Hub"), TEXT("Hub/All/X"), TEXT("Hub/All/Y"), TEXT("Over") });
	Check(Controller->CurrentState == TEXT("Intro"), TEXT("queue starts at Intro"));
	Controller->ContinueToNextState();
	Check(Controller->CurrentState == TEXT("Hub"), TEXT("continue enters the hub"));
	Check(Controller->GetRemainingBranches().Num() == 2, TEXT("two branches remain"));
	Controller->RequestStateChange(TEXT("Hub/All/Y"));
	Check(Controller->CurrentState == TEXT("Hub/All/Y"), TEXT("app chose branch Y"));
	Check(Controller->GetNextQueuedState() == TEXT("Hub"), TEXT("next from Y is the hub"));
	Controller->ContinueToNextState();
	Check(Controller->CurrentState == TEXT("Hub"), TEXT("continue returns to the hub"));
	Controller->SnapToNextState();
	Check(Controller->CurrentState == TEXT("Hub/All/X"), TEXT("snap from the hub takes the remaining branch"));
	Controller->ContinueToNextState();
	Check(Controller->CurrentState == TEXT("Over"), TEXT("block complete: continue leaves it"));
	Check(Controller->TransitionQueue.IsEmpty(), TEXT("queue drained"));

	UE_LOG(LogHVPGameflowTest, Display, TEXT("HVPGameflowTest finished: %d failure(s)."), GFailures);
	return GFailures == 0 ? 0 : 1;
}
