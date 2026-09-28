#include "StereoButtonBase.h"

#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "IStereoLayers.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "StereoButtonLayerComponent.h"
#include "StereoButtonWidgetInterface.h"
#include "StereoRendering.h"

DEFINE_LOG_CATEGORY_STATIC(LogStereoButton, Log, All);

namespace
{
	const FName LabelTextWidgetName(TEXT("LabelText"));
	const FName FaceImageWidgetName(TEXT("FaceImage"));
	const FName ParamViewOffset(TEXT("ViewOffset"));
	const FName ParamPressAmount(TEXT("PressAmount"));

	bool StereoLayersAvailable()
	{
		return GEngine
			&& GEngine->StereoRenderingDevice.IsValid()
			&& GEngine->StereoRenderingDevice->IsStereoEnabled()
			&& GEngine->StereoRenderingDevice->GetStereoLayers() != nullptr;
	}
}

AStereoButtonBase::AStereoButtonBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	FloatRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FloatRoot"));
	FloatRoot->SetupAttachment(Root);

	WidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("WidgetComp"));
	WidgetComp->SetupAttachment(FloatRoot);
	WidgetComp->SetWidgetSpace(EWidgetSpace::World);
	WidgetComp->SetDrawAtDesiredSize(false);
	WidgetComp->SetPivot(FVector2D(0.5f, 0.5f));
	WidgetComp->SetTwoSided(false);
	WidgetComp->SetBackgroundColor(FLinearColor::Transparent);
	// The plane must keep drawing to its render target even when the (invisible) quad is culled,
	// or the stereo layer freezes on the last frame it saw.
	WidgetComp->SetTickWhenOffscreen(true);
	// This button does its own pressing; hand pointers must not also click the widget.
	WidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WidgetComp->SetGenerateOverlapEvents(false);
	WidgetComp->CastShadow = false;

	StereoLayer = CreateDefaultSubobject<UStereoButtonLayerComponent>(TEXT("StereoLayer"));
	StereoLayer->SetupAttachment(FloatRoot);

	PressVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("PressVolume"));
	PressVolume->SetupAttachment(Root);
	PressVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PressVolume->SetCollisionResponseToAllChannels(ECR_Overlap);
	PressVolume->SetGenerateOverlapEvents(true);
	PressVolume->SetVisibility(false);
	PressVolume->SetHiddenInGame(true);
}

// --- Layout -------------------------------------------------------------------------------

FVector2D AStereoButtonBase::GetFaceSizeCm() const
{
	const float Ppc = FMath::Max(PixelsPerCm, 1.f);
	return FVector2D(FaceSizePx.X / Ppc, FaceSizePx.Y / Ppc);
}

void AStereoButtonBase::ApplyLayout()
{
	const FVector2D FaceCm = GetFaceSizeCm();
	const float Ppc = FMath::Max(PixelsPerCm, 1.f);

	if (WidgetComp)
	{
		if (WidgetComp->GetWidgetClass() != FaceWidgetClass)
		{
			WidgetComp->SetWidgetClass(FaceWidgetClass);
		}
		const float Super = FMath::Clamp(FaceSupersample, 1.f, 4.f);
		// More texels, same centimetres: the draw size grows by the supersample and the plane
		// shrinks by it, so the face keeps its physical width at a higher resolution.
		//
		// SetDrawSize is BOTH the render target size and the widget's layout box, so the widget is
		// laid out at FaceSizePx * FaceSupersample and must be AUTHORED for that box - a supersample
		// of 2 doubles the space the widget lays out in. An earlier version tried to keep the layout
		// at FaceSizePx by render-scaling the widget up to fill the bigger target; that scaled
		// content which already filled the target, so only its top-left quarter survived the clip.
		WidgetComp->SetDrawSize(FVector2D(FaceSizePx) * Super);
		WidgetComp->SetRelativeScale3D(FVector(1.f / (Ppc * Super)));
		WidgetComp->SetRelativeLocation(FVector::ZeroVector);
		WidgetComp->SetRelativeRotation(FRotator::ZeroRotator);
	}

	if (StereoLayer)
	{
		StereoLayer->EnsureWorldLocked();
		StereoLayer->SetQuadSize(FaceCm);
		StereoLayer->SetPriority(LayerPriority);
		StereoLayer->SetRelativeLocation(FVector::ZeroVector);
		// The compositor quad faces the OPPOSITE way to a UWidgetComponent plane: with an identity
		// rotation it showed on the headset facing away from the player, readable only from behind
		// (mirrored). A yaw flip puts its front on +X with the widget plane; up stays up. The
		// project's callout popup carries the same (0,180,0) on its stereo quad for the same reason.
		StereoLayer->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
		StereoLayer->SetRelativeScale3D(FVector::OneVector);
	}

	if (PressVolume)
	{
		const float Pad = FMath::Max(PressVolumePaddingCm, 0.f);
		PressVolume->SetRelativeLocation(FVector::ZeroVector);
		PressVolume->SetBoxExtent(FVector(PressDepth + 1.f, FaceCm.X * 0.5f + Pad, FaceCm.Y * 0.5f + Pad), true);
	}

	PushLabel();
	PushColor();
}

void AStereoButtonBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLayout();
}

#if WITH_EDITOR
void AStereoButtonBase::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	ApplyLayout();
}
#endif

void AStereoButtonBase::BeginPlay()
{
	Super::BeginPlay();
	TimeSinceBeginPlay = 0.f;
	PressModel.Reset();
	FloatSpring.Reset();
	ApplyLayout();
	ReconcileSurface();
}

// --- Tick ---------------------------------------------------------------------------------

void AStereoButtonBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TimeSinceBeginPlay += DeltaSeconds;

	UWorld* World = GetWorld();
	const bool bGameWorld = World && World->IsGameWorld();

	if (bGameWorld)
	{
		SamplePressers(ScratchSamples);

		FStereoButtonPressSettings S;
		S.PressThreshold = PressThreshold;
		S.ReleaseThreshold = ReleaseThreshold;
		S.ReturnSpeed = ReturnSpeed;
		S.UnlatchGraceSeconds = UnlatchGraceSeconds;
		S.PressCooldownSeconds = PressCooldownSeconds;
		S.MaxConcurrentPressers = MaxConcurrentPressers;
		S.bRearmOnRelease = ShouldRearmOnRelease();
		S.bMayFire = !bInputLocked && IsEngaged() && !(bBlockPressWhenHidden && IsEffectivelyHidden());

		ScratchEdges.Reset();
		PressModel.Update(ScratchSamples, S, DeltaSeconds, ScratchEdges);
		FireEdges(ScratchEdges);
	}
	else
	{
		ScratchSamples.Reset();
	}

	StepFloat(DeltaSeconds, ScratchSamples);
	ViewOffset = ComputeViewOffset();
	ReconcileSurface();
	PushWidgetState();
	PushFaceMaterialParams();
}

FVector2D AStereoButtonBase::ComputeViewOffset() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !FloatRoot)
	{
		return FVector2D::ZeroVector;
	}
	const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(World, 0);
	if (!Cam)
	{
		return FVector2D::ZeroVector;
	}
	// Viewer in the PLATE's frame (FloatRoot, not Root) so the spring's tilt shows up as parallax.
	const FVector L = FloatRoot->GetComponentTransform().InverseTransformPositionNoScale(Cam->GetCameraLocation());
	if (L.X <= 1.f)
	{
		return FVector2D::ZeroVector;   // behind or on the face: no meaningful angle
	}
	return FVector2D(FMath::Clamp(L.Y / L.X, -2.f, 2.f), FMath::Clamp(L.Z / L.X, -2.f, 2.f));
}

void AStereoButtonBase::PushFaceMaterialParams()
{
	UUserWidget* W = GetFaceWidget();
	if (!W)
	{
		return;
	}
	if (W->Implements<UStereoButtonWidgetInterface>())
	{
		IStereoButtonWidgetInterface::Execute_UpdateButtonView(W, ViewOffset);
	}
	if (UImage* Face = Cast<UImage>(W->GetWidgetFromName(FaceImageWidgetName)))
	{
		// GetDynamicMaterial creates the MID once (only if the brush holds a material) and caches it.
		if (UMaterialInstanceDynamic* MID = Face->GetDynamicMaterial())
		{
			MID->SetVectorParameterValue(ParamViewOffset, FLinearColor(ViewOffset.X, ViewOffset.Y, 0.f, 0.f));
			MID->SetScalarParameterValue(ParamPressAmount, PressModel.GetPressedAmount());
		}
	}
}

// --- Pressers -----------------------------------------------------------------------------

