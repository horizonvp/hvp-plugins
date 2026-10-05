#include "CodeAnimationWeb.h"

#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#endif
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogCodeAnimWeb, Log, All);

double FCodeAnimWebTransitionTiming::AlphaAt(double Progress) const
{
	Progress = FMath::Clamp(Progress, 0.0, 1.0);
	if (Curve)
	{
		return Curve->GetFloatValue(static_cast<float>(Progress));
	}
	return UKismetMathLibrary::Ease(0.0, 1.0, Progress, Easing, BlendExponent);
}

namespace CodeAnimWebPrivate
{
	/** How a slot lerps. Everything not listed switches at a point in the transition instead. */
	enum class EBlendKind : uint8
	{
		Snap, Float, Double, Vector, Vector2D, Vector4, Rotator, Quat, LinearColor, Color, Transform,
	};

	/**
	 * Calls one of the Web's graphs. Its numeric inputs are filled by position - A, then B - so renaming
	 * a graph's inputs in the editor does not break it.
	 */
	static void CallGraph(UObject* Target, UFunction* Function, double A, double B)
	{
		uint8* Parms = static_cast<uint8*>(FMemory_Alloca_Aligned(FMath::Max<int32>(Function->ParmsSize, 1), Function->GetMinAlignment()));
		FMemory::Memzero(Parms, FMath::Max<int32>(Function->ParmsSize, 1));

		int32 Numeric = 0;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			FProperty* Param = *It;
			Param->InitializeValue_InContainer(Parms);
			if (Param->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm))
			{
				continue;
			}
			const double Value = Numeric == 0 ? A : B;
			if (const FDoubleProperty* Double = CastField<FDoubleProperty>(Param))
			{
				Double->SetPropertyValue_InContainer(Parms, Value);
				++Numeric;
			}
			else if (const FFloatProperty* Float = CastField<FFloatProperty>(Param))
			{
				Float->SetPropertyValue_InContainer(Parms, static_cast<float>(Value));
				++Numeric;
			}
		}

		Target->ProcessEvent(Function, Parms);

		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Parms);
		}
	}

	/**
	 * Fires an output's own dispatcher: (Web, new value). Its signature is made by the editor to match
	 * the output; anything that no longer matches (the Web not recompiled since a type change) is left
	 * at its default rather than copied across types.
	 */
	static void FireOutputEvent(UObject* Web, const FMulticastDelegateProperty* Event, const FProperty* Value, const void* ValueAddress)
	{
		const FMulticastScriptDelegate* Delegate = Event->GetMulticastDelegate(Event->ContainerPtrToValuePtr<void>(Web));
		const UFunction* Signature = Event->SignatureFunction;
		if (!Delegate || !Delegate->IsBound() || !Signature)
		{
			return;
		}

		uint8* Parms = static_cast<uint8*>(FMemory_Alloca_Aligned(FMath::Max<int32>(Signature->ParmsSize, 1), Signature->GetMinAlignment()));
		FMemory::Memzero(Parms, FMath::Max<int32>(Signature->ParmsSize, 1));
		int32 Index = 0;
		for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It, ++Index)
		{
			FProperty* Param = *It;
			Param->InitializeValue_InContainer(Parms);
			if (Index == 0)
			{
				if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Param))
				{
					Object->SetObjectPropertyValue_InContainer(Parms, Web);
				}
			}
			else if (Index == 1 && Param->SameType(Value))
			{
				Param->CopyCompleteValue(Param->ContainerPtrToValuePtr<void>(Parms), ValueAddress);
			}
		}

		Delegate->ProcessDelegate<UObject>(Parms);

		for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Parms);
		}
	}

	/**
	 * Fires the Web Blueprint's On Outputs Changed: (Web, changed output names). As with an output's own
	 * dispatcher, a parameter that no longer matches is left at its default.
	 */
	static void FireOutputsChangedEvent(UObject* Web, const FMulticastDelegateProperty* Event, const TArray<FName>& Changed)
	{
		const FMulticastScriptDelegate* Delegate = Event->GetMulticastDelegate(Event->ContainerPtrToValuePtr<void>(Web));
		const UFunction* Signature = Event->SignatureFunction;
		if (!Delegate || !Delegate->IsBound() || !Signature)
		{
			return;
		}

		uint8* Parms = static_cast<uint8*>(FMemory_Alloca_Aligned(FMath::Max<int32>(Signature->ParmsSize, 1), Signature->GetMinAlignment()));
		FMemory::Memzero(Parms, FMath::Max<int32>(Signature->ParmsSize, 1));
		int32 Index = 0;
		for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It, ++Index)
		{
			FProperty* Param = *It;
			Param->InitializeValue_InContainer(Parms);
			if (Index == 0)
			{
				if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Param))
				{
					Object->SetObjectPropertyValue_InContainer(Parms, Web);
				}
			}
			else if (Index == 1)
			{
				const FArrayProperty* Array = CastField<FArrayProperty>(Param);
				if (Array && Array->Inner->IsA<FNameProperty>())
				{
					*Array->ContainerPtrToValuePtr<TArray<FName>>(Parms) = Changed;
				}
			}
		}

		Delegate->ProcessDelegate<UObject>(Parms);

		for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Parms);
		}
	}

	static EBlendKind KindOf(const FProperty* Property)
	{
		if (Property->IsA<FFloatProperty>())
		{
			return EBlendKind::Float;
		}
		if (Property->IsA<FDoubleProperty>())
		{
			return EBlendKind::Double;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			if (Struct == TBaseStructure<FVector>::Get())		{ return EBlendKind::Vector; }
			if (Struct == TBaseStructure<FVector2D>::Get())		{ return EBlendKind::Vector2D; }
			if (Struct == TBaseStructure<FVector4>::Get())		{ return EBlendKind::Vector4; }
			if (Struct == TBaseStructure<FRotator>::Get())		{ return EBlendKind::Rotator; }
			if (Struct == TBaseStructure<FQuat>::Get())			{ return EBlendKind::Quat; }
			if (Struct == TBaseStructure<FLinearColor>::Get())	{ return EBlendKind::LinearColor; }
			if (Struct == TBaseStructure<FColor>::Get())		{ return EBlendKind::Color; }
			if (Struct == TBaseStructure<FTransform>::Get())	{ return EBlendKind::Transform; }
		}
		return EBlendKind::Snap;
	}

	/** Copies a value between two properties that should be the same type, falling back to text if not. */
	static void CopyValue(const FProperty* Source, const void* SourceValue, const FProperty* Target, void* TargetValue)
	{
		if (Source->SameType(Target))
		{
			Target->CopyCompleteValue(TargetValue, SourceValue);
			return;
		}
		FString Text;
		Source->ExportTextItem_Direct(Text, SourceValue, nullptr, nullptr, PPF_None);
		Target->ImportText_Direct(*Text, TargetValue, nullptr, PPF_None);
	}

	struct FSlot
	{
		const FProperty* Property = nullptr;
		FName Name;
		FGuid ID;
		int32 Offset = 0;
		EBlendKind Kind = EBlendKind::Snap;
		ECodeAnimLerpPolicy Lerp = ECodeAnimLerpPolicy::Auto;
		/** The output's On <Output> Changed dispatcher on the Web Blueprint. */
		const FMulticastDelegateProperty* Event = nullptr;
	};

	/** Where each output lives in a pose. One per Web instance; poses share it. */
	struct FLayout
	{
		TArray<FSlot> Slots;
		int32 Size = 0;
		int32 Alignment = 1;
	};

	/**
	 * One value per output, in raw property memory.
	 *
	 * Not seen by the garbage collector: every object a pose can hold comes from the class defaults
	 * (the variables' defaults or the State Values), which keep it alive for as long as the class exists.
	 */
	class FPose
	{
	public:
		FPose() = default;
		explicit FPose(const TSharedRef<const FLayout>& InLayout) : Layout(InLayout) { Allocate(); }
		FPose(const FPose& Other) : Layout(Other.Layout)
		{
			if (Layout)
			{
				Allocate();
				CopyFrom(Other);
			}
		}
		FPose(FPose&& Other) : Layout(MoveTemp(Other.Layout)), Memory(Other.Memory) { Other.Memory = nullptr; }
		~FPose() { Free(); }

		FPose& operator=(const FPose& Other)
		{
			if (this != &Other)
			{
				if (Layout != Other.Layout)
				{
					Free();
					Layout = Other.Layout;
					Allocate();
				}
				CopyFrom(Other);
			}
			return *this;
		}

		FPose& operator=(FPose&& Other)
		{
			if (this != &Other)
			{
				Free();
				Layout = MoveTemp(Other.Layout);
				Memory = Other.Memory;
				Other.Memory = nullptr;
			}
			return *this;
		}

		uint8* Get(int32 Slot) { return Memory + Layout->Slots[Slot].Offset; }
		const uint8* Get(int32 Slot) const { return Memory + Layout->Slots[Slot].Offset; }

		void CopyFrom(const FPose& Other)
		{
			check(Layout == Other.Layout);
			for (int32 Index = 0; Index < Layout->Slots.Num(); ++Index)
			{
				Layout->Slots[Index].Property->CopyCompleteValue(Get(Index), Other.Get(Index));
			}
		}

		/** Reads every output off an object of the Web's class. */
		void ReadFrom(const UObject* Container)
		{
			for (int32 Index = 0; Index < Layout->Slots.Num(); ++Index)
			{
				const FProperty* Property = Layout->Slots[Index].Property;
				Property->CopyCompleteValue(Get(Index), Property->ContainerPtrToValuePtr<void>(Container));
			}
		}

		/** Writes every output onto an object of the Web's class, for a graph to read and change. */
		void WriteTo(UObject* Container) const
		{
			for (int32 Index = 0; Index < Layout->Slots.Num(); ++Index)
			{
				const FProperty* Property = Layout->Slots[Index].Property;
				Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Container), Get(Index));
			}
		}

		bool IsValid() const { return Memory != nullptr; }

	private:
		void Allocate()
		{
			Memory = static_cast<uint8*>(FMemory::Malloc(FMath::Max(Layout->Size, 1), Layout->Alignment));
			for (const FSlot& Slot : Layout->Slots)
			{
				Slot.Property->InitializeValue(Memory + Slot.Offset);
			}
		}

		void Free()
		{
			if (Memory && Layout)
			{
				for (const FSlot& Slot : Layout->Slots)
				{
					Slot.Property->DestroyValue(Memory + Slot.Offset);
				}
				FMemory::Free(Memory);
			}
			Memory = nullptr;
		}

		TSharedPtr<const FLayout> Layout;
		uint8* Memory = nullptr;
	};

	template <typename T, typename TScalar = double>
	static void LerpAs(uint8* Out, const uint8* To, TScalar Alpha)
	{
		T& From = *reinterpret_cast<T*>(Out);
		From = From + (*reinterpret_cast<const T*>(To) - From) * Alpha;
	}

	/** Out holds the from-value on entry and the blended value on exit. */
	static void BlendSlot(const FSlot& Slot, uint8* Out, const uint8* To, double Alpha)
	{
		// Custom blends as Auto first; its graph runs afterwards and overwrites what it likes.
		const bool bAuto = Slot.Lerp == ECodeAnimLerpPolicy::Auto || Slot.Lerp == ECodeAnimLerpPolicy::Custom;
		if (bAuto && Slot.Kind != EBlendKind::Snap)
		{
			switch (Slot.Kind)
			{
			case EBlendKind::Float:
				{
					float& From = *reinterpret_cast<float*>(Out);
					From = static_cast<float>(From + (*reinterpret_cast<const float*>(To) - From) * Alpha);
				}
				return;
			case EBlendKind::Double:		LerpAs<double>(Out, To, Alpha); return;
			case EBlendKind::Vector:		LerpAs<FVector>(Out, To, Alpha); return;
			case EBlendKind::Vector2D:		LerpAs<FVector2D>(Out, To, Alpha); return;
			case EBlendKind::Vector4:		LerpAs<FVector4>(Out, To, Alpha); return;
			// Component-wise and unwrapped, not shortest path: a state authored at 360 means a full turn.
			case EBlendKind::Rotator:		LerpAs<FRotator>(Out, To, Alpha); return;
			case EBlendKind::LinearColor:	LerpAs<FLinearColor, float>(Out, To, static_cast<float>(Alpha)); return;
			case EBlendKind::Quat:
				{
					FQuat& From = *reinterpret_cast<FQuat*>(Out);
					From = FQuat::Slerp(From, *reinterpret_cast<const FQuat*>(To), Alpha);
				}
				return;
			case EBlendKind::Color:
				{
					// In linear space, as FColor is sRGB-encoded and lerping the bytes darkens the midpoint.
					FColor& From = *reinterpret_cast<FColor*>(Out);
					const FLinearColor A(From);
					const FLinearColor B(*reinterpret_cast<const FColor*>(To));
					From = (A + (B - A) * static_cast<float>(Alpha)).ToFColor(true);
				}
				return;
			case EBlendKind::Transform:
				{
					FTransform& From = *reinterpret_cast<FTransform*>(Out);
					FTransform Blended;
					Blended.Blend(From, *reinterpret_cast<const FTransform*>(To), static_cast<float>(Alpha));
					From = Blended;
				}
				return;
			default:
				break;
			}
		}

		bool bSwitched;
		switch (Slot.Lerp)
		{
		case ECodeAnimLerpPolicy::SnapAtStart:	bSwitched = Alpha > 0.0; break;
		case ECodeAnimLerpPolicy::SnapAtEnd:	bSwitched = Alpha >= 1.0; break;
		default:								bSwitched = Alpha >= 0.5; break;
		}
		if (bSwitched)
		{
			Slot.Property->CopyCompleteValue(Out, To);
		}
	}
}

