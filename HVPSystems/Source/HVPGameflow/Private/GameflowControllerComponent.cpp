#include "GameflowControllerComponent.h"
#include "ExitAuthorityList.h"
#include "GameflowQueueRules.h"

DEFINE_LOG_CATEGORY(LogHVPGameflow);

UGameflowControllerComponent::UGameflowControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UGameflowControllerComponent::RequestStateChange(const FString& InRequestedState)
{
	// Copy first: AdvanceQueue below mutates TransitionQueue, and a caller may have passed a
	// reference into it.
	const FString NewState = InRequestedState;

	UE_LOG(LogHVPGameflow, Log, TEXT("Request State Change: %s"), *NewState);

	if (!RequestedState.IsEmpty() || !PendingDemandState.IsEmpty())
	{
		UE_LOG(LogHVPGameflow, Warning,
			TEXT("Request for '%s' ignored: transition to '%s' already pending"),
			*NewState, RequestedState.IsEmpty() ? *PendingDemandState : *RequestedState);
		return;
	}
	if (NewState.Equals(CurrentState, ESearchCase::IgnoreCase))
	{
		return;
	}

	RequestedState = NewState;
	StateRequestTime = FDateTime::UtcNow();
	EntryRequirements.Empty();
	AdvanceQueue(NewState, /*bClearIfUnqueued=*/false);

	// Listeners register exit authorities and entry requirements synchronously during this broadcast.
	OnStateChangeRequested.Broadcast(CurrentState, NewState);

	if (!IsExitPending())
	{
		UE_LOG(LogHVPGameflow, Log, TEXT("No authorities for %s"), *CurrentState);
		ApproveStateTransition();
	}
}

void UGameflowControllerComponent::DemandStateChange(const FString& DemandedState)
{
	// Copy first: AdvanceQueue below mutates TransitionQueue, and a caller may have passed a
	// reference into it.
	const FString NewState = DemandedState;

	UE_LOG(LogHVPGameflow, Log, TEXT("Demand State Change: %s"), *NewState);

	if (NewState.Equals(CurrentState, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogHVPGameflow, Log,
			TEXT("Demand for '%s' ignored: already the current state"), *NewState);
		return;
	}

	// What this demand is cutting short, captured before the claim below overwrites it.
	const FString InterruptedTarget =
		!RequestedState.IsEmpty() ? RequestedState
		: !PendingDemandState.IsEmpty() ? PendingDemandState
		: NewState;

	// Claim the transition BEFORE broadcasting anything. A listener that reacts to the interrupt by
	// calling RequestStateChange — an ending animation driving "continue to next state" is the usual
	// one — would otherwise find both pending fields empty, sail past the already-pending guard in
	// RequestStateChange, and run an entire extra transition to completion inside this broadcast.
	// The demand would then flip from whatever state that left behind, so a demand of A -> C lands
	// as A -> B -> C with a spurious B nobody asked for. With the claim in place the re-entrant
	// request is dropped with a log and the demand stays the only transition in flight.
	PendingDemandState = NewState;
	StateRequestTime = FDateTime::UtcNow();

	// A demand always interrupts whatever is playing, whether or not a transition was pending —
	// this is what lets the skip button cut a running enter-side animation, not just exits.
	OnStateChangeInterrupted.Broadcast(CurrentState, InterruptedTarget);

	ClearExits();
	RequestedState.Empty();
	EntryRequirements.Empty();

	// Entry gates (level loads) register here. Exits were interrupted above and stay skipped —
	// a demand defers only for the target's entry requirements, never for exit animations.
	OnStateChangeDemanded.Broadcast(CurrentState, NewState);

	if (EntryRequirements.IsEmpty())
	{
		PendingDemandState.Empty();
		AdvanceQueue(NewState, /*bClearIfUnqueued=*/true);
		ChangeState(NewState);
	}
	else
	{
		UE_LOG(LogHVPGameflow, Log, TEXT("Demand for '%s' gated on %d entry requirement(s)"),
			*NewState, EntryRequirements.Num());
	}
}

void UGameflowControllerComponent::StartTransitionQueue(const TArray<FString>& Queue)
{
	TransitionQueue = Queue;
	for (const FString& Warning : GameflowQueueRules::Validate(TransitionQueue))
	{
		UE_LOG(LogHVPGameflow, Warning, TEXT("Transition queue: %s"), *Warning);
	}
	ContinueToNextState();
}

