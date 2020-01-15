#include "FrustumConstraintFiller.h"
#include "GDKLogging.h"
#include "Kismet/GameplayStatics.h"
#include <cmath>
#include <ciso646>
#include <tuple>
#include <algorithm>
#include <EngineGlobals.h>

//DECLARE_STATS_GROUP(TEXT("ZoomConstraints"), STATGROUP_ZoomConstraints, STATCAT_Advanced);

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

FrustumConstraintFiller::FrustumConstraintFiller (float fov, float near, float far) :
    m_fov(fov),
    m_near(near),
    m_far(far)
{
}

FrustumConstraintFiller::~FrustumConstraintFiller() noexcept = default;

auto FrustumConstraintFiller::sphere_filling (const FVector& from, const FVector& direction) const -> SphereList {
    //DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ZoomConstraints"), STATGROUP_ZoomConstraints, STATCAT_Advanced);

    SphereList retval;
    unsigned int circle_count = 0;
    float max_radius = 0.0f, min_radius = 0.0f;
    const float tan_half_fov = std::tan(FMath::DegreesToRadians(m_fov) / 2.0f);
    float remaining_dist = m_far + radius_at_distance(tan_half_fov, m_far);
    UE_LOG(LogBlueprint, Warning, TEXT("Orig radius lolol: %f"), radius_at_distance(tan_half_fov, m_far));
    while (remaining_dist > m_near) {
        const float radius = radius_at_distance(tan_half_fov, remaining_dist);
        remaining_dist -= radius;

        retval.Emplace(direction * remaining_dist + from, radius);

        //UE_LOG(LogBlueprint, Warning, TEXT("Setting up zoomed QBI to <%g, %g, %g>, dist=%g, rad=%g"),
        //       location.X, location.Y, location.Z,
        //       remaining_dist,
        //       new_sphere->Radius
        //);
        if (0 == circle_count)
            max_radius = radius;
        min_radius = radius;
        remaining_dist -= radius;
        ++circle_count;
    }

    UE_LOG(LogBlueprint, Warning, TEXT("Created %d circles, smallest is %g, biggest is %g at distance %g"),
           circle_count, min_radius, max_radius, m_far);
    return retval;
}

auto FrustumConstraintFiller::box_filling (const FVector& from, const FVector& direction, int splits) const -> BoxList {
    BoxList retval;

    const float& distance = m_far;
    const auto[sin_half_fov, cos_half_fov] = sin_cos(FMath::DegreesToRadians(m_fov) / 2.0f);
    const auto facing_proj_2d = FVector(direction.X, direction.Y, 0.0f).GetSafeNormal();
    const float sin_a = FVector::CrossProduct(FVector(1.0f, 0.0f, 0.0f), facing_proj_2d).Z;
    const float cos_a = FVector::DotProduct(FVector(1.0f, 0.0f, 0.0f), facing_proj_2d);
    const float cos_lower_angle = cos_a * cos_half_fov + sin_a * sin_half_fov;
    const float sin_upper_angle = sin_a * cos_half_fov + cos_a * sin_half_fov;
    const float i = distance * cos_half_fov;
    const float fov_base = distance * std::tan(FMath::DegreesToRadians(m_fov) / 2.0f) * 2.0f;
    const float bounding_rect_h = std::max(FMath::Abs(i * sin_upper_angle), fov_base);
    const float bounding_rect_w = std::max(FMath::Abs(i * cos_lower_angle), fov_base);

    const float waste_ratio =
            (bounding_rect_w * bounding_rect_h - fov_base * distance / 2.0f) / (bounding_rect_w * bounding_rect_h);

    UE_LOG(LogBlueprint, Warning,
           TEXT("Bounding rect facing <%g, %g, %g> is %fx%f, A=%f,\nfovbase=%f, A=%f, i=%f, waste=%f%%, lowercos=%f, uppersin=%f\ncos_=%f, sin_a=%f"),
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
    } else {
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
            retval.Emplace(left, top, right, bottom);
        }
    }
    return retval;
}