void AStereoButtonBase::SamplePressers(TArray<FStereoButtonPresserSample>& OutSamples)
{
	// Presence only — who is touching the face and how deep. The latch decides what that means.
	OutSamples.Reset();
	if (PressDepth <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	// A button hidden by scaling it to nothing (MenuCard_BP does this until its intro plays) must
	// not be pressable. InverseTransformPosition of a zero-scale transform collapses every world
	// point onto the origin, which would read as a fingertip dead centre on the face.
	if (GetActorScale3D().GetAbsMin() <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FTransform& T = GetActorTransform();
	const FVector2D Half = GetFaceSizeCm() * 0.5f + FVector2D(FMath::Max(PressVolumePaddingCm, 0.f));
	const float Exit = FMath::Max(ReArmClearanceCm, 0.f);
	const float Pad = FMath::Max(FingertipRadius, 0.f);

	// --- Fingertips: the primary presser. When any hand is tracked the fingertip is the ONLY
	// presser; the grab sphere is consulted only when no tip could be read. Keeping both live let
	// the palm sweep through on withdrawal and fire a second press.
	bool bUsedFingertips = false;
	if (bPressWithFingertips)
	{
		HandTracker.Gather(GetWorld(), FingertipMeshClassFilter, FingertipMergeDistanceCm, ScratchTips);
		if (FStereoButtonHandTracker::AnyValid(ScratchTips))
		{
			bUsedFingertips = true;
			for (int32 TipIndex = 0; TipIndex < ScratchTips.Num(); ++TipIndex)
			{
				const FVector& Tip = ScratchTips[TipIndex];
				if (!FStereoButtonHandTracker::IsValidTip(Tip))
				{
					continue;   // slot held open for an untracked hand: skipping keeps TipIndex stable
				}
				const FVector L = T.InverseTransformPosition(Tip);
				// The soft pad leads the tracked bone by ~FingertipRadius toward the face.
				const float Penetration = Pad - L.X;

				// RETAIN region: bounded at the FRONT and the sides, open at the back. Once at or
				// behind the face you are inside the button however deep you go, and you leave only
				// by coming back out the front past the exit margin.
				const bool bRetain = FMath::Abs(L.Y) <= Half.X + Exit && FMath::Abs(L.Z) <= Half.Y + Exit
					&& Penetration > -Exit;
				if (!bRetain)
				{
					continue;
				}

				FStereoButtonPresserSample& S = OutSamples.AddDefaulted_GetRef();
				S.Source = EStereoButtonPresserSource::Fingertip;
				S.FingertipIndex = TipIndex;
				S.PenetrationCm = Penetration;
				S.Amount = FMath::Clamp(Penetration / PressDepth, 0.f, 1.f);
				S.ContactLocal = FVector2D(L.Y, L.Z);
				// ACQUIRE region: squarely within the face and actually at/behind it. The band
				// between acquire and retain is the hysteresis that stops a withdrawal re-pressing.
				S.bCanAcquire = FMath::Abs(L.Y) <= Half.X && FMath::Abs(L.Z) <= Half.Y && Penetration > 0.f;
			}
		}
	}

	// --- Grab sphere: fallback presser, only when no fingertip was available.
	if (!bUsedFingertips && PressVolume)
	{
		TArray<UPrimitiveComponent*> Overlapping;
		PressVolume->GetOverlappingComponents(Overlapping);

		const FVector FaceCenterWS = T.GetLocation();
		AActor* const MyOwner = GetOwner();
		const FString Filter = PresserComponentFilter.IsNone() ? FString() : PresserComponentFilter.ToString();

		for (UPrimitiveComponent* Comp : Overlapping)
		{
			AActor* const A = Comp ? Comp->GetOwner() : nullptr;
			if (!A || A == this || A == MyOwner || A->IsA<AStereoButtonBase>())
			{
				continue;   // a spawner isn't a finger, and packed buttons must not poke each other
			}
			if (!Filter.IsEmpty()
				&& !Comp->GetName().Contains(Filter, ESearchCase::IgnoreCase)
				&& !Comp->ComponentHasTag(PresserComponentFilter))
			{
				continue;
			}

			// Contact = the presser's leading edge: closest point on its collision to the face centre.
			FVector Contact = Comp->GetComponentLocation();
			const float Dist = Comp->GetClosestPointOnCollision(FaceCenterWS, Contact);
			if (Dist < 0.f)
			{
				Contact = Comp->GetComponentLocation();
			}
			const FVector L = T.InverseTransformPosition(Contact);
			const float Penetration = (Dist == 0.f) ? PressDepth : -L.X;

			FStereoButtonPresserSample& S = OutSamples.AddDefaulted_GetRef();
			S.Source = EStereoButtonPresserSource::Component;
			S.Comp = Comp;
			S.PenetrationCm = Penetration;
			S.Amount = FMath::Clamp(Penetration / PressDepth, 0.f, 1.f);
			S.ContactLocal = FVector2D(L.Y, L.Z);
			// Engine overlap already has the property the fingertip test had to be given by hand:
			// a presser buried deep in the volume still overlaps it.
			S.bCanAcquire = true;
		}
	}
}

void AStereoButtonBase::FireEdges(const TArray<FStereoButtonPressEdge>& Edges)
{
	for (const FStereoButtonPressEdge& E : Edges)
	{
		if (E.bPressed)
		{
			OnButtonPressed.Broadcast(ButtonIndex, this);
			PlayOneShot(PressSound);
			LockExclusiveGroupPeers();
		}
		else
		{
			OnButtonReleased.Broadcast(ButtonIndex, this);
			PlayOneShot(ReleaseSound);
		}
		if (UUserWidget* W = GetFaceWidget())
		{
			if (W->Implements<UStereoButtonWidgetInterface>())
			{
				IStereoButtonWidgetInterface::Execute_OnButtonEdge(W, E.bPressed);
			}
		}
	}
}

// --- Float --------------------------------------------------------------------------------

void AStereoButtonBase::StepFloat(float DeltaSeconds, const TArray<FStereoButtonPresserSample>& Samples)
{
	if (!FloatRoot)
	{
		return;
	}
	if (!bFloatEnabled)
	{
		FloatRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		return;
	}

	// Every presser near the face pushes the plate, latched or not — a light graze on one side
	// tilts it without ever registering as a press.
	TArray<FStereoButtonContact, TInlineAllocator<4>> Contacts;
	for (const FStereoButtonPresserSample& S : Samples)
	{
		FStereoButtonContact& C = Contacts.AddDefaulted_GetRef();
		C.Local = S.ContactLocal;
		C.PadX = -S.PenetrationCm;
	}

	FloatSpring.Step(FloatSettings, DeltaSeconds, Contacts);
	FloatRoot->SetRelativeLocationAndRotation(FVector(FloatSpring.GetOffsetX(), 0.f, 0.f), FloatSpring.ToRotator());
}

// --- Surface ------------------------------------------------------------------------------

bool AStereoButtonBase::WantsStereoLayer() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return false;   // the compositor never draws into an editor viewport
	}
	switch (Surface)
	{
	case EStereoButtonSurface::StereoLayer: return true;
	case EStereoButtonSurface::WidgetPlane: return false;
	default: return StereoLayersAvailable();
	}
}