void UGameflowControllerComponent::ContinueToNextState()
{
	// Resolve, don't pop: if the request is dropped (another transition pending), the queue is
	// untouched and the next Continue resolves the same answer. The queue advances in
	// AdvanceQueue once the request is actually accepted.
	const FString Next = GetNextQueuedState();
	if (Next.IsEmpty())
	{
		UE_LOG(LogHVPGameflow, Log, TEXT("Continue to Next State: transition queue empty"));
		return;
	}
	RequestStateChange(Next);
}

void UGameflowControllerComponent::SnapToNextState()
{
	const FString Next = GetNextQueuedState();
	if (Next.IsEmpty())
	{
		UE_LOG(LogHVPGameflow, Log, TEXT("Snap to Next State: transition queue empty"));
		return;
	}
	DemandStateChange(Next);
}

FString UGameflowControllerComponent::GetNextQueuedState() const
{
	return GameflowQueueRules::NextState(TransitionQueue, CurrentState);
}

TArray<FString> UGameflowControllerComponent::GetRemainingBranches() const
{
	return GameflowQueueRules::RemainingBranches(TransitionQueue);
}

void UGameflowControllerComponent::RegisterExitAuthority(FName Authority)
{
	UExitAuthorityList* List = FindOrAddAuthorityList(CurrentState);
	List->AddAuthority(Authority);
	UE_LOG(LogHVPGameflow, Log, TEXT("Adding authority %s, %s now with %d"),
		*Authority.ToString(), *CurrentState, List->Authorities.Num());
}

void UGameflowControllerComponent::ClearExitAuthority(FName Authority)
{
	UE_LOG(LogHVPGameflow, Log, TEXT("Clearing authority: %s"), *Authority.ToString());

	UExitAuthorityList* List = FindAuthorityList(CurrentState);
	if (!List)
	{
		return;
	}
	List->RemoveAuthority(Authority);

	if (List->IsEmpty() && !RequestedState.IsEmpty())
	{
		UE_LOG(LogHVPGameflow, Log, TEXT("Finished requests for exit authority %s"), *CurrentState);
		ApproveStateTransition();
	}
}

void UGameflowControllerComponent::RegisterEntryRequirement(FName Requirement)
{
	if (RequestedState.IsEmpty() && PendingDemandState.IsEmpty())
	{
		UE_LOG(LogHVPGameflow, Warning,
			TEXT("Entry requirement '%s' ignored: no transition pending"), *Requirement.ToString());
		return;
	}
	EntryRequirements.Add(Requirement);
	UE_LOG(LogHVPGameflow, Log, TEXT("Adding entry requirement %s for '%s' (now %d)"),
		*Requirement.ToString(),
		RequestedState.IsEmpty() ? *PendingDemandState : *RequestedState,
		EntryRequirements.Num());
}

void UGameflowControllerComponent::ClearEntryRequirement(FName Requirement)
{
	if (EntryRequirements.Remove(Requirement) == 0)
	{
		return;
	}
	UE_LOG(LogHVPGameflow, Log, TEXT("Clearing entry requirement: %s (%d left)"),
		*Requirement.ToString(), EntryRequirements.Num());
	if (EntryRequirements.IsEmpty())
	{
		TryFinishPendingTransition();
	}
}

bool UGameflowControllerComponent::IsEntryPending() const
{
	return !EntryRequirements.IsEmpty();
}

bool UGameflowControllerComponent::IsStateWithin(const FString& State, const FString& Parent)
{
	if (State.IsEmpty() || Parent.IsEmpty())
	{
		return false;
	}
	if (State.Equals(Parent, ESearchCase::IgnoreCase))
	{
		return true;
	}
	return State.Len() > Parent.Len()
		&& State[Parent.Len()] == TEXT('/')
		&& State.StartsWith(Parent, ESearchCase::IgnoreCase);
}

void UGameflowControllerComponent::TryFinishPendingTransition()
{
	if (!EntryRequirements.IsEmpty())
	{
		return;
	}
	if (!PendingDemandState.IsEmpty())
	{
		// Same demand, just resumed after its entry requirements cleared.
		const FString NewState = PendingDemandState;
		PendingDemandState.Empty();
		AdvanceQueue(NewState, /*bClearIfUnqueued=*/true);
		ChangeState(NewState);
	}
	else if (!RequestedState.IsEmpty() && !IsExitPending())
	{
		ApproveStateTransition();
	}
}

void UGameflowControllerComponent::ClearExits()
{
	ExitAuthorities.Empty();
}

bool UGameflowControllerComponent::IsExitPending() const
{
	const UExitAuthorityList* List = FindAuthorityList(CurrentState);
	return List && !List->IsEmpty();
}

