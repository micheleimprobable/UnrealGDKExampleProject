// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "InstantWeapon.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "GDKLogging.h"
#include "UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include <cmath>
#include <ciso646>
#include <tuple>
#include <algorithm>

namespace {
    const constexpr float g_pi = 3.1415927f;

    [[gnu::const]]
    std::tuple<float, float> sin_cos (float a) {
        float s, c;
        FMath::SinCos(&s, &c, a);
        return std::make_tuple(s, c);
    }

    [[gnu::const]]
    float radius_at_distance (float m, float d) {
        return m * d / (m + FMath::Sqrt(1.0f + m * m));
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
    auto* const zoom_interest = Cast<UActorInterestComponent>(character->GetComponentByClass(UActorInterestComponent::StaticClass()));
    const auto [sin_half_fov, cos_half_fov] = sin_cos(FMath::DegreesToRadians(fov) / 2.0f);
    const auto facing_proj_2d = FVector(character->GetActorForwardVector().X, character->GetActorForwardVector().Y, 0.0f).GetSafeNormal();
    const float sin_a = FVector::CrossProduct(FVector(1.0f, 0.0f, 0.0f), facing_proj_2d).Z;
    const float cos_a = FVector::DotProduct(FVector(1.0f, 0.0f, 0.0f), facing_proj_2d);
    const float cos_lower_angle = cos_a * cos_half_fov + sin_a * sin_half_fov;
    const float sin_upper_angle = sin_a * cos_half_fov + cos_a * sin_half_fov;
    const float i = distance * cos_half_fov;
    const float fov_base = distance * std::tan(FMath::DegreesToRadians(fov) / 2.0f) * 2.0f;
    const float bounding_rect_h = std::max(FMath::Abs(i * sin_upper_angle), fov_base);
    const float bounding_rect_w = std::max(FMath::Abs(i * cos_lower_angle), fov_base);

    const float waste_ratio = (bounding_rect_w * bounding_rect_h - fov_base * distance / 2.0f) / (bounding_rect_w * bounding_rect_h);

    UE_LOG(LogBlueprint, Warning, TEXT("Bounding rect facing <%g, %g, %g> is %fx%f, A=%f,\nfovbase=%f, A=%f, i=%f, waste=%f%%, lowercos=%f, uppersin=%f\ncos_=%f, sin_a=%f"),
        facing_proj_2d.X, facing_proj_2d.Y, facing_proj_2d.Z,
        bounding_rect_w, bounding_rect_h,
        bounding_rect_w * bounding_rect_h,
        fov_base,
        distance * fov_base / 2.0f,
        i,
        waste_ratio * 100.0f,
        cos_lower_angle, sin_upper_angle,
        cos_a, sin_a
    );

    //const float curr_top
    if (cos_a < std::cos(g_pi / 4.0f) and cos_a > std::cos(g_pi / 4.0f * 3.0f)) {
        //character is looking towards y or -y, split vertically
        UE_LOG(LogBlueprint, Warning, TEXT("Splitting vertically"));
    }
    else {
        //character is looking towards x or -x, split horizontally
        UE_LOG(LogBlueprint, Warning, TEXT("Splitting horizontally"));
        const float sin_lower_angle = sin_a * cos_half_fov - cos_a * sin_half_fov;
        const float cos_upper_angle = cos_a * cos_half_fov - sin_a * sin_half_fov;
        float prev_bottom = bounding_rect_h;
        float prev_left = bounding_rect_w;
        for (int z = splits; z > 0; --z) {
            const float left = static_cast<float>(z - 1) * (bounding_rect_w / static_cast<float>(splits));
            const float top = prev_bottom;
            const float right = prev_left;
            const float m_lower = sin_lower_angle / cos_lower_angle;
            const float bottom = m_lower * left;
            prev_left = left;
            const float m_upper = sin_upper_angle / cos_upper_angle;
            prev_bottom = m_upper * left;

            UE_LOG(LogBlueprint, Warning, TEXT("Would add rect [%f, %f, %f, %f]"), left, top, right, bottom);
        }
    }
}

void AInstantWeapon::SetupZoomedQBI (UActorInterestComponent* interest, float distance, AActor* character, float fov) {
    interest->Queries.Empty();
    auto* const zoom_interest = Cast<UActorInterestComponent>(character->GetComponentByClass(UActorInterestComponent::StaticClass()));

    unsigned int circle_count = 0;
    float max_radius = 0.0f, min_radius = 0.0f;
    const float tan_half_fov = std::tan(FMath::DegreesToRadians(fov) / 2.0f);
    float remaining_dist = distance + radius_at_distance(tan_half_fov, distance);
	while (remaining_dist > 500.0f) {
        const float radius = radius_at_distance(tan_half_fov, remaining_dist);
        remaining_dist -= radius;
        const FVector location(character->GetActorForwardVector() * remaining_dist + character->GetActorLocation());
        FQueryData new_query;
        USphereConstraint* const new_sphere = NewObject<USphereConstraint>();
        new_sphere->Center = location;
        new_sphere->Radius = radius;
        new_query.Constraint = new_sphere;
        interest->Queries.Add(new_query);

        UE_LOG(LogBlueprint, Warning, TEXT("Setting up zoomed QBI to <%g, %g, %g>, dist=%g, rad=%g"),
               location.X, location.Y, location.Z,
               remaining_dist,
               new_sphere->Radius
        );
        if (0 == circle_count)
            max_radius = radius;
        min_radius = radius;
        remaining_dist -= radius;
        ++circle_count;
    }

    UE_LOG(LogBlueprint, Warning, TEXT("Created %d circles, smallest is %g, biggest is %g at distance %g"), circle_count, min_radius, max_radius, distance);
    zoom_interest->refresh();
}

void AInstantWeapon::RemoveZoomedQBI() {
    UE_LOG(LogBlueprint, Warning, TEXT("------------------------------------ Would remove zoomed QBI"));
}

void AInstantWeapon::DrawDebugSpheresOnly (float distance, AActor* character, float fov) {
    auto* const zoom_interest = Cast<UActorInterestComponent>(character->GetComponentByClass(UActorInterestComponent::StaticClass()));

    UE_LOG(LogBlueprint, Warning, TEXT("Drawing sphere sphere sphere sphere sphere sphere sphere sphere spher spher spher spher spher spher spher spher spher spher spher spher sphereeeeeeeeeeeee"));

    const float tan_half_fov = std::tan(FMath::DegreesToRadians(fov) / 2.0f);
    float remaining_dist = distance + radius_at_distance(tan_half_fov, distance);
    while (remaining_dist > 500.0f) {
        const float radius = radius_at_distance(tan_half_fov, remaining_dist);
        remaining_dist -= radius;
        const FVector location(character->GetActorForwardVector() * remaining_dist + character->GetActorLocation());

        DrawDebugSphere(
                GetWorld(),
                location,
                radius,
                30,
                FColor(0xb7, 0x10, 0x20, 0x60),
                false,
                75.0f,
                0x0,
                2.0f
        );

        remaining_dist -= radius;
    }
}