using namespace CodeAnimWebPrivate;

/**
 * Where a Web is and where it is going.
 *
 * The result is a stack: a base (a state, or a frozen pose), with transitions layered on top. Each
 * layer blends from the result of everything under it - still live, so a wiggle being left keeps
 * wiggling as it fades - towards its own target state. When a layer finishes, everything under it is
 * irrelevant and it becomes the new base.
 *
 * Graphs run against the Web itself: a pose is written onto its output variables, the graph sets
 * whichever of them it wants, and the pose is read back. Publish then writes the final values over the
 * top, so nothing outside the Web ever sees an intermediate.
 */
struct FCodeAnimWebRuntime
{
	struct FLayer
	{
		FName Target;
		double Start = 0.0;
		FCodeAnimWebTransitionTiming Timing;
		bool bReversed = false;
		UFunction* Graph = nullptr;

		double Progress(double Now) const
		{
			return Timing.Duration > 0.0f ? (Now - Start) / Timing.Duration : 1.0;
		}

		double Alpha(double Now) const
		{
			const double Linear = FMath::Clamp(Progress(Now), 0.0, 1.0);
			return bReversed ? 1.0 - Timing.AlphaAt(1.0 - Linear) : Timing.AlphaAt(Linear);
		}
	};

	TSharedPtr<const FLayout> Layout;