void UGameflowControllerComponent::ApproveStateTransition()
{
	if (RequestedState.IsEmpty())
	{
		return;
	}
	if (!EntryRequirements.IsEmpty())
	{
		// Exits are done (or being forced), but the target isn't ready — the transition completes
		// from ClearEntryRequirement when the last requirement releases.
		UE_LOG(LogHVPGameflow, Log, TEXT("Transition to '%s' approved on the exit side; waiting on %d entry requirement(s)"),
			*RequestedState, EntryRequirements.Num());
		ExitAuthorities.Remove(CurrentState.ToLower());
		return;
	}
	UE_LOG(LogHVPGameflow, Log, TEXT("Approving transition: %s"), *RequestedState);

	ExitAuthorities.Remove(CurrentState.ToLower());
	const FString NewState = RequestedState;
	RequestedState.Empty();
	ChangeState(NewState);
}

void UGameflowControllerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!RequestedState.IsEmpty() && IsExitPending())
	{
		const FTimespan Waited = FDateTime::UtcNow() - StateRequestTime;
		if (Waited.GetTotalSeconds() > StateChangeMaxWaitSeconds)
		{
			UE_LOG(LogHVPGameflow, Warning, TEXT("Timeout waiting for state exit permission (%s -> %s)"),
				*CurrentState, *RequestedState);
			ClearExits();
			ApproveStateTransition();
		}
	}

	// Entry requirements are never forced through — a state must not be entered before its level
	// is ready. A stall past the exit timeout is a bug in whoever registered the requirement, so
	// make it loud.
	if (!EntryRequirements.IsEmpty())
	{
		const FDateTime Now = FDateTime::UtcNow();
		if ((Now - StateRequestTime).GetTotalSeconds() > FMath::Max(StateChangeMaxWaitSeconds, 5.0f)
			&& (Now - LastEntryStallWarning).GetTotalSeconds() > 5.0)
		{
			LastEntryStallWarning = Now;
			for (const FName Requirement : EntryRequirements)
			{
				UE_LOG(LogHVPGameflow, Warning, TEXT("Still waiting on entry requirement '%s' for '%s' (%.0fs)"),
					*Requirement.ToString(),
					RequestedState.IsEmpty() ? *PendingDemandState : *RequestedState,
					(Now - StateRequestTime).GetTotalSeconds());
			}
		}
	}
}

void UGameflowControllerComponent::ChangeState(const FString& NewState)
{
	PreviousState = CurrentState;
	CurrentState = NewState;
	UE_LOG(LogHVPGameflow, Log, TEXT("STATE CHANGE EXECUTED: %s -> %s"), *PreviousState, *CurrentState);
	OnStateChange.Broadcast(PreviousState, CurrentState);
}

void UGameflowControllerComponent::AdvanceQueue(const FString& State, bool bClearIfUnqueued)
{
	if (TransitionQueue.IsEmpty())
	{
		return;
	}
	const int32 Before = TransitionQueue.Num();
	GameflowQueueRules::Advance(TransitionQueue, State, bClearIfUnqueued);

	if (TransitionQueue.IsEmpty() && Before > 1)
	{
		UE_LOG(LogHVPGameflow, Log,
			TEXT("Queue advanced to '%s' — %d remaining state(s) dropped"), *State, Before);
	}
	else if (const TArray<FString> Branches = GameflowQueueRules::RemainingBranches(TransitionQueue); !Branches.IsEmpty())
	{
		UE_LOG(LogHVPGameflow, Log, TEXT("Queue advanced to '%s' — %d branch(es) of '%s' remain: %s"),
			*State, Branches.Num(), *GameflowQueueRules::FrontBlock(TransitionQueue).Hub,
			*FString::Join(Branches, TEXT(", ")));
	}
	else if (Before - TransitionQueue.Num() > 1)
	{
		UE_LOG(LogHVPGameflow, Log, TEXT("Queue advanced to '%s', dropping %d skipped state(s)"),
			*State, Before - TransitionQueue.Num() - 1);
	}
}

UExitAuthorityList* UGameflowControllerComponent::FindOrAddAuthorityList(const FString& State)
{
	const FString Key = State.ToLower();
	if (TObjectPtr<UExitAuthorityList>* Found = ExitAuthorities.Find(Key))
	{
		return *Found;
	}
	UExitAuthorityList* List = NewObject<UExitAuthorityList>(this);
	ExitAuthorities.Add(Key, List);
	return List;
}

UExitAuthorityList* UGameflowControllerComponent::FindAuthorityList(const FString& State) const
{
	const TObjectPtr<UExitAuthorityList>* Found = ExitAuthorities.Find(State.ToLower());
	return Found ? Found->Get() : nullptr;
}
