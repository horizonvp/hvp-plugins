#include "GameflowBlueprintLibrary.h"
#include "GameflowControllerComponent.h"
#include "GameflowLevelBindingComponent.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"

void UGameflowBlueprintLibrary::GetGameflowController(const UObject* WorldContextObject, UGameflowControllerComponent*& Controller)
{
	Controller = FindOrAddController(WorldContextObject);
}

UGameflowControllerComponent* UGameflowBlueprintLibrary::FindOrAddController(const UObject* WorldContextObject)
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(WorldContextObject, 0);
	if (!IsValid(PlayerController))
	{
		return nullptr;
	}

	if (UGameflowControllerComponent* Existing = PlayerController->FindComponentByClass<UGameflowControllerComponent>())
	{
		return Existing;
	}

	UGameflowControllerComponent* Added = NewObject<UGameflowControllerComponent>(PlayerController);
	Added->RegisterComponent();
	// A second one of these appearing after listeners have bound means those listeners are wired to
	// a controller nobody drives any more. Should only ever be logged once per PlayerController.
	UE_LOG(LogHVPGameflow, Log, TEXT("Created a NEW gameflow controller on %s (requested by %s)"),
		*GetFullNameSafe(PlayerController), *GetFullNameSafe(WorldContextObject));
	return Added;
}

void UGameflowBlueprintLibrary::GetGameflowLevelBinding(const UObject* WorldContextObject, UGameflowLevelBindingComponent*& LevelBinding)
{
	LevelBinding = FindOrAddLevelBinding(WorldContextObject);
}

TArray<UGameflowLevelBindingComponent*> UGameflowBlueprintLibrary::GetAllGameflowLevelBindings(const UObject* WorldContextObject)
{
	TArray<UGameflowLevelBindingComponent*> Found;

	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return Found;
	}
	// GetComponents resets the array it is handed, so accumulate through a scratch one.
	TArray<UGameflowLevelBindingComponent*> OnThisActor;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		It->GetComponents<UGameflowLevelBindingComponent>(OnThisActor, /*bIncludeFromChildActors=*/true);
		Found.Append(OnThisActor);
	}
	return Found;
}

UGameflowLevelBindingComponent* UGameflowBlueprintLibrary::FindOrAddLevelBinding(const UObject* WorldContextObject)
{
	const TArray<UGameflowLevelBindingComponent*> Existing = GetAllGameflowLevelBindings(WorldContextObject);
	if (Existing.Num() > 1)
	{
		UE_LOG(LogHVPGameflow, Warning,
			TEXT("GetGameflowLevelBinding: %d level-binding components in the world; returning the one on '%s'"),
			Existing.Num(), *GetNameSafe(Existing[0]->GetOwner()));
	}
	if (Existing.Num() > 0)
	{
		return Existing[0];
	}

	// Nothing placed: the world settings actor is the one guaranteed-present, never-streamed-out
	// home for a persistent-level component.
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	AWorldSettings* WorldSettings = World ? World->GetWorldSettings() : nullptr;
	if (!IsValid(WorldSettings))
	{
		return nullptr;
	}

	UGameflowLevelBindingComponent* Added = NewObject<UGameflowLevelBindingComponent>(WorldSettings);
	Added->RegisterComponent();
	UE_LOG(LogHVPGameflow, Log, TEXT("GetGameflowLevelBinding: none placed in the world; added one to %s"),
		*WorldSettings->GetName());
	return Added;
}

bool UGameflowBlueprintLibrary::IsStateWithin(const FString& State, const FString& Parent)
{
	return UGameflowControllerComponent::IsStateWithin(State, Parent);
}

FString UGameflowBlueprintLibrary::GetParentState(const FString& State)
{
	int32 LastSlash;
	if (State.FindLastChar(TEXT('/'), LastSlash))
	{
		return State.Left(LastSlash);
	}
	return FString();
}

bool UGameflowBlueprintLibrary::IsControllerInState(const UObject* WorldContextObject, const FString& Query)
{
	const UGameflowControllerComponent* Controller = FindOrAddController(WorldContextObject);
	return Controller && UGameflowControllerComponent::IsStateWithin(Controller->CurrentState, Query);
}
