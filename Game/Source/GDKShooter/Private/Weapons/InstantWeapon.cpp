// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "InstantWeapon.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "GDKLogging.h"
#include "UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "FrustumConstraintFiller.h"
#include "Interop/SpatialReceiverEntityQueue.h"
#include "EngineClasses/SpatialGameInstance.h"
#include <ciso646>
#include <algorithm>

class USpatialStaticComponentView;

namespace {
    class IdToPositionIterator {
    public:
        typedef SpatialReceiverEntityQueue::ActorQueue::const_iterator wrapped_iterator_type;
        typedef FVector value_type;
        typedef FVector reference;
        typedef FVector pointer;
        typedef wrapped_iterator_type::iterator_category iterator_category;
        typedef wrapped_iterator_type::difference_type difference_type;

        IdToPositionIterator (USpatialStaticComponentView* comp_view, const wrapped_iterator_type& it) :
            m_wrapped(it),
            m_comp_view(comp_view)
        {
        }

        reference operator*() { return SpatialReceiverEntityQueue::position(*m_wrapped, m_comp_view); }
        const reference operator*() const { return SpatialReceiverEntityQueue::position(*m_wrapped, m_comp_view); }
        pointer operator->() { return SpatialReceiverEntityQueue::position(*m_wrapped, m_comp_view); }
        IdToPositionIterator& operator++() { ++m_wrapped; return *this; }
        wrapped_iterator_type& wrapped_iterator() { return m_wrapped; }
        difference_type operator- (const IdToPositionIterator& other) const { return m_wrapped - other.m_wrapped; }
        IdToPositionIterator operator+= (difference_type offs) { m_wrapped += offs; return *this; }

    private:
        wrapped_iterator_type m_wrapped;
        USpatialStaticComponentView* m_comp_view;
    };

    float squared_distance (const FVector& a, const FVector& b) {
        return (a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z);
    }
} //unnamed namespace

AInstantWeapon::AInstantWeapon()
{
	BurstInterval = 0.5f;
	BurstCount = 1;
	ShotInterval = 0.2f;
	NextBurstTime = 0.0f;
	BurstShotsRemaining = 0;
	ShotBaseDamage = 10.0f;
	HitValidationTolerance = 50.0f;
	DamageTypeClass = UDamageType::StaticClass();  // generic damage type
	ShotVisualizationDelayTolerance = FTimespan::FromMilliseconds(3000.0f);
}

void AInstantWeapon::StartPrimaryUse_Implementation()
{
	if (IsBurstFire())
	{
		if (!IsPrimaryUsing && NextBurstTime < UGameplayStatics::GetRealTimeSeconds(GetWorld()))
		{
			BurstShotsRemaining = BurstCount;
			NextBurstTime = UGameplayStatics::GetRealTimeSeconds(GetWorld()) + BurstInterval;
		}
	}

	Super::StartPrimaryUse_Implementation();
}

void AInstantWeapon::StopPrimaryUse_Implementation()
{
	// Can't force stop a burst.
	if (!IsBurstFire() || bAllowContinuousBurstFire)
	{
		Super::StopPrimaryUse_Implementation();
	}
}

void AInstantWeapon::DoFire_Implementation()
{
	if (!bIsActive)
	{
		IsPrimaryUsing = false;
		ConsumeBufferedShot();
		return;
	}

	NextShotTime = UGameplayStatics::GetRealTimeSeconds(GetWorld()) + ShotInterval;
	
	FInstantHitInfo HitInfo = DoLineTrace();
	if (HitInfo.bDidHit)
	{
		ServerDidHit(HitInfo);
		SpawnFX(HitInfo, true);  // Spawn the hit fx locally
		AnnounceShot(HitInfo.HitActor ? HitInfo.HitActor->bCanBeDamaged : false);
	}
	else
	{
		ServerDidMiss(HitInfo);
		SpawnFX(HitInfo, false);  // Spawn the hit fx locally
		AnnounceShot(false);
	}

	if (IsBurstFire())
	{
		--BurstShotsRemaining;
		if (BurstShotsRemaining <= 0)
		{
			FinishedBurst();
			if (bAllowContinuousBurstFire)
			{
				BurstShotsRemaining = BurstCount;
				// We will force a cooldown for the full burst interval, regardless of the time already consumed in the previous burst, for simplicity.
				ForceCooldown(BurstInterval);
			}
			else
			{
				if (GetMovementComponent())
				{
					GetMovementComponent()->SetIsBusy(false);
				}
				IsPrimaryUsing = false;
			}
		}
	}

}

