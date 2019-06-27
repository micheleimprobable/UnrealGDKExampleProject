// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Character.h"
#include "Runtime/Engine/Classes/GameFramework/NavMovementComponent.h"
#include "SimPlayerMovementComponent.generated.h"

UCLASS(meta = (BlueprintSpawnableComponent))
class USimPlayerMovementComponent : public UNavMovementComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	USimPlayerMovementComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

	ACharacter* ControlledPlayer;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	virtual void RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) override;
	virtual void RequestPathMove(const FVector& MoveInput) override;
};
