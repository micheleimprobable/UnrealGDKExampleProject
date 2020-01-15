#pragma once

#include <Containers/Array.h>
#include <Math/Vector.h>

class FrustumConstraintFiller {
public:
    struct Sphere {
        Sphere() = default;
        Sphere (const FVector& centre, float radius) :
            centre(centre), radius(radius)
        {
        }

        FVector centre;
        float radius;
    };

    struct Box {
        Box() = default;
        Box(float left, float top, float right, float bottom) :
            left(left), top(top), right(right), bottom(bottom)
        {
        }

        float left, top, right, bottom;
    };

    typedef TArray<Sphere> SphereList;
    typedef TArray<Box> BoxList;

    FrustumConstraintFiller (float fov, float near, float far);
    ~FrustumConstraintFiller() noexcept;

    SphereList sphere_filling(const FVector& from, const FVector& direction) const;
    BoxList box_filling (const FVector& from, const FVector& direction, int splits) const;

private:
    float m_fov;
    float m_near;
    float m_far;
};