	/** The Web Blueprint's On Outputs Changed, if it has one. */
	const FMulticastDelegateProperty* OutputsChangedEvent = nullptr;

	/** The variables' defaults: what a state leaves an unticked output at. */
	FPose Defaults;
	/** Each state's ticked values over the defaults. A state with a graph runs it on top of these. */
	TMap<FName, FPose> StatePoses;
	TMap<FName, UFunction*> StateGraphs;
	TArray<UFunction*> CustomLerpGraphs;

	TMap<uint8, FName> KeyByValue;
	TMap<FName, uint8> ValueByKey;

	FName BaseState;
	double BaseStart = 0.0;
	/** When the base state was actually reached: where a route's next transition starts from. */
	double BaseArrived = 0.0;

	/**
	 * Transition Traversal Only: the transitions still to play, in order, each starting the moment the
	 * one before it finishes. Their Start is filled in as they begin.
	 */
	TArray<FLayer> Route;
	/** When the current route was asked for: its first transition cannot start before then. */
	double RouteRequestedAt = 0.0;
	bool bBaseFrozen = false;
	FPose Frozen;
	TArray<FLayer> Layers;

	/** The latest evaluation, and what was last written to the variables. */
	FPose Work;
	FPose Published;

	/** A graph state's evaluated values, and a transition's from-side kept while its graphs run. */
	FPose ScratchTo;
	FPose ScratchFrom;

	/** The two ends of the transition whose graphs are running, for Get From / To Output. */
	const FPose* ContextFrom = nullptr;
	const FPose* ContextTo = nullptr;

	/** A graph setting the state mid-evaluation is deferred until the evaluation is done. */
	bool bEvaluating = false;
	TOptional<TPair<uint8, bool>> PendingState;

	FName Target() const { return Layers.Num() > 0 ? Layers.Last().Target : BaseState; }

	/** Where the Web will end up: the end of the route, if it is on one. */
	FName FinalTarget() const { return Route.Num() > 0 ? Route.Last().Target : Target(); }

	/** Nothing playing and nothing still to play: sat in the state it was headed for. */
	bool HasArrived() const { return Layers.Num() == 0 && Route.Num() == 0 && !bBaseFrozen; }

	/** A settled Web still has to tick while it sits in a state whose graph animates. */
	bool HasLiveBase() const { return !bBaseFrozen && StateGraphs.Contains(BaseState); }

	const FPose& StatePose(UObject& Web, FName Key, double TimeInState)
	{
		const FPose* Constant = StatePoses.Find(Key);
		const FPose& Values = Constant ? *Constant : Defaults;
		UFunction* const* Graph = StateGraphs.Find(Key);
		if (!Graph)
		{
			return Values;
		}
		ScratchTo.CopyFrom(Values);
		ScratchTo.WriteTo(&Web);
		CallGraph(&Web, *Graph, TimeInState, 0.0);
		ScratchTo.ReadFrom(&Web);
		return ScratchTo;
	}

	/** Drops every layer made irrelevant by a finished layer above it. */
	void Collapse(double Now)
	{
		for (int32 Index = Layers.Num() - 1; Index >= 0; --Index)
		{
			if (Layers[Index].Progress(Now) >= 1.0)
			{
				BaseState = Layers[Index].Target;
				BaseStart = Layers[Index].Start;
				BaseArrived = Layers[Index].Start + FMath::Max(Layers[Index].Timing.Duration, 0.0f);
				bBaseFrozen = false;
				Layers.RemoveAt(0, Index + 1);
				return;
			}
		}
	}

	/**
	 * Collapses finished layers, then starts the route's next transitions for as long as there is
	 * nothing left playing - each at the exact moment the one before finished, so a slow frame does not
	 * stretch the route, and several short ones can all complete inside one.
	 */
	void Advance(double Now)
	{
		Collapse(Now);
		while (Route.Num() > 0 && Layers.Num() == 0 && !bBaseFrozen)
		{
			FLayer Step = Route[0];
			Route.RemoveAt(0);
			Step.Start = FMath::Min(FMath::Max(BaseArrived, RouteRequestedAt), Now);
			Layers.Add(MoveTemp(Step));
			Collapse(Now);
		}
	}

