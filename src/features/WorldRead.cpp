#include "features/WorldRead.hpp"

#include "core/ObjectUtil.hpp"
#include "features/ExploreText.hpp"

#include <cmath>
#include <numbers>

using namespace RC::Unreal;

namespace da::features::world
{
    bool ReadVec(void* structPtr, FProperty* structProp, Vec& out)
    {
        if (!structPtr || !structProp) return false;
        return obj::ReadFloatAt(structPtr, obj::StructMember(structProp, L"X"), out.x) &&
               obj::ReadFloatAt(structPtr, obj::StructMember(structProp, L"Y"), out.y) &&
               obj::ReadFloatAt(structPtr, obj::StructMember(structProp, L"Z"), out.z);
    }

    void WriteVec(void* structPtr, FProperty* structProp, const Vec& v)
    {
        if (!structPtr || !structProp) return;
        const struct
        {
            const wchar_t* name;
            double value;
        } members[] = {{L"X", v.x}, {L"Y", v.y}, {L"Z", v.z}};
        for (const auto& m : members)
        {
            auto* prop = obj::StructMember(structProp, m.name);
            if (!prop) continue;
            const auto type = obj::PropertyTypeName(prop);
            if (type == L"FloatProperty")
                *static_cast<float*>(obj::ValuePtrAt(structPtr, prop)) = static_cast<float>(m.value);
            else if (type == L"DoubleProperty")
                *static_cast<double*>(obj::ValuePtrAt(structPtr, prop)) = m.value;
        }
    }

    bool VecReturn(UObject* target, const wchar_t* function, Vec& out)
    {
        bool ok = false;
        obj::CallReturn(target, function, [&](void* params, FProperty* returnValue) { ok = ReadVec(obj::ValuePtrAt(params, returnValue), returnValue, out); });
        return ok;
    }

    bool VecProperty(UObject* object, std::wstring_view name, Vec& out)
    {
        if (!obj::IsLive(object)) return false;
        FProperty* prop = obj::FindProperty(object, name);
        return prop && ReadVec(obj::ValuePtr(object, prop), prop, out);
    }

    bool ActorLocation(UObject* actor, Vec& out)
    {
        return obj::IsLive(actor) && VecReturn(actor, L"K2_GetActorLocation", out);
    }

    bool ComponentLocation(UObject* component, Vec& out)
    {
        return obj::IsLive(component) && VecReturn(component, L"K2_GetComponentLocation", out);
    }

    bool RotationOf(UObject* target, const wchar_t* function, double& pitch, double& yaw)
    {
        bool ok = false;
        obj::CallReturn(target, function,
                        [&](void* params, FProperty* returnValue)
                        {
                            void* value = obj::ValuePtrAt(params, returnValue);
                            if (value)
                                ok = obj::ReadFloatAt(value, obj::StructMember(returnValue, L"Pitch"), pitch) &&
                                     obj::ReadFloatAt(value, obj::StructMember(returnValue, L"Yaw"), yaw);
                        });
        return ok;
    }

    double BearingDegrees(const Vec& from, const Vec& to, double headingYaw)
    {
        const double toYaw = std::atan2(to.y - from.y, to.x - from.x) * 180.0 / std::numbers::pi;
        return explore::NormalizeDegrees(toYaw - headingYaw);
    }

    double ElevationDegrees(const Vec& from, const Vec& to)
    {
        return std::atan2(to.z - from.z, FlatDistance(from, to)) * 180.0 / std::numbers::pi;
    }

    double Distance(const Vec& a, const Vec& b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
    }

    double FlatDistance(const Vec& a, const Vec& b)
    {
        return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
    }

    double Dot(const Vec& a, const Vec& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    Vec Minus(const Vec& a, const Vec& b)
    {
        return Vec{a.x - b.x, a.y - b.y, a.z - b.z};
    }

    void ViewBasis(double pitch, double yaw, Vec& forward, Vec& right, Vec& up)
    {
        const double p = pitch * std::numbers::pi / 180.0;
        const double y = yaw * std::numbers::pi / 180.0;
        forward = Vec{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
        right = Vec{-std::sin(y), std::cos(y), 0.0};
        // forward x right points up for the engine's left-handed frame (x forward, y right, z up).
        up = Vec{forward.y * right.z - forward.z * right.y, forward.z * right.x - forward.x * right.z, forward.x * right.y - forward.y * right.x};
    }

    UObject* Controller()
    {
        return obj::LocalPlayerController();
    }

    UObject* Pawn()
    {
        UObject* controller = Controller();
        UObject* pawn = nullptr;
        if (controller) obj::ReadObject(controller, L"Pawn", pawn);
        return obj::IsLive(pawn) && obj::IsA(pawn, L"BatesCharacter") ? pawn : nullptr;
    }

    bool CameraLine(Vec& eye, double& pitch, double& yaw)
    {
        UObject* controller = Controller();
        UObject* camera = nullptr;
        if (!controller || !obj::ReadObject(controller, L"PlayerCameraManager", camera) || !obj::IsLive(camera)) return false;
        return ActorLocation(camera, eye) && RotationOf(camera, L"GetCameraRotation", pitch, yaw);
    }

    bool CameraYaw(double& yaw)
    {
        Vec eye;
        double pitch = 0.0;
        return CameraLine(eye, pitch, yaw);
    }

    bool ControlYaw(double& yaw)
    {
        UObject* controller = Controller();
        double pitch = 0.0;
        return controller && RotationOf(controller, L"GetControlRotation", pitch, yaw);
    }
}
