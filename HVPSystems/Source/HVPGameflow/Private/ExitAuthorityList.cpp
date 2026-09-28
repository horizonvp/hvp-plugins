#include "ExitAuthorityList.h"

void UExitAuthorityList::AddAuthority(FName Authority)
{
	Authorities.Add(Authority);
}

void UExitAuthorityList::RemoveAuthority(FName Authority)
{
	Authorities.Remove(Authority);
}