	/** The base with the first NumLayers layers blended over it. */
	void EvaluateLayers(UObject& Web, double Now, int32 NumLayers, FPose& Out)
	{
		if (bBaseFrozen)
		{
			Out.CopyFrom(Frozen);
		}
		else
		{
			Out.CopyFrom(StatePose(Web, BaseState, Now - BaseStart));
		}

		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			const FLayer& Layer = Layers[LayerIndex];
			const FPose& To = StatePose(Web, Layer.Target, Now - Layer.Start);
			const double Alpha = Layer.Alpha(Now);
			const double Progress = FMath::Clamp(Layer.Progress(Now), 0.0, 1.0);

			const bool bGraphs = Layer.Graph || CustomLerpGraphs.Num() > 0;
			if (bGraphs)
			{
				ScratchFrom.CopyFrom(Out);
			}

			for (int32 Slot = 0; Slot < Layout->Slots.Num(); ++Slot)
			{
				BlendSlot(Layout->Slots[Slot], Out.Get(Slot), To.Get(Slot), Alpha);
			}

			if (!bGraphs)
			{
				continue;
			}

			// Graphs start from the automatic blend, so any output they leave alone still travels.
			Out.WriteTo(&Web);
			if (Layer.Graph)
			{
				// As authored: a two-way transition played backwards swaps its ends and runs its alpha
				// back, so one graph describes both directions.
				ContextFrom = Layer.bReversed ? &To : &ScratchFrom;
				ContextTo = Layer.bReversed ? &ScratchFrom : &To;
				CallGraph(&Web, Layer.Graph, Layer.bReversed ? 1.0 - Alpha : Alpha, Layer.bReversed ? 1.0 - Progress : Progress);
			}

			// Custom Lerps last, and always in the direction actually travelled.
			ContextFrom = &ScratchFrom;
			ContextTo = &To;
			for (UFunction* Lerp : CustomLerpGraphs)
			{
				CallGraph(&Web, Lerp, Alpha, Progress);
			}
			ContextFrom = nullptr;
			ContextTo = nullptr;
			Out.ReadFrom(&Web);
		}
	}

	/** Bakes the base and the oldest layer into a frozen pose: the fallback when too many are stacked. */
	void FreezeOldest(UObject& Web, double Now)
	{
		FPose Baked(Layout.ToSharedRef());
		EvaluateLayers(Web, Now, 1, Baked);
		Frozen = MoveTemp(Baked);
		bBaseFrozen = true;
		Layers.RemoveAt(0);
	}
};

UCodeAnimationWeb::UCodeAnimationWeb()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Only ticks while something animates; see UpdateTickEnabled.
	PrimaryComponentTick.bStartWithTickEnabled = false;

	InterruptTransition.Duration = 0.2f;
	InterruptTransition.Easing = EEasingFunc::EaseOut;
}

void UCodeAnimationWeb::GetStateList(TArray<TPair<FName, FText>>& OutStates) const
{
	OutStates.Reset();
	if (!StateEnum)
	{
		return;
	}
	const int32 Num = StateEnum->NumEnums() - (StateEnum->ContainsExistingMax() ? 1 : 0);
	for (int32 Index = 0; Index < Num; ++Index)
	{
		OutStates.Emplace(FName(StateEnum->GetNameStringByIndex(Index)), StateEnum->GetDisplayNameTextByIndex(Index));
	}
}

double UCodeAnimationWeb::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

FCodeAnimWebRuntime& UCodeAnimationWeb::EnsureRuntime(double Now)
{
	if (Runtime.IsValid())
	{
		return *Runtime;
	}

	// The definition lives on the class defaults; instances only carry state.
	UClass* Class = GetClass();
	const UCodeAnimationWeb* Definition = Class->GetDefaultObject<UCodeAnimationWeb>();
	Runtime = MakeShared<FCodeAnimWebRuntime>();
	FCodeAnimWebRuntime& R = *Runtime;

	const TSharedRef<FLayout> Layout = MakeShared<FLayout>();
	for (const FCodeAnimOutputSpec& Spec : Definition->OutputSpecs)
	{
		const FProperty* Property = Class->FindPropertyByName(Spec.Name);
		if (!Property)
		{
			UE_LOG(LogCodeAnimWeb, Warning, TEXT("%s: output '%s' has no variable - recompile the Web."),
				*Class->GetName(), *Spec.Name.ToString());
			continue;
		}
		FSlot& Slot = Layout->Slots.AddDefaulted_GetRef();
		Slot.Property = Property;
		Slot.Name = Spec.Name;
		Slot.ID = Spec.ID;
		Slot.Kind = KindOf(Property);
		Slot.Lerp = Spec.Lerp;
		Slot.Event = Spec.EventName.IsNone() ? nullptr : FindFProperty<FMulticastDelegateProperty>(Class, Spec.EventName);
		const int32 Alignment = FMath::Max(Property->GetMinAlignment(), 1);
		Slot.Offset = Align(Layout->Size, Alignment);
		Layout->Size = Slot.Offset + Property->GetSize();
		Layout->Alignment = FMath::Max(Layout->Alignment, Alignment);
	}
	R.Layout = Layout;
	R.OutputsChangedEvent = Definition->OutputsChangedEventName.IsNone()
		? nullptr : FindFProperty<FMulticastDelegateProperty>(Class, Definition->OutputsChangedEventName);

	R.Defaults = FPose(Layout);
	R.Defaults.ReadFrom(Definition);

	for (const FCodeAnimWebStateEntry& Entry : Definition->States)
	{
		if (Entry.bOrphaned)
		{
			continue;
		}
		FPose Pose = R.Defaults;
		const void* BagMemory = Entry.Values.GetValue().GetMemory();
		for (int32 Index = 0; Index < Layout->Slots.Num(); ++Index)
		{
			const FSlot& Slot = Layout->Slots[Index];
			if (!BagMemory || !Entry.OverriddenOutputs.Contains(Slot.ID))
			{
				continue;
			}
			const FPropertyBagPropertyDesc* Desc = Entry.Values.FindPropertyDescByID(Slot.ID);
			if (Desc && Desc->CachedProperty)
			{
				CopyValue(Desc->CachedProperty, Desc->CachedProperty->ContainerPtrToValuePtr<void>(BagMemory),
					Slot.Property, Pose.Get(Index));
			}
		}
		R.StatePoses.Add(Entry.Key, MoveTemp(Pose));

		if (!Entry.GraphFunction.IsNone())
		{
			if (UFunction* Graph = Class->FindFunctionByName(Entry.GraphFunction))
			{
				R.StateGraphs.Add(Entry.Key, Graph);
			}
		}
	}

	for (const FCodeAnimCustomLerp& Lerp : Definition->CustomLerps)
	{
		const bool bCustom = Layout->Slots.ContainsByPredicate([&Lerp](const FSlot& Slot)
		{
			return Slot.ID == Lerp.Output && Slot.Lerp == ECodeAnimLerpPolicy::Custom;
		});
		UFunction* Graph = Lerp.GraphFunction.IsNone() ? nullptr : Class->FindFunctionByName(Lerp.GraphFunction);
		if (bCustom && Graph)
		{
			R.CustomLerpGraphs.Add(Graph);
		}
	}

	TArray<TPair<FName, FText>> StateList;
	Definition->GetStateList(StateList);
	for (int32 Index = 0; Index < StateList.Num(); ++Index)
	{
		// GetStateList walks the enum by index, so its order matches the enum's own.
		const uint8 Value = static_cast<uint8>(Definition->StateEnum->GetValueByIndex(Index));
		R.KeyByValue.Add(Value, StateList[Index].Key);
		R.ValueByKey.Add(StateList[Index].Key, Value);
	}

	if (Definition->InitialState.IsSet() && R.ValueByKey.Contains(Definition->InitialState.Key))
	{
		R.BaseState = Definition->InitialState.Key;
	}
	else if (StateList.Num() > 0)
	{
		R.BaseState = StateList[0].Key;
	}
	R.BaseStart = Now;
	R.BaseArrived = Now;

	R.Work = FPose(Layout);
	R.ScratchTo = FPose(Layout);
	R.ScratchFrom = FPose(Layout);
	R.Published = FPose(Layout);
	R.Published.ReadFrom(this);
	return R;
}

