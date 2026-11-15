#include "WWOSwitches.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool WWOSwitches::Has(const TCHAR* Name)
{
	static TMap<FString, bool> Known;
	const FString Key(Name);
	if (const bool* Found = Known.Find(Key)) return *Found;
	return Known.Add(Key, FParse::Param(FCommandLine::Get(), Name));
}

bool WWOSwitches::Value(const TCHAR* Match, FString& Out, bool bStopOnSeparator)
{
	return FParse::Value(FCommandLine::Get(), Match, Out, bStopOnSeparator);
}

bool WWOSwitches::Value(const TCHAR* Match, int32& Out)
{
	return FParse::Value(FCommandLine::Get(), Match, Out);
}

bool WWOSwitches::Value(const TCHAR* Match, float& Out)
{
	return FParse::Value(FCommandLine::Get(), Match, Out);
}

namespace
{
	// Whether the command line contains this text, ignoring case.
	bool LineHas(const TCHAR* Text) { return FCString::Stristr(FCommandLine::Get(), Text) != nullptr; }
}

bool WWOSwitches::HasAutoTestText()
{
	static const bool bHas = LineHas(TEXT("WWOAutoTest"));
	return bHas;
}

bool WWOSwitches::HasAutoTestSwitch()
{
	static const bool bHas = LineHas(TEXT("-WWOAutoTest"));
	return bHas;
}

bool WWOSwitches::HasTitleTestSwitch()
{
	static const bool bHas = LineHas(TEXT("-WWOAutoTestTitle"));
	return bHas;
}

bool WWOSwitches::HasAnyWWOSwitch()
{
	static const bool bHas = LineHas(TEXT("-WWO"));
	return bHas;
}

bool WWOSwitches::NamesGamePath()
{
	static const bool bHas = LineHas(TEXT("/Game/"));
	return bHas;
}

bool WWOSwitches::IsStress()
{
	static const bool bIs = Has(TEXT("WWOStress"));
	return bIs;
}

bool WWOSwitches::HasStressText()
{
	static const bool bHas = LineHas(TEXT("WWOStress"));
	return bHas;
}

bool WWOSwitches::UsesDefaultBinds()
{
	return HasAutoTestText() || IsStress();
}