FVector AInstantWeapon::GetLineTraceDirection()
{
	FVector Direction = Super::GetLineTraceDirection();

	float SpreadToUse = SpreadAt100m;
	if (GetMovementComponent())
	{
		if (GetMovementComponent()->IsAiming())
		{
			SpreadToUse = SpreadAt100mWhenAiming;
		}
		if (GetMovementComponent()->IsCrouching())
		{
			SpreadToUse *= SpreadCrouchModifier;
		}
	}

	if (SpreadToUse > 0)
	{
		auto Spread = FMath::RandPointInCircle(SpreadToUse);
		Direction = Direction.Rotation().RotateVector(FVector(10000, Spread.X, Spread.Y));
	}

	return Direction;
}

void AInstantWeapon::NotifyClientsOfHit(const FInstantHitInfo& HitInfo, bool bImpact)
{
	check(GetNetMode() < NM_Client);

	MulticastNotifyHit(HitInfo, bImpact);
}

void AInstantWeapon::SpawnFX(const FInstantHitInfo& HitInfo, bool bImpact)
{
	if (GetNetMode() < NM_Client)
	{
		return;
	}
	
	AInstantWeapon::OnRenderShot(HitInfo.Location, bImpact);
}

bool AInstantWeapon::ValidateHit(const FInstantHitInfo& HitInfo)
{
	check(GetNetMode() < NM_Client);

	if (HitInfo.HitActor == nullptr)
	{
		return false;
	}

	// Get the bounding box of the actor we hit.
	const FBox HitBox = HitInfo.HitActor->GetComponentsBoundingBox();

	// Calculate the extent of the box along all 3 axes an add a tolerance factor.
	FVector BoxExtent = 0.5 * (HitBox.Max - HitBox.Min) + (HitValidationTolerance * FVector::OneVector);
	FVector BoxCenter = (HitBox.Max + HitBox.Min) * 0.5;

	// Avoid precision errors for really thin objects.
	BoxExtent.X = FMath::Max(20.0f, BoxExtent.X);
	BoxExtent.Y = FMath::Max(20.0f, BoxExtent.Y);
	BoxExtent.Z = FMath::Max(20.0f, BoxExtent.Z);

	// Check whether the hit is within the box + tolerance.
	if (FMath::Abs(HitInfo.Location.X - BoxCenter.X) > BoxExtent.X ||
		FMath::Abs(HitInfo.Location.Y - BoxCenter.Y) > BoxExtent.Y ||
		FMath::Abs(HitInfo.Location.Z - BoxCenter.Z) > BoxExtent.Z)
	{
		return false;
	}

	return true;
}

void AInstantWeapon::DealDamage(const FInstantHitInfo& HitInfo)
{
	FPointDamageEvent DmgEvent;
	DmgEvent.DamageTypeClass = DamageTypeClass;
	DmgEvent.HitInfo.ImpactPoint = HitInfo.Location;

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		HitInfo.HitActor->TakeDamage(ShotBaseDamage, DmgEvent, Pawn->GetController(), this);
	}
}

bool AInstantWeapon::ServerDidHit_Validate(const FInstantHitInfo& HitInfo)
{
	return true;
}

void AInstantWeapon::ServerDidHit_Implementation(const FInstantHitInfo& HitInfo)
{

	bool bDoNotifyHit = false;

	if (HitInfo.HitActor == nullptr)
	{
		bDoNotifyHit = true;
	}
	else
	{
		if (ValidateHit(HitInfo))
		{
			DealDamage(HitInfo);
			bDoNotifyHit = true;
		}
		else
		{
			UE_LOG(LogGDK, Verbose, TEXT("%s server: rejected hit of actor %s"), *this->GetName(), *HitInfo.HitActor->GetName());
		}
	}

	if (bDoNotifyHit)
	{
		NotifyClientsOfHit(HitInfo, true);
	}
}

bool AInstantWeapon::ServerDidMiss_Validate(const FInstantHitInfo& HitInfo)
{
	return true;
}

void AInstantWeapon::ServerDidMiss_Implementation(const FInstantHitInfo& HitInfo)
{
	NotifyClientsOfHit(HitInfo, false);
}

void AInstantWeapon::MulticastNotifyHit_Implementation(FInstantHitInfo HitInfo, bool bImpact)
{
	// Make sure we're a client, and we're not the client that owns this gun (they will have already played the effect locally).
	APawn* Pawn = Cast<APawn>(GetOwner());

	if (GetNetMode() != NM_DedicatedServer &&
		(Pawn == nullptr || !Pawn->IsLocallyControlled()))
	{
		SpawnFX(HitInfo, bImpact);
	}
}

void AInstantWeapon::SetIsActive(bool bNewActive)
{
	Super::SetIsActive(bNewActive);

	ConsumeBufferedShot();
}

