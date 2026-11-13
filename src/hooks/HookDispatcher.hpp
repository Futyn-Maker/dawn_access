#pragma once
// Central hook installation and routing.
//  - One ProcessLocalScriptFunction post (and pre) callback routes Blueprint
//    function executions by (owner class name, function name) to handlers.
//  - Native UFunctions are hooked individually with UObjectGlobals::RegisterHook.
//  - StaticConstructObject post callbacks record new objects; class-filtered
//    listeners are notified on the game thread on the next tick.

#include <Unreal/FFrame.hpp>
#include <Unreal/UObject.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace da::hooks
{
    using RC::Unreal::FFrame;
    using RC::Unreal::UObject;

    using ScriptHandler = std::function<void(UObject* self, FFrame& stack)>;
    using ConstructedHandler = std::function<void(UObject* object)>;

    void Install();

    // Blueprint function routing. `ownerClass` is the short class name that declares
    // the function (e.g. "MenuButton_C"); subclasses inherit the match automatically.
    void OnScript(std::wstring ownerClass, std::wstring function, ScriptHandler handler);
    void OnScriptPre(std::wstring ownerClass, std::wstring function, ScriptHandler handler);

    // Native function hook by full name ("/Script/UMG.UserWidget:OnAddedToFocusPath").
    // Returns false (and logs) when the function does not exist in this build.
    bool OnNative(const std::wstring& fullName, ScriptHandler pre, ScriptHandler post);

    // The same, the post handler given where the function's return value lies (null when the call
    // gave it no place): what the game's own function answered.
    using ResultHandler = std::function<void(UObject* self, FFrame& stack, void* result)>;
    bool OnNativeResult(const std::wstring& fullName, ScriptHandler pre, ResultHandler post);

    // Object construction notifications, filtered by short class name (exact) or
    // by suffix when `suffix` is true. Delivered on the game thread.
    void OnConstructed(std::wstring className, bool suffix, ConstructedHandler handler);

    // Function tracer (dev): logs every Blueprint call whose owner class matches the configured prefixes.
    void SetTrace(bool enabled);
    bool TraceEnabled();

    // Last hook that ran (crash breadcrumb).
    std::wstring LastHook();

    // Statistics for the log header.
    size_t ScriptHandlerCount();

    // The game has freed the object at this address (core/Freed): the handlers found for it, as a
    // function, and the construction listeners it matched, as a class, are looked up anew. True when
    // anything was kept for it.
    bool Forget(const void* object);
}
