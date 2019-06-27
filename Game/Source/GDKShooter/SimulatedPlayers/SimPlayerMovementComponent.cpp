// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SimPlayerMovementComponent.h"
#include "SimulatedPlayerPawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimPlayer, Log, All);

// Sets default values for this component's properties
USimPlayerMovementComponent::USimPlayerMovementComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void USimPlayerMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...

}


// Called every frame
void USimPlayerMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Initialise controlled player if not done already.
	if (ControlledPlayer == nullptr)
	{
		ASimulatedPlayerPawn* owner = Cast<ASimulatedPlayerPawn>(GetOwner());
		if (owner == nullptr) {
			UE_LOG(LogSimPlayer, Warning, TEXT("Cannot get ControlledPlayer from owner, owner is not a SimulatedPlayerPawn"));
			return;
		}

		auto ControlledPlayerInOwner = owner->ControlledPlayer;
		if (ControlledPlayerInOwner == nullptr)
		{
			UE_LOG(LogSimPlayer, Warning, TEXT("Cannot set ControlledPlayer, ControlledPlayer in owner is null"));
			return;
		}

		ControlledPlayer = ControlledPlayerInOwner;
	}
}

void USimPlayerMovementComponent::RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed)
{
	if (ControlledPlayer != nullptr)
	{
		// Redirect move request to ControlledPlayer
		ControlledPlayer->AddMovementInput(MoveVelocity, 1.0f, bForceMaxSpeed);
	}
}

void USimPlayerMovementComponent::RequestPathMove(const FVector& MoveInput)
{
	if (ControlledPlayer != nullptr)
	{
		// Redirect move request to ControlledPlayer
		ControlledPlayer->AddMovementInput(MoveInput);
	}
}