void UCodeAnimationWeb::Evaluate(double Now)
{
	FCodeAnimWebRuntime& R = *Runtime;
	R.bEvaluating = true;
	R.Advance(Now);
	R.EvaluateLayers(*this, Now, R.Layers.Num(), R.Work);
	R.bEvaluating = false;
}

void UCodeAnimationWeb::Publish(bool bBroadcastAll)
{
	FCodeAnimWebRuntime& R = *Runtime;
	const TArray<FSlot>& Slots = R.Layout->Slots;

	// Every variable is written before anything is broadcast, so a listener reading several outputs
	// sees them all from the same moment. Written even when unchanged: a graph may have left an
	// intermediate value on the variable during evaluation.
	TArray<int32, TInlineAllocator<16>> Changed;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FSlot& Slot = Slots[Index];
		Slot.Property->CopyCompleteValue(Slot.Property->ContainerPtrToValuePtr<void>(this), R.Work.Get(Index));
		if (!bBroadcastAll && Slot.Property->Identical(R.Work.Get(Index), R.Published.Get(Index)))
		{
			continue;
		}
		Slot.Property->CopyCompleteValue(R.Published.Get(Index), R.Work.Get(Index));
		Changed.Add(Index);
	}

	if (Changed.Num() == 0)
	{
		return;
	}

	// The value handed out is the variable's, read at the moment of firing: a listener earlier in the
	// list may have set the state again, and later listeners should see what is true now.
	TArray<FName> ChangedNames;
	ChangedNames.Reserve(Changed.Num());
	for (const int32 Index : Changed)
	{
		const FSlot& Slot = Slots[Index];
		if (Slot.Event)
		{
			FireOutputEvent(this, Slot.Event, Slot.Property, Slot.Property->ContainerPtrToValuePtr<void>(this));
		}
		ChangedNames.Add(Slot.Name);
	}

	// Once, however many changed: a listener re-applying the whole Web runs once, not once per output.
	if (R.OutputsChangedEvent)
	{
		FireOutputsChangedEvent(this, R.OutputsChangedEvent, ChangedNames);
	}
	if (OnOutputChanged.IsBound())
	{
		OnOutputChanged.Broadcast(this, ChangedNames);
	}
}

void UCodeAnimationWeb::ApplyPendingState(double Now)
{
	if (Runtime.IsValid() && Runtime->PendingState.IsSet())
	{
		const TPair<uint8, bool> Pending = Runtime->PendingState.GetValue();
		Runtime->PendingState.Reset();
		SetStateWebTime(Pending.Key, Now, Pending.Value);
	}
}

void UCodeAnimationWeb::UpdateTickEnabled()
{
	if (IsTemplate())
	{
		return;
	}
	const bool bNeedsTick = !bIsPaused && Runtime.IsValid()
		&& (Runtime->Layers.Num() > 0 || Runtime->Route.Num() > 0 || Runtime->HasLiveBase());
	if (IsComponentTickEnabled() != bNeedsTick)
	{
		SetComponentTickEnabled(bNeedsTick);
	}
}

bool UCodeAnimationWeb::SetState(uint8 NewState, bool bInstant)
{
	return SetStateAtTime(NewState, GetNow(), bInstant);
}

namespace CodeAnimWebPrivate
{
	/**
	 * Transition Traversal Only: the cheapest chain of authored transitions from From to To, as layers
	 * ready to play (Start unset). Dijkstra over the Transitions rows, two-way rows usable both ways.
	 * The cost is the chosen measure with the other as a tie-break.
	 */
	static bool FindRoute(const UCodeAnimationWeb& Definition, const UClass* Class, FName From, FName To,
		TArray<FCodeAnimWebRuntime::FLayer>& OutRoute)
	{
		struct FEdge
		{
			FName To;
			FCodeAnimWebRuntime::FLayer Step;
			double Cost = 0.0;
		};

		const bool bByTime = Definition.PathCost == ECodeAnimWebPathCost::ShortestTime;
		TMap<FName, TArray<FEdge>> Edges;
		for (const FCodeAnimWebTransition& Transition : Definition.Transitions)
		{
			if (!Transition.From.IsSet() || !Transition.To.IsSet() || Transition.From == Transition.To)
			{
				continue;
			}
			const double Duration = FMath::Max(Transition.Timing.Duration, 0.0f);
			const double Cost = bByTime ? Duration + 1e-6 : 1.0 + Duration * 1e-6;
			UFunction* Graph = Transition.GraphFunction.IsNone() ? nullptr : Class->FindFunctionByName(Transition.GraphFunction);
			auto Add = [&Edges, &Transition, Cost, Graph](FName A, FName B, bool bReversed)
			{
				FEdge& Edge = Edges.FindOrAdd(A).AddDefaulted_GetRef();
				Edge.To = B;
				Edge.Cost = Cost;
				Edge.Step.Target = B;
				Edge.Step.Timing = Transition.Timing;
				Edge.Step.bReversed = bReversed;
				Edge.Step.Graph = Graph;
			};
			Add(Transition.From.Key, Transition.To.Key, false);
			if (Transition.bTwoWay)
			{
				Add(Transition.To.Key, Transition.From.Key, true);
			}
		}

		TMap<FName, double> Distance;
		TMap<FName, TPair<FName, const FEdge*>> Previous;
		TSet<FName> Done;
		Distance.Add(From, 0.0);
		for (;;)
		{
			// Few states: a linear scan for the nearest open node is plenty.
			FName Nearest;
			double NearestDistance = TNumericLimits<double>::Max();
			for (const TPair<FName, double>& Entry : Distance)
			{
				if (!Done.Contains(Entry.Key) && Entry.Value < NearestDistance)
				{
					Nearest = Entry.Key;
					NearestDistance = Entry.Value;
				}
			}
			if (Nearest.IsNone() || Nearest == To)
			{
				break;
			}
			Done.Add(Nearest);
			if (const TArray<FEdge>* Out = Edges.Find(Nearest))
			{
				for (const FEdge& Edge : *Out)
				{
					const double Candidate = NearestDistance + Edge.Cost;
					const double* Known = Distance.Find(Edge.To);
					if (!Known || Candidate < *Known)
					{
						Distance.Add(Edge.To, Candidate);
						Previous.Add(Edge.To, TPair<FName, const FEdge*>(Nearest, &Edge));
					}
				}
			}
		}

		if (!Previous.Contains(To))
		{
			return false;
		}
		OutRoute.Reset();
		for (FName At = To; At != From; )
		{
			const TPair<FName, const FEdge*>& Step = Previous.FindChecked(At);
			OutRoute.Insert(Step.Value->Step, 0);
			At = Step.Key;
		}
		return true;
	}
}

