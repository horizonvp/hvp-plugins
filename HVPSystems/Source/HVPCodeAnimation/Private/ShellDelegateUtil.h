#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"

/**
 * Fires Blueprint-declared multicast dispatchers by name via reflection.
 *
 * The migration reparents the Content/HVP Blueprints onto the native classes but leaves their
 * original event dispatchers in place, because consumer Blueprints bind to them with signatures
 * (e.g. a Component pin typed to the Blueprint class) that native delegates cannot reproduce
 * exactly. The native code calls Fire() after each of its own broadcasts; on a plain native
 * instance the named property doesn't exist and the call is a cheap no-op.
 */
namespace HVPShellDelegates
{
	inline void SetStr(UFunction* Sig, uint8* Parms, const TCHAR* Name, const FString& V)
	{
		if (FStrProperty* P = FindFProperty<FStrProperty>(Sig, Name))
		{
			P->SetPropertyValue_InContainer(Parms, V);
		}
	}

	inline void SetDouble(UFunction* Sig, uint8* Parms, const TCHAR* Name, double V)
	{
		if (FDoubleProperty* P = FindFProperty<FDoubleProperty>(Sig, Name))
		{
			P->SetPropertyValue_InContainer(Parms, V);
		}
		else if (FFloatProperty* P2 = FindFProperty<FFloatProperty>(Sig, Name))
		{
			P2->SetPropertyValue_InContainer(Parms, static_cast<float>(V));
		}
	}

	inline void SetBool(UFunction* Sig, uint8* Parms, const TCHAR* Name, bool V)
	{
		if (FBoolProperty* P = FindFProperty<FBoolProperty>(Sig, Name))
		{
			P->SetPropertyValue_InContainer(Parms, V);
		}
	}

	inline void SetObj(UFunction* Sig, uint8* Parms, const TCHAR* Name, UObject* V)
	{
		FObjectPropertyBase* P = FindFProperty<FObjectPropertyBase>(Sig, Name);
		if (P && (!V || !P->PropertyClass || V->IsA(P->PropertyClass)))
		{
			P->SetObjectPropertyValue_InContainer(Parms, V);
		}
	}

	template <typename FillType>
	void Fire(UObject* Obj, const TCHAR* PropName, FillType Fill)
	{
		FMulticastDelegateProperty* Prop = FindFProperty<FMulticastDelegateProperty>(Obj->GetClass(), PropName);
		if (!Prop || !Prop->SignatureFunction)
		{
			return;
		}
		const FMulticastScriptDelegate* Del = Prop->GetMulticastDelegate(Prop->ContainerPtrToValuePtr<void>(Obj));
		if (!Del || !Del->IsBound())
		{
			return;
		}
		UFunction* Sig = Prop->SignatureFunction;
		TArray<uint8, TInlineAllocator<128>> Parms;
		Parms.SetNumZeroed(Sig->ParmsSize);
		for (TFieldIterator<FProperty> It(Sig); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Parms.GetData());
		}
		Fill(Sig, Parms.GetData());
		// ProcessMulticastDelegate<T> was deprecated in 5.8 in favour of ProcessDelegate, which it
		// now just forwards to. Calling it directly avoids the deprecation and the template
		// instantiation failure the forwarding wrapper produces under 5.8.
		Del->ProcessDelegate<UObject>(Parms.GetData());
		for (TFieldIterator<FProperty> It(Sig); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Parms.GetData());
		}
	}
}
