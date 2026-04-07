// Copyright Epic Games, Inc. All Rights Reserved.

#include "ValorGameMode.h"
#include "ValorCharacter.h"
#include "UObject/ConstructorHelpers.h"

AValorGameMode::AValorGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