void AStereoButtonBase::ReconcileSurface()
{
	if (!WidgetComp || !StereoLayer)
	{
		return;
	}

	const bool bWantLayer = WantsStereoLayer();
	if (bWantLayer != bStereoLayerLive)
	{
		bStereoLayerLive = bWantLayer;
		StereoLayer->SetVisibility(bWantLayer);
		if (bWantLayer)
		{
			if (HiddenPlaneMaterial)
			{
				WidgetComp->SetMaterial(0, HiddenPlaneMaterial);
			}
			else if (!bWarnedNoHiddenMaterial)
			{
				bWarnedNoHiddenMaterial = true;
				UE_LOG(LogStereoButton, Warning,
					TEXT("[%s] HiddenPlaneMaterial is not set: the widget plane will show behind the stereo layer."), *GetName());
			}
		}
		else
		{
			WidgetComp->SetMaterial(0, nullptr);   // back to the component's own widget material
		}
		UE_LOG(LogStereoButton, Log, TEXT("[%s] face surface -> %s"), *GetName(), bWantLayer ? TEXT("stereo layer") : TEXT("widget plane"));
	}

	if (!bStereoLayerLive)
	{
		return;
	}

	// The widget's render target does not exist until the widget has painted once, so this is a
	// reconcile rather than a one-off push: compare, and touch the layer only when it differs.
	UTexture* RT = WidgetComp->GetRenderTarget();
	if (LayerTexture.Get() != RT)
	{
		LayerTexture = RT;
		StereoLayer->SetTexture(RT);
	}
	const FVector2D FaceCm = GetFaceSizeCm();
	if (!StereoLayer->GetQuadSize().Equals(FaceCm))
	{
		StereoLayer->SetQuadSize(FaceCm);
	}
}