bool UCodeAnimationWeb::SetStateAtTime(uint8 NewState, double Now, bool bInstant)
{
	if (IsStateLocked())
	{
		UE_LOG(LogCodeAnimWeb, Verbose, TEXT("%s: %s; Set State to %d refused."), *GetName(),
			bIsPaused ? TEXT("paused") : TEXT("headed for a pause"), NewState);
		return false;
	}
	return SetStateWebTime(NewState, ToWebTime(Now), bInstant);
}

bool UCodeAnimationWeb::PauseInState(uint8 NewState, bool bInstant)
{
	return PauseInStateAtTime(NewState, GetNow(), bInstant);
}

bool UCodeAnimationWeb::PauseInStateAtTime(uint8 NewState, double Now, bool bInstant)
{
	if (IsStateLocked())
	{
		UE_LOG(LogCodeAnimWeb, Verbose, TEXT("%s: already paused or headed for a pause; Pause In State refused."), *GetName());
		return false;
	}

	// Locked before the state is set, so a graph reacting to the change cannot redirect it.
	bPauseOnArrival = true;
	const double WebNow = ToWebTime(Now);
	if (!SetStateWebTime(NewState, WebNow, bInstant))
	{
		bPauseOnArrival = false;
		return false;
	}

	if (Runtime->HasArrived())
	{
		// Instant, or already sat there: freeze now, on the values of now. (Set State does not evaluate
		// when the state is already the target, so do it here - the Web may never have run.)
		Evaluate(WebNow);
		Publish(false);
		FreezeAtWebTime(WebNow);
	}
	UpdateTickEnabled();
	return true;
}

void UCodeAnimationWeb::FreezeAtWebTime(double Now)
{
	bPauseOnArrival = false;
	bIsPaused = true;
	// Back to game time: the Web's clock is game time less the time already spent paused.
	PausedAt = Now + PausedTotal;
	UpdateTickEnabled();
}

bool UCodeAnimationWeb::SetStateWebTime(uint8 NewState, double Now, bool bInstant)
{
	FCodeAnimWebRuntime& R = EnsureRuntime(Now);
	if (R.bEvaluating)
	{
		// From inside one of this Web's own graphs: changing the layers mid-evaluation would pull them
		// out from under it. Whether it succeeds is only known once it runs.
		R.PendingState = TPair<uint8, bool>(NewState, bInstant);
		return true;
	}

	const FName* Key = R.KeyByValue.Find(NewState);
	if (!Key)
	{
		UE_LOG(LogCodeAnimWeb, Warning, TEXT("%s: %d is not a value of the States enum."), *GetName(), NewState);
		return false;
	}

	const UClass* Class = GetClass();
	const UCodeAnimationWeb* Definition = Class->GetDefaultObject<UCodeAnimationWeb>();

	if (Definition->bTransitionTraversalOnly && !bInstant)
	{
		R.Advance(Now);
		const FName Final = R.FinalTarget();
		if (*Key == Final)
		{
			return true;
		}

		// From the state being headed for right now: the transition in progress always finishes, as
		// leaving it part-way would be a move no authored transition describes.
		const FName Committed = R.Target();
		TArray<FCodeAnimWebRuntime::FLayer> Route;
		if (*Key != Committed && !CodeAnimWebPrivate::FindRoute(*Definition, Class, Committed, *Key, Route))
		{
			UE_LOG(LogCodeAnimWeb, Verbose, TEXT("%s: no transitions lead from %s to %s; staying put."),
				*GetName(), *Committed.ToString(), *Key->ToString());
			return false;
		}
		R.Route = MoveTemp(Route);
		R.RouteRequestedAt = Now;

		Evaluate(Now);
		Publish(false);
		UpdateTickEnabled();
		const uint8* FinalValue = R.ValueByKey.Find(Final);
		OnStateChanged.Broadcast(this, NewState, FinalValue ? *FinalValue : NewState);
		ApplyPendingState(Now);
		return true;
	}

	R.Collapse(Now);
	const FName Previous = R.FinalTarget();
	if (*Key == Previous && !(bInstant && (R.Layers.Num() > 0 || R.Route.Num() > 0)))
	{
		return true;
	}

	if (bInstant)
	{
		R.Layers.Reset();
		R.Route.Reset();
		R.bBaseFrozen = false;
		R.BaseState = *Key;
		R.BaseStart = Now;
		R.BaseArrived = Now;
	}
	else
	{
		FCodeAnimWebRuntime::FLayer Layer;
		Layer.Target = *Key;
		Layer.Start = Now;

		if (R.Layers.Num() == 0 && !R.bBaseFrozen)
		{
			// Sat in a state that was actually reached: the authored transition applies.
			Layer.Timing = Definition->DefaultTransition;
			for (const FCodeAnimWebTransition& Transition : Definition->Transitions)
			{
				const bool bForward = Transition.From.Key == R.BaseState && Transition.To.Key == *Key;
				const bool bBackward = Transition.bTwoWay && Transition.From.Key == *Key && Transition.To.Key == R.BaseState;
				if (bForward || bBackward)
				{
					Layer.Timing = Transition.Timing;
					Layer.bReversed = !bForward;
					Layer.Graph = Transition.GraphFunction.IsNone() ? nullptr : Class->FindFunctionByName(Transition.GraphFunction);
					break;
				}
			}
		}
		else
		{
			Layer.Timing = Definition->InterruptTransition;
		}

		R.bEvaluating = true;
		while (R.Layers.Num() >= FMath::Max(Definition->MaxBlendDepth, 1))
		{
			R.FreezeOldest(*this, Now);
		}
		R.bEvaluating = false;
		R.Layers.Add(MoveTemp(Layer));
	}

	Evaluate(Now);
	Publish(false);
	UpdateTickEnabled();

	const uint8* PreviousValue = R.ValueByKey.Find(Previous);
	OnStateChanged.Broadcast(this, NewState, PreviousValue ? *PreviousValue : NewState);
	ApplyPendingState(Now);
	return true;
}

