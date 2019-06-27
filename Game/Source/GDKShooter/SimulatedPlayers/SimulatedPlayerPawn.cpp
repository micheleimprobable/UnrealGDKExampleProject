// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SimulatedPlayerPawn.h"


// Sets default values
ASimulatedPlayerPawn::ASimulatedPlayerPawn()
{
 	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ASimulatedPlayerPawn::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void ASimulatedPlayerPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ASimulatedPlayerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

