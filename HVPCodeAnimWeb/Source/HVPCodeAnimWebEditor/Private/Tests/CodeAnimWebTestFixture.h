#pragma once

// Shared by the Code Animation Web tests. Only included from inside WITH_DEV_AUTOMATION_TESTS.

#include "CodeAnimationWeb.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace CodeAnimWebTests
{
	/**
	 * A Web Blueprint with three outputs - Scale (double), Tint (colour), Visible (bool, so it snaps) -
	 * and a three-state Blueprint enum, compiled and synced. States: 0 all default, 1 sets all three,
	 * 2 sets Scale only.
	 */
	struct FFixture
	{
		TStrongObjectPtr<UBlueprint> Blueprint;
		TStrongObjectPtr<UUserDefinedEnum> Enum;

		FFixture()
		{
			UPackage* Package = GetTransientPackage();
			Blueprint.Reset(FKismetEditorUtilities::CreateBlueprint(UCodeAnimationWeb::StaticClass(), Package,
				MakeUniqueObjectName(Package, UBlueprint::StaticClass(), TEXT("CodeAnimWebTest")), BPTYPE_Normal));

			FEdGraphPinType Real;
			Real.PinCategory = UEdGraphSchema_K2::PC_Real;
			Real.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			FEdGraphPinType Colour;
			Colour.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Colour.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
			FEdGraphPinType Bool;
			Bool.PinCategory = UEdGraphSchema_K2::PC_Boolean;

			UBlueprint* BP = Blueprint.Get();
			FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Scale"), Real, TEXT("1.0"));
			FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Tint"), Colour, TEXT("(R=0.0,G=0.0,B=1.0,A=1.0)"));
			FBlueprintEditorUtils::AddMemberVariable(BP, TEXT("Visible"), Bool, TEXT("true"));
			for (const TCHAR* Name : { TEXT("Scale"), TEXT("Tint"), TEXT("Visible") })
			{
				FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, Name, nullptr, CodeAnimWeb::OutputMetaKey, TEXT("true"));
			}

			Enum.Reset(Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(Package,
				MakeUniqueObjectName(Package, UUserDefinedEnum::StaticClass(), TEXT("E_CodeAnimWebTest")), RF_Public | RF_Transient)));
			while (Enum->NumEnums() - 1 < 3)
			{
				FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum.Get());
			}

			FKismetEditorUtilities::CompileBlueprint(BP);
			UCodeAnimationWeb* Web = Defaults();
			Web->StateEnum = Enum.Get();
			Web->DefaultTransition.Duration = 1.0f;
			Web->DefaultTransition.Easing = EEasingFunc::Linear;
			Web->InterruptTransition.Duration = 1.0f;
			Web->InterruptTransition.Easing = EEasingFunc::Linear;
			Web->SyncDefinition();

			Set(1, TEXT("Scale"), [](FInstancedPropertyBag& Bag) { Bag.SetValueDouble(TEXT("Scale"), 2.0); });
			Set(1, TEXT("Tint"), [](FInstancedPropertyBag& Bag) { Bag.SetValueStruct(TEXT("Tint"), FConstStructView::Make(FLinearColor(1.0f, 0.0f, 0.0f, 1.0f))); });
			Set(1, TEXT("Visible"), [](FInstancedPropertyBag& Bag) { Bag.SetValueBool(TEXT("Visible"), false); });
			Set(2, TEXT("Scale"), [](FInstancedPropertyBag& Bag) { Bag.SetValueDouble(TEXT("Scale"), 0.5); });
		}

		UCodeAnimationWeb* Defaults() const
		{
			return Blueprint->GeneratedClass->GetDefaultObject<UCodeAnimationWeb>();
		}

		FCodeAnimWebStateEntry& Entry(int32 State) const
		{
			const FName Key(Enum->GetNameStringByIndex(State));
			return *Defaults()->States.FindByPredicate([Key](const FCodeAnimWebStateEntry& E) { return E.Key == Key; });
		}

		template <typename TSetter>
		void Set(int32 State, const TCHAR* Output, TSetter&& Setter) const
		{
			FCodeAnimWebStateEntry& E = Entry(State);
			Setter(E.Values);
			E.OverriddenOutputs.AddUnique(E.Values.FindPropertyDescByName(Output)->ID);
		}

		UCodeAnimationWeb* NewWeb() const
		{
			return NewObject<UCodeAnimationWeb>(GetTransientPackage(), Blueprint->GeneratedClass);
		}
	};

	template <typename T>
	inline T Read(const UObject* Web, const TCHAR* Name)
	{
		const FProperty* Property = Web->GetClass()->FindPropertyByName(Name);
		return Property ? *Property->ContainerPtrToValuePtr<T>(Web) : T();
	}
}