void UCodeAnimationWeb::UpdateAtTime(double Now)
{
	if (!bIsPaused)
	{
		UpdateWebTime(ToWebTime(Now));
	}
}

void UCodeAnimationWeb::SetPaused(bool bPaused)
{
	SetPausedAtTime(bPaused, GetNow());
}

void UCodeAnimationWeb::SetPausedAtTime(bool bPaused, double Now)
{
	// Either way a pending Pause In State is over: unpausing releases it, pausing does its job now.
	bPauseOnArrival = false;
	if (bPaused == bIsPaused)
	{
		return;
	}
	if (bPaused)
	{
		// The outputs keep whatever was last published; nothing is evaluated on the way in.
		PausedAt = Now;
	}
	else
	{
		PausedTotal += Now - PausedAt;
	}
	bIsPaused = bPaused;
	UpdateTickEnabled();
}

void UCodeAnimationWeb::UpdateWebTime(double Now)
{
	FCodeAnimWebRuntime& R = EnsureRuntime(Now);
	double At = Now;
	bool bFreeze = false;
	if (bPauseOnArrival)
	{
		// Arrived since the last update: evaluate at the moment of arrival rather than now, so the frozen
		// values are the state's own and not a frame past them.
		R.Advance(Now);
		if (R.HasArrived())
		{
			At = FMath::Min(R.BaseArrived, Now);
			bFreeze = true;
		}
	}
	Evaluate(At);
	Publish(false);
	if (bFreeze)
	{
		FreezeAtWebTime(At);
	}
	UpdateTickEnabled();
	ApplyPendingState(At);
}

void UCodeAnimationWeb::Refresh()
{
	UpdateAtTime(GetNow());
}

uint8 UCodeAnimationWeb::GetState() const
{
	if (Runtime.IsValid())
	{
		const uint8* Value = Runtime->ValueByKey.Find(Runtime->FinalTarget());
		return Value ? *Value : 0;
	}
	// Not started yet: report where it will start.
	const UCodeAnimationWeb* Definition = GetClass()->GetDefaultObject<UCodeAnimationWeb>();
	TArray<TPair<FName, FText>> StateList;
	Definition->GetStateList(StateList);
	for (int32 Index = 0; Index < StateList.Num(); ++Index)
	{
		if (StateList[Index].Key == Definition->InitialState.Key)
		{
			return static_cast<uint8>(Definition->StateEnum->GetValueByIndex(Index));
		}
	}
	return StateList.Num() > 0 ? static_cast<uint8>(Definition->StateEnum->GetValueByIndex(0)) : 0;
}

bool UCodeAnimationWeb::IsTransitioning() const
{
	// Only the top layer matters: once it finishes, everything under it is gone too. A route still to
	// play counts as travelling.
	return Runtime.IsValid() && (Runtime->Route.Num() > 0
		|| (Runtime->Layers.Num() > 0 && Runtime->Layers.Last().Progress(ToWebTime(GetNow())) < 1.0));
}

void UCodeAnimationWeb::RebroadcastOutputs()
{
	// Allowed while paused: on the stopped clock it re-sends the frozen values, and changes nothing.
	const double Now = ToWebTime(GetNow());
	EnsureRuntime(Now);
	Evaluate(Now);
	Publish(true);
	UpdateTickEnabled();
	ApplyPendingState(Now);
}

TArray<FName> UCodeAnimationWeb::GetOutputNames() const
{
	TArray<FName> Names;
	for (const FCodeAnimOutputSpec& Spec : GetClass()->GetDefaultObject<UCodeAnimationWeb>()->OutputSpecs)
	{
		Names.Add(Spec.Name);
	}
	return Names;
}

void UCodeAnimationWeb::ReadOutput(FName Output, int32 Source, const FProperty* ValueProperty, void* ValueAddress) const
{
	const FProperty* Own = GetClass()->FindPropertyByName(Output);
	if (!ValueProperty || !ValueAddress || !Own || !Own->SameType(ValueProperty))
	{
		UE_LOG(LogCodeAnimWeb, Warning, TEXT("%s: cannot read output '%s' - refresh the node reading it."),
			*GetName(), *Output.ToString());
		return;
	}

	// Source 0: the live variable. 1 / 2: the from / to end of the transition being evaluated.
	if (Source == 0 || !Runtime.IsValid())
	{
		Own->CopyCompleteValue(ValueAddress, Own->ContainerPtrToValuePtr<void>(this));
		return;
	}

	const FCodeAnimWebRuntime& R = *Runtime;
	const int32 Slot = R.Layout->Slots.IndexOfByPredicate([Output](const FSlot& S) { return S.Name == Output; });
	if (Slot == INDEX_NONE)
	{
		Own->CopyCompleteValue(ValueAddress, Own->ContainerPtrToValuePtr<void>(GetClass()->GetDefaultObject()));
		return;
	}
	const FPose* Pose = Source == 1 ? R.ContextFrom : R.ContextTo;
	Own->CopyCompleteValue(ValueAddress, Pose ? Pose->Get(Slot) : R.Defaults.Get(Slot));
}

DEFINE_FUNCTION(UCodeAnimationWeb::execGetTransitionEndpoint)
{
	P_GET_PROPERTY(FNameProperty, Output);
	P_GET_UBOOL(bTo);

	Stack.MostRecentProperty = nullptr;
	Stack.MostRecentPropertyAddress = nullptr;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	void* ValueAddress = Stack.MostRecentPropertyAddress;

	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->ReadOutput(Output, bTo ? 2 : 1, ValueProperty, ValueAddress);
	P_NATIVE_END;
}

void UCodeAnimationWeb::BeginPlay()
{
	Super::BeginPlay();

	// Everything, once: events bound before play need the starting values as much as the changes.
	RebroadcastOutputs();
}

void UCodeAnimationWeb::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateAtTime(GetNow());
}

#if WITH_EDITOR

