#pragma once
// What the mod reads of the world through the engine's own reflection: the places and
// directions of actors, components and the camera, and the little geometry the readings are
// compared with. Shared by the features that lead the player through the world.
#include "features/Vec.hpp"

#include <optional>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
    class FProperty;
}

namespace da::features::world
{
    // A vector struct read from or written into a property's value (float or double members).
    bool ReadVec(void* structPtr, RC::Unreal::FProperty* structProp, Vec& out);
    void WriteVec(void* structPtr, RC::Unreal::FProperty* structProp, const Vec& v);
    // The vector a function of the object returns, or a vector property of it.
    bool VecReturn(RC::Unreal::UObject* target, const wchar_t* function, Vec& out);
    bool VecProperty(RC::Unreal::UObject* object, std::wstring_view name, Vec& out);

    bool ActorLocation(RC::Unreal::UObject* actor, Vec& out);
    bool ComponentLocation(RC::Unreal::UObject* component, Vec& out);
    // The pitch and yaw a rotator-returning function of the object gives.
    bool RotationOf(RC::Unreal::UObject* target, const wchar_t* function, double& pitch, double& yaw);

    // Degrees to the right of a heading, from one point to another (the engine's yaw turns
    // clockwise seen from above).
    double BearingDegrees(const Vec& from, const Vec& to, double headingYaw);
    // Degrees a point stands above the level line from a spot.
    double ElevationDegrees(const Vec& from, const Vec& to);
    double Distance(const Vec& a, const Vec& b);
    double FlatDistance(const Vec& a, const Vec& b);
    double Dot(const Vec& a, const Vec& b);
    Vec Minus(const Vec& a, const Vec& b);
    // The frame a view with this pitch and yaw looks in: its forward, right and up.
    void ViewBasis(double pitch, double yaw, Vec& forward, Vec& right, Vec& up);

    RC::Unreal::UObject* Controller();
    // The player's character: the pawn the controller drives, when it is one of the game's
    // characters.
    RC::Unreal::UObject* Pawn();
    // The camera the player sees through: where it is and which way it points.
    bool CameraLine(Vec& eye, double& pitch, double& yaw);
    // Whether a point of the world lies in the player's picture, where the game's own projection puts
    // it (APlayerController::ProjectWorldLocationToScreen relative to the player's viewport, against
    // GetViewportSize): false behind the camera; nothing when it cannot be asked.
    std::optional<bool> InView(const Vec& point);
    bool CameraYaw(double& yaw);
    // The frame the game reads the player's movement in: the controller's control rotation,
    // which the exploration camera keeps at the camera's own.
    bool ControlYaw(double& yaw);
}