void AInstantWeapon::SetupZoomedQBIBox (UActorInterestComponent* interest, float distance, AActor* character, float fov, int splits) {
    interest->Queries.Empty();
    auto *const zoom_interest = Cast<UActorInterestComponent>(
        character->GetComponentByClass(UActorInterestComponent::StaticClass()));

    FrustumConstraintFiller filler(fov, 500.0f, distance);

    auto boxes = filler.box_filling(FVector(0.0f), character->GetActorForwardVector(), splits);
}

void AInstantWeapon::SetupZoomedQBI (UActorInterestComponent* interest, float distance, AActor* character, float fov) {
    interest->Queries.Empty();
    auto *const zoom_interest = Cast<UActorInterestComponent>(character->GetComponentByClass(UActorInterestComponent::StaticClass()));

    FrustumConstraintFiller filler(fov, 500.0f, distance);
    auto spheres = filler.sphere_filling(character->GetActorLocation(), character->GetActorForwardVector());

    auto d = character->GetActorForwardVector();
    for (const FrustumConstraintFiller::Sphere& sphere : spheres) {
        FQueryData new_query;
        USphereConstraint *const new_sphere = NewObject<USphereConstraint>();
        new_sphere->Center = sphere.centre;
        new_sphere->Radius = sphere.radius;
        new_query.Constraint = new_sphere;
        interest->Queries.Add(new_query);
    }

    zoom_interest->refresh();
}

void AInstantWeapon::SetupZoomedQBIClient() {
    auto* game_inst = Cast<USpatialGameInstance>(GetWorld()->GetGameInstance());
    UE_LOG(LogBlueprint, Warning, TEXT("Got game_inst=%p"), game_inst);
    if (game_inst) {
        USpatialGameInstance::ActorSpawnDelegateChain::delegate_type new_delegate;
        new_delegate.BindLambda([this](const FVector& pos, const SpatialReceiverEntityQueue* queue, USpatialStaticComponentView* comp_view) {
            UE_LOG(LogBlueprint, Warning, TEXT("----------------------------- inside the lower_bound delegate thing --------------------------------"));
            auto lo_queue = queue->low_prio_queue();
            auto it_before = std::lower_bound(
                    IdToPositionIterator(comp_view, lo_queue.begin()),
                    IdToPositionIterator(comp_view, lo_queue.end()),
                    pos,
                    [this](const FVector& l, const FVector& r) {
                        AActor* const self = this->GetOwner();
                        return squared_distance(l, self->GetActorLocation()) < squared_distance(r, self->GetActorLocation());
                    }
            );
            UE_LOG(LogBlueprint, Error, TEXT("lowerbound says insert before %d, total %d"), it_before.wrapped_iterator().m_index, lo_queue.size());
            return USpatialGameInstance::NewActorQueuePriority{
                    USpatialGameInstance::NewActorQueuePriority::Low,
                    it_before.wrapped_iterator()
            };
        });
        if (m_lambda_id)
            game_inst->ActorSpawning().erase(m_lambda_id);
        m_lambda_id = game_inst->ActorSpawning().push_front(std::move(new_delegate));
        UE_LOG(LogBlueprint, Warning, TEXT("added to delegate list, now it's got %tu entries, id=%d"), game_inst->ActorSpawning().size(), m_lambda_id);
    }
}

void AInstantWeapon::RemoveZoomedQBI() {
    UE_LOG(LogBlueprint, Warning, TEXT("------------------------------------ Would remove zoomed QBI"));
}

void AInstantWeapon::RemoveZoomedQBIClient() {
    if (m_lambda_id) {
        auto* game_inst = Cast<USpatialGameInstance>(GetWorld()->GetGameInstance());
        if (game_inst) {
            game_inst->ActorSpawning().erase(m_lambda_id);
        }
        m_lambda_id = 0;
    }
}

void AInstantWeapon::DrawDebugSpheresOnly (float distance, AActor* character, float fov) {
    auto* const zoom_interest = Cast<UActorInterestComponent>(character->GetComponentByClass(UActorInterestComponent::StaticClass()));

    UE_LOG(LogBlueprint, Warning, TEXT("Drawing sphere sphere sphere sphere sphere sphere sphere sphere spher spher spher spher spher spher spher spher spher spher spher spher sphereeeeeeeeeeeee"));

    FrustumConstraintFiller filler(fov, 500.0f, distance);
    auto spheres = filler.sphere_filling(character->GetActorLocation(), character->GetActorForwardVector());
    for (const auto& sphere : spheres) {
        DrawDebugSphere(
            GetWorld(),
            sphere.centre,
            sphere.radius,
            30,
            FColor(0xb7, 0x10, 0x20, 0x60),
            false,
            75.0f,
            0x0,
            2.0f
        );
    }
}