// --- Widget -------------------------------------------------------------------------------

UUserWidget* AStereoButtonBase::GetFaceWidget() const
{
	return WidgetComp ? WidgetComp->GetWidget() : nullptr;
}

void AStereoButtonBase::PushLabel()
{
	UUserWidget* W = GetFaceWidget();
	if (!W)
	{
		return;
	}
	if (W->Implements<UStereoButtonWidgetInterface>())
	{
		IStereoButtonWidgetInterface::Execute_SetButtonLabel(W, LabelText);
	}
	if (UTextBlock* Text = Cast<UTextBlock>(W->GetWidgetFromName(LabelTextWidgetName)))
	{
		Text->SetText(LabelText);
	}
}

void AStereoButtonBase::PushColor()
{
	UUserWidget* W = GetFaceWidget();
	if (W && W->Implements<UStereoButtonWidgetInterface>())
	{
		IStereoButtonWidgetInterface::Execute_SetButtonColor(W, LabelColor);
	}
}

void AStereoButtonBase::PushWidgetState()
{
	UUserWidget* W = GetFaceWidget();
	if (W && W->Implements<UStereoButtonWidgetInterface>())
	{
		IStereoButtonWidgetInterface::Execute_UpdateButtonState(W,
			PressModel.GetPressedAmount(), IsInContact(), PressModel.IsPressed(), PressModel.GetTimeSinceInteraction());
	}
}

void AStereoButtonBase::SetLabelText(const FText& NewText)
{
	LabelText = NewText;
	PushLabel();
}

void AStereoButtonBase::SetLabelColor(const FLinearColor& NewColor)
{
	LabelColor = NewColor;
	PushColor();
}

// --- Control ------------------------------------------------------------------------------

bool AStereoButtonBase::IsEngaged() const
{
	const UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return true;
	}
	return TimeSinceBeginPlay >= EngageDelaySeconds;
}

void AStereoButtonBase::ResetButton()
{
	PressModel.Reset();
	FloatSpring.Reset();
	bInputLocked = false;
	if (FloatRoot)
	{
		FloatRoot->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	}
}

void AStereoButtonBase::TriggerButton()
{
	if (bInputLocked || !IsEngaged() || (bBlockPressWhenHidden && IsEffectivelyHidden()))
	{
		return;
	}
	PressModel.Reset();
	PressModel.StartCooldown(PressCooldownSeconds);
	FloatSpring.Impulse(FloatSettings, FVector2D::ZeroVector, 60.f);
	TArray<FStereoButtonPressEdge> Edges;
	Edges.Add({ true, INDEX_NONE });
	Edges.Add({ false, INDEX_NONE });
	FireEdges(Edges);
}

void AStereoButtonBase::DebugFirePress()
{
	FloatSpring.Impulse(FloatSettings, FVector2D(GetFaceSizeCm().X * -0.35f, 0.f), 80.f);
	TArray<FStereoButtonPressEdge> Edges;
	Edges.Add({ true, INDEX_NONE });
	Edges.Add({ false, INDEX_NONE });
	FireEdges(Edges);
}

void AStereoButtonBase::PlayOneShot(USoundBase* Cue) const
{
	if (Cue && GetWorld() && GetWorld()->IsGameWorld())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Cue, GetActorLocation());
	}
}

void AStereoButtonBase::LockExclusiveGroupPeers()
{
	if (ExclusiveGroup.IsNone())
	{
		return;
	}
	for (TActorIterator<AStereoButtonBase> It(GetWorld()); It; ++It)
	{
		if (It->ExclusiveGroup == ExclusiveGroup)
		{
			It->SetInputLocked(true);
		}
	}
}

void AStereoButtonBase::UnlockExclusiveGroup()
{
	SetInputLocked(false);
	if (ExclusiveGroup.IsNone())
	{
		return;
	}
	for (TActorIterator<AStereoButtonBase> It(GetWorld()); It; ++It)
	{
		if (It->ExclusiveGroup == ExclusiveGroup)
		{
			It->SetInputLocked(false);
		}
	}
}

bool AStereoButtonBase::IsEffectivelyHidden() const
{
	for (const AActor* A = this; A; A = A->GetAttachParentActor())
	{
		if (A->IsHidden())
		{
			return true;
		}
		if (const USceneComponent* R = A->GetRootComponent(); R && !R->IsVisible())
		{
			return true;
		}
	}
	return false;
}