void UCodeAnimationWeb::SyncDefinition()
{
	UClass* Class = GetClass();
	const UEnum* LerpEnum = StaticEnum<ECodeAnimLerpPolicy>();

	TArray<FCodeAnimOutputSpec> Specs;
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property->HasMetaData(CodeAnimWeb::OutputMetaKey))
		{
			continue;
		}
		FCodeAnimOutputSpec& Spec = Specs.AddDefaulted_GetRef();
		Spec.Name = Property->GetFName();
		Spec.ID = Class->FindPropertyGuidFromName(Spec.Name);
		if (!Spec.ID.IsValid())
		{
			Spec.ID = FGuid::NewDeterministicGuid(Spec.Name.ToString());
		}
		const int64 Lerp = LerpEnum->GetValueByNameString(Property->GetMetaData(CodeAnimWeb::LerpMetaKey));
		Spec.Lerp = Lerp == INDEX_NONE ? ECodeAnimLerpPolicy::Auto : static_cast<ECodeAnimLerpPolicy>(Lerp);
	}

	// Each output's dispatcher, recognised by the output GUID the editor stamped on it, and the one for
	// all of them.
	OutputsChangedEventName = NAME_None;
	for (TFieldIterator<FMulticastDelegateProperty> It(Class); It; ++It)
	{
		const FString Owner = It->GetMetaData(CodeAnimWeb::EventMetaKey);
		if (Owner.IsEmpty())
		{
			continue;
		}
		if (Owner == CodeAnimWeb::AllOutputsEventKey)
		{
			OutputsChangedEventName = It->GetFName();
			continue;
		}
		for (FCodeAnimOutputSpec& Spec : Specs)
		{
			if (Spec.ID.ToString() == Owner)
			{
				Spec.EventName = It->GetFName();
			}
		}
	}
	OutputSpecs = MoveTemp(Specs);
	RebuildOutputDefaults();

	TSet<FGuid> LiveOutputs;
	for (const FCodeAnimOutputSpec& Spec : OutputSpecs)
	{
		LiveOutputs.Add(Spec.ID);
	}

	// Graphs are held by GUID and run by function name; the name is refreshed here, after any rename in
	// My Blueprint. A graph that has been deleted unlinks.
	UBlueprint* Blueprint = GetWebBlueprint();
	auto ResolveGraph = [Blueprint](FGuid& Guid, FName& Function)
	{
		Function = NAME_None;
		if (!Guid.IsValid())
		{
			return;
		}
		if (Blueprint)
		{
			for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
			{
				if (Graph && Graph->GraphGuid == Guid)
				{
					Function = Graph->GetFName();
					return;
				}
			}
		}
		Guid.Invalidate();
	};

	for (FCodeAnimWebTransition& Transition : Transitions)
	{
		ResolveGraph(Transition.GraphGuid, Transition.GraphFunction);
	}
	CustomLerps.RemoveAll([&LiveOutputs](const FCodeAnimCustomLerp& Lerp) { return !LiveOutputs.Contains(Lerp.Output); });
	for (FCodeAnimCustomLerp& Lerp : CustomLerps)
	{
		ResolveGraph(Lerp.GraphGuid, Lerp.GraphFunction);
	}

	auto Refresh = [this, &LiveOutputs, &ResolveGraph](FCodeAnimWebStateEntry& Entry)
	{
		ResolveGraph(Entry.GraphGuid, Entry.GraphFunction);
		Entry.OverriddenOutputs.RemoveAll([&LiveOutputs](const FGuid& ID) { return !LiveOutputs.Contains(ID); });
		if (OutputDefaults.IsValid())
		{
			// Keeps overridden values (matched by ID, so a renamed output keeps its value) and puts
			// everything else back to the current default.
			Entry.Values.MigrateToNewBagInstanceWithOverrides(OutputDefaults, Entry.OverriddenOutputs);
		}
		else
		{
			Entry.Values.Reset();
		}
	};

	TArray<TPair<FName, FText>> StateList;
	GetStateList(StateList);

	TArray<FCodeAnimWebStateEntry> Previous = MoveTemp(States);
	States.Reset();
	for (const TPair<FName, FText>& State : StateList)
	{
		const int32 Found = Previous.IndexOfByPredicate(
			[&State](const FCodeAnimWebStateEntry& Entry) { return Entry.Key == State.Key; });
		FCodeAnimWebStateEntry Entry;
		if (Found != INDEX_NONE)
		{
			Entry = MoveTemp(Previous[Found]);
			Previous.RemoveAt(Found);
		}
		Entry.Key = State.Key;
		Entry.DisplayName = State.Value;
		Entry.bOrphaned = false;
		Refresh(Entry);
		States.Add(MoveTemp(Entry));
	}
	for (FCodeAnimWebStateEntry& Entry : Previous)
	{
		if (Entry.Key.IsNone())
		{
			continue;
		}
		Entry.bOrphaned = true;
		Refresh(Entry);
		States.Add(MoveTemp(Entry));
	}
}

void UCodeAnimationWeb::RebuildOutputDefaults()
{
	UClass* Class = GetClass();
	const UObject* Defaults = Class->GetDefaultObject();

	TArray<FPropertyBagPropertyDesc> Descs;
	for (const FCodeAnimOutputSpec& Spec : OutputSpecs)
	{
		if (const FProperty* Property = Class->FindPropertyByName(Spec.Name))
		{
			FPropertyBagPropertyDesc& Desc = Descs.Emplace_GetRef(Spec.Name, Property);
			Desc.ID = Spec.ID;
		}
	}

	OutputDefaults.Reset();
	if (Descs.Num() == 0)
	{
		return;
	}
	OutputDefaults.AddProperties(Descs);

	void* BagMemory = OutputDefaults.GetMutableValue().GetMemory();
	for (const FCodeAnimOutputSpec& Spec : OutputSpecs)
	{
		const FProperty* Property = Class->FindPropertyByName(Spec.Name);
		const FPropertyBagPropertyDesc* Desc = OutputDefaults.FindPropertyDescByID(Spec.ID);
		if (Property && Desc && Desc->CachedProperty)
		{
			CopyValue(Property, Property->ContainerPtrToValuePtr<void>(Defaults),
				Desc->CachedProperty, Desc->CachedProperty->ContainerPtrToValuePtr<void>(BagMemory));
		}
	}
}

UBlueprint* UCodeAnimationWeb::GetWebBlueprint() const
{
	return Cast<UBlueprint>(GetClass()->ClassGeneratedBy);
}

const FInstancedPropertyBag& UCodeAnimationWeb::GetOutputDefaults()
{
	if (!OutputDefaults.IsValid() && OutputSpecs.Num() > 0)
	{
		RebuildOutputDefaults();
	}
	return OutputDefaults;
}

void UCodeAnimationWeb::RemoveOrphanedStates()
{
	Modify();
	States.RemoveAll([](const FCodeAnimWebStateEntry& Entry) { return Entry.bOrphaned; });
}

void UCodeAnimationWeb::PostCDOCompiled(const FPostCDOCompiledContext& Context)
{
	Super::PostCDOCompiled(Context);
	if (!Context.bIsSkeletonOnly)
	{
		SyncDefinition();
	}
}

void UCodeAnimationWeb::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Anything but the State Values themselves: the enum, or an output's default (which unticked
	// cells show). Rebuilding the array under the Details panel while it edits a cell is asking for trouble.
	if (HasAnyFlags(RF_ClassDefaultObject)
		&& PropertyChangedEvent.GetMemberPropertyName() != GET_MEMBER_NAME_CHECKED(UCodeAnimationWeb, States))
	{
		SyncDefinition();
	}
}

#endif
