#include "Session/ValorSessionTypes.h"

namespace ValorSession
{
	FString BuildServerURL(EValorHostMode Mode, int32 MinPlayers)
	{
		if (Mode == EValorHostMode::Deathmatch)
		{
			return FString::Printf(TEXT("%s?game=DM"), ServerMap);
		}
		return FString::Printf(TEXT("%s?Lobby=1?MinPlayers=%d"), ServerMap, FMath::Clamp(MinPlayers, 1, 10));
	}

	FString BuildLocalServerArguments(bool bEditorBinary, const FString& ProjectFile, const FString& ServerURL, int32 Port, const FString& LogFile)
	{
		FString Arguments;
		if (bEditorBinary)
		{
			Arguments = FString::Printf(TEXT("\"%s\" %s -server"), *ProjectFile, *ServerURL);
		}
		else
		{
			Arguments = ServerURL;
		}
		Arguments += FString::Printf(TEXT(" -port=%d -log -unattended -nosound -abslog=\"%s\""), Port, *LogFile);
		return Arguments;
	}

	bool IsAllowedNameChar(TCHAR Character)
	{
		return (FChar::IsAlnum(Character) && Character < 128) || Character == TEXT('_') || Character == TEXT('-');
	}

	FString SanitizePlayerName(const FString& Name)
	{
		FString Result;
		for (const TCHAR Character : Name)
		{
			if (IsAllowedNameChar(Character) && Result.Len() < MaxNameLength)
			{
				Result.AppendChar(Character);
			}
		}
		return Result;
	}

	bool IsAllowedAddressChar(TCHAR Character)
	{
		return (FChar::IsAlnum(Character) && Character < 128) || Character == TEXT('.') || Character == TEXT(':') || Character == TEXT('-');
	}

	bool IsValidJoinAddress(const FString& Address, FString& OutError)
	{
		const FString Trimmed = Address.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			OutError = TEXT("ENTER AN ADDRESS");
			return false;
		}
		if (Trimmed.Len() > MaxAddressLength)
		{
			OutError = TEXT("ADDRESS TOO LONG");
			return false;
		}
		for (const TCHAR Character : Trimmed)
		{
			if (!IsAllowedAddressChar(Character))
			{
				OutError = TEXT("ONLY LETTERS, NUMBERS, '.', '-' AND ':PORT'");
				return false;
			}
		}

		FString Host = Trimmed;
		FString Port;
		if (Trimmed.Split(TEXT(":"), &Host, &Port))
		{
			// 콜론은 하나만(포트). IPv6 주소는 받지 않는다.
			int32 PortNumber = 0;
			if (Port.Contains(TEXT(":")) || Port.IsEmpty() || !Port.IsNumeric() || !LexTryParseString(PortNumber, *Port) || PortNumber < 1 || PortNumber > 65535)
			{
				OutError = TEXT("PORT MUST BE 1-65535");
				return false;
			}
		}
		if (Host.IsEmpty() || Host.StartsWith(TEXT(".")) || Host.EndsWith(TEXT(".")) || Host.Contains(TEXT("..")))
		{
			OutError = TEXT("CHECK THE ADDRESS");
			return false;
		}

		// 숫자와 점만이면 IPv4: 네 칸, 칸마다 0~255(1.2.3.4:5를 콜론 없이 친 1.2.3.45 같은 오타를 많이 거른다).
		bool bNumeric = true;
		for (const TCHAR Character : Host)
		{
			bNumeric &= FChar::IsDigit(Character) || Character == TEXT('.');
		}
		if (bNumeric)
		{
			TArray<FString> Octets;
			Host.ParseIntoArray(Octets, TEXT("."), false);
			int32 Value = 0;
			bool bValid = Octets.Num() == 4;
			for (const FString& Octet : Octets)
			{
				bValid &= !Octet.IsEmpty() && Octet.Len() <= 3 && LexTryParseString(Value, *Octet) && Value >= 0 && Value <= 255;
			}
			if (!bValid)
			{
				OutError = TEXT("IP MUST BE FOUR NUMBERS 0-255 (E.G. 192.168.0.5)");
				return false;
			}
		}
		return true;
	}

	FString MakeUniqueName(const FString& Desired, const TArray<FString>& Taken)
	{
		if (!Taken.Contains(Desired))
		{
			return Desired;
		}
		for (int32 Suffix = 2; Suffix < 100; ++Suffix)
		{
			const FString Candidate = FString::Printf(TEXT("%s %d"), *Desired, Suffix);
			if (!Taken.Contains(Candidate))
			{
				return Candidate;
			}
		}
		return Desired;
	}

	TCHAR KeyToChar(const FKey& Key, bool bShift)
	{
		const FString Name = Key.GetFName().ToString();

		// 글자 키는 이름이 한 글자(A~Z).
		if (Name.Len() == 1 && FChar::IsAlpha(Name[0]))
		{
			return bShift ? FChar::ToUpper(Name[0]) : FChar::ToLower(Name[0]);
		}

		static const TCHAR* const DigitNames[] = {TEXT("Zero"), TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four"), TEXT("Five"), TEXT("Six"), TEXT("Seven"), TEXT("Eight"), TEXT("Nine")};
		for (int32 Digit = 0; Digit < 10; ++Digit)
		{
			// 숫자 줄은 Shift를 누르면 기호라 받지 않는다(이름·주소에 쓸 기호가 아니다). 숫자 패드는 그대로.
			if (Name == DigitNames[Digit])
			{
				return bShift ? 0 : static_cast<TCHAR>(TEXT('0') + Digit);
			}
			if (Name == FString(TEXT("NumPad")) + DigitNames[Digit])
			{
				return static_cast<TCHAR>(TEXT('0') + Digit);
			}
		}

		if (Key == EKeys::Period || Key == EKeys::Decimal)
		{
			return TEXT('.');
		}
		if (Key == EKeys::Hyphen || Key == EKeys::Subtract)
		{
			return bShift && Key == EKeys::Hyphen ? TEXT('_') : TEXT('-');
		}
		if (Key == EKeys::Semicolon && bShift)
		{
			return TEXT(':');
		}
		return 0;
	}
}
