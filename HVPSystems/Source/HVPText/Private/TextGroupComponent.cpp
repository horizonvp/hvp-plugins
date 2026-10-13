#include "TextGroupComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TextRenderSubsystem.h"

UTextRenderSubsystem* UTextGroupComponent::GetTextSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UTextRenderSubsystem>() : nullptr;
}

FName UTextGroupComponent::GetGroupName() const
{
	if (!ActiveName.IsNone())
	{
		return ActiveName;
	}
	if (!GroupName.IsNone())
	{
		return GroupName;
	}
	const AActor* Owner = GetOwner();
	return FName(*FString::Printf(TEXT("%s.%s"), Owner ? *Owner->GetName() : TEXT("None"), *GetName()));
}

void UTextGroupComponent::BeginPlay()
{
	Super::BeginPlay();
	Register();
}

void UTextGroupComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UTextRenderSubsystem* Subsystem = GetTextSubsystem(); Subsystem && !ActiveName.IsNone())
	{
		Subsystem->RemoveGroup(ActiveName);
	}
	ActiveName = NAME_None;
	Super::EndPlay(EndPlayReason);
}

void UTextGroupComponent::Register()
{
	UTextRenderSubsystem* Subsystem = GetTextSubsystem();
	if (!Subsystem)
	{
		return;
	}
	ActiveName = GetGroupName();
	Subsystem->SetGroupStyle(ActiveName, Style);
	Subsystem->SetGroupFrame(ActiveName, this, bOwnInstances);
	if (!Text.IsEmpty())
	{
		SetText(Text);
	}
}

void UTextGroupComponent::SetText(const FString& NewText)
{
	Text = NewText;
	if (UTextRenderSubsystem* Subsystem = GetTextSubsystem(); Subsystem && !ActiveName.IsNone())
	{
		Subsystem->SetGroupText(ActiveName, Text, FVector::ZeroVector, TextScale, Anchor, FRotator::ZeroRotator, Tracking, LineSpacing);
	}
}

void UTextGroupComponent::SetStyle(const FTextGroupStyle& NewStyle)
{
	Style = NewStyle;
	if (UTextRenderSubsystem* Subsystem = GetTextSubsystem(); Subsystem && !ActiveName.IsNone())
	{
		Subsystem->SetGroupStyle(ActiveName, Style);
		if (!Text.IsEmpty())
		{
			SetText(Text);  // a new font lays out differently; letters added by others are left alone
		}
	}
}

void UTextGroupComponent::SetOwnInstances(bool bNewOwnInstances)
{
	bOwnInstances = bNewOwnInstances;
	if (UTextRenderSubsystem* Subsystem = GetTextSubsystem(); Subsystem && !ActiveName.IsNone())
	{
		Subsystem->SetGroupFrame(ActiveName, this, bOwnInstances);
	}
}

void UTextGroupComponent::SetCustomDataVector(int32 DataIndex, FVector Value)
{
	if (UTextRenderSubsystem* Subsystem = GetTextSubsystem(); Subsystem && !ActiveName.IsNone())
	{
		Subsystem->SetGroupCustomDataVector(ActiveName, DataIndex, Value);
	}
}
