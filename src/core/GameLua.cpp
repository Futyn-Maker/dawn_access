#include "core/GameLua.hpp"

#include "core/Fault.hpp"
#include "core/GameThread.hpp"
#include "core/Log.hpp"
#include "core/ObjectUtil.hpp"
#include "core/Strings.hpp"
#include "core/Symbols.hpp"

#include <Unreal/UObject.hpp>

#include <windows.h>

#include <memory>
#include <string>

namespace da::lua
{
    namespace
    {
        using RC::Unreal::UObject;

        struct lua_State;
        // Lua 5.1's interface (the executable has lua_getfenv and luaL_register, and no LuaJIT),
        // with lua_Number a float in this build: lua_tonumber loads its result with movss.
        using GetTop = int (*)(lua_State*);
        using SetTop = void (*)(lua_State*, int);
        using GetField = void (*)(lua_State*, int, const char*);
        using Type = int (*)(lua_State*, int);
        using ToNumber = float (*)(lua_State*, int);
        using ToBoolean = int (*)(lua_State*, int);
        using CheckStack = int (*)(lua_State*, int);
        using RawGetI = void (*)(lua_State*, int, int);
        using ToLString = const char* (*)(lua_State*, int, size_t*);

        constexpr int kGlobalsIndex = -10002; // LUA_GLOBALSINDEX
        constexpr int kNil = 0, kBoolean = 1, kNumber = 3, kString = 4, kTable = 5;

        struct Api
        {
            GetTop getTop = nullptr;
            SetTop setTop = nullptr;
            GetField getField = nullptr;
            Type type = nullptr;
            ToNumber toNumber = nullptr;
            ToBoolean toBoolean = nullptr;
            CheckStack checkStack = nullptr;
            RawGetI rawGetI = nullptr;
            ToLString toLString = nullptr;
            bool Complete() const
            {
                return getTop && setTop && getField && type && toNumber && toBoolean && checkStack && rawGetI && toLString;
            }
        };

        const Api& Library()
        {
            static const Api api = []
            {
                Api a;
                a.getTop = reinterpret_cast<GetTop>(sym::Address(L"lua_gettop"));
                a.setTop = reinterpret_cast<SetTop>(sym::Address(L"lua_settop"));
                a.getField = reinterpret_cast<GetField>(sym::Address(L"lua_getfield"));
                a.type = reinterpret_cast<Type>(sym::Address(L"lua_type"));
                a.toNumber = reinterpret_cast<ToNumber>(sym::Address(L"lua_tonumber"));
                a.toBoolean = reinterpret_cast<ToBoolean>(sym::Address(L"lua_toboolean"));
                a.checkStack = reinterpret_cast<CheckStack>(sym::Address(L"lua_checkstack"));
                a.rawGetI = reinterpret_cast<RawGetI>(sym::Address(L"lua_rawgeti"));
                a.toLString = reinterpret_cast<ToLString>(sym::Address(L"lua_tolstring"));
                log::Info(L"lua: the game's Lua library is {}", a.Complete() ? L"found" : L"not found, so no script's globals are read");
                return a;
            }();
            return api;
        }

        struct Read
        {
            const Api* api = nullptr;
            lua_State* state = nullptr;
            const char* table = nullptr;
            const char* field = nullptr;
            const char* key = nullptr; // a field of the field, or none
            Value out;
        };

        // The value on the top of the stack, as the scripts' comparisons see it.
        void TakeTop(const Api& a, lua_State* state, Value& out)
        {
            const int type = a.type(state, -1);
            if (type == kNil)
            {
                out.kind = Value::Kind::Nil;
            }
            else if (type == kNumber)
            {
                out.kind = Value::Kind::Number;
                out.number = a.toNumber(state, -1);
            }
            else if (type == kBoolean)
            {
                out.kind = Value::Kind::Boolean;
                out.boolean = a.toBoolean(state, -1) != 0;
            }
            else
            {
                out.kind = Value::Kind::Other;
            }
        }

        // Under the fault boundary: no C++ object lives here.
        void ReadField(void* context)
        {
            auto& r = *static_cast<Read*>(context);
            const Api& a = *r.api;
            const int top = a.getTop(r.state);
            if (!a.checkStack(r.state, 4)) return;
            a.getField(r.state, kGlobalsIndex, r.table);
            if (a.type(r.state, -1) == kTable)
            {
                a.getField(r.state, -1, r.field);
                if (!r.key)
                    TakeTop(a, r.state, r.out);
                else if (a.type(r.state, -1) == kTable)
                {
                    a.getField(r.state, -1, r.key);
                    TakeTop(a, r.state, r.out);
                }
                else if (a.type(r.state, -1) == kNil)
                    r.out.kind = Value::Kind::Nil;
                else
                    r.out.kind = Value::Kind::Other;
            }
            a.setTop(r.state, top);
        }
    }

    namespace
    {
        constexpr int kMostMachines = 256;
        constexpr int kNameMax = 96;
        struct MachinesRead
        {
            const Api* api = nullptr;
            lua_State* state = nullptr;
            int count = 0;
            char names[kMostMachines][kNameMax] = {};
            int states[kMostMachines] = {};
        };

        void ReadMachines(void* context)
        {
            auto& r = *static_cast<MachinesRead*>(context);
            const Api& a = *r.api;
            const int top = a.getTop(r.state);
            if (!a.checkStack(r.state, 6)) return;
            a.getField(r.state, kGlobalsIndex, "gStateMachines");
            if (a.type(r.state, -1) == kTable)
            {
                for (int i = 1; i <= kMostMachines; ++i)
                {
                    a.rawGetI(r.state, -1, i);
                    if (a.type(r.state, -1) != kTable)
                    {
                        a.setTop(r.state, -2);
                        break;
                    }
                    a.getField(r.state, -1, "name");
                    a.getField(r.state, -2, "currentState");
                    if (a.type(r.state, -2) == kString && a.type(r.state, -1) == kNumber && r.count < kMostMachines)
                    {
                        size_t length = 0;
                        const char* name = a.toLString(r.state, -2, &length);
                        if (name)
                        {
                            const size_t n = length < kNameMax - 1 ? length : kNameMax - 1;
                            for (size_t c = 0; c < n; ++c)
                                r.names[r.count][c] = name[c];
                            r.names[r.count][n] = 0;
                            r.states[r.count] = static_cast<int>(a.toNumber(r.state, -1));
                            ++r.count;
                        }
                    }
                    a.setTop(r.state, -4); // the name, the state and the machine
                }
            }
            a.setTop(r.state, top);
        }
    }

    bool Machines(std::vector<Machine>& out)
    {
        out.clear();
        if (!gamethread::IsGameThread()) return false;
        const Api& api = Library();
        if (!api.Complete()) return false;
        static obj::ObjectKey s_updater;
        if (!obj::IsSame(s_updater))
        {
            UObject* found = obj::FindFirstLive(L"BatesLuaScriptUpdater");
            if (!found) return false;
            s_updater = obj::KeyOf(found);
        }
        UObject* script = nullptr;
        if (!obj::ReadObject(s_updater.object, L"RunningScript", script) || !obj::IsLive(script)) return false;
        auto* const* state = static_cast<lua_State* const*>(sym::MemberOf(script, L"UScript.mScriptData"));
        auto* lock = static_cast<CRITICAL_SECTION*>(sym::MemberOf(script, L"UScript.mScriptAccessMutex"));
        if (!state || !*state || !lock) return false;
        if (!TryEnterCriticalSection(lock)) return false;
        auto read = std::make_unique<MachinesRead>();
        read->api = &api;
        read->state = *state;
        const bool ran = fault::InvokeLogged(L"lua.Machines", &ReadMachines, read.get());
        LeaveCriticalSection(lock);
        if (!ran) return false;
        for (int i = 0; i < read->count; ++i)
            out.push_back(Machine{str::Utf8ToWide(read->names[i]), read->states[i]});
        return true;
    }

    namespace
    {
        // A field (and a field of it) of a global table of the running script, under its lock.
        Value ReadPath(std::string_view table, std::string_view field, const std::string* key)
        {
            Value none;
            if (!gamethread::IsGameThread()) return none;
            const Api& api = Library();
            if (!api.Complete()) return none;
            // The updater is a subsystem of the game instance: found once, asked again only when gone.
            static obj::ObjectKey s_updater;
            if (!obj::IsSame(s_updater))
            {
                UObject* found = obj::FindFirstLive(L"BatesLuaScriptUpdater");
                if (!found) return none;
                s_updater = obj::KeyOf(found);
            }
            UObject* updater = s_updater.object;
            UObject* script = nullptr;
            if (!updater || !obj::ReadObject(updater, L"RunningScript", script) || !obj::IsLive(script)) return none;
            auto* const* state = static_cast<lua_State* const*>(sym::MemberOf(script, L"UScript.mScriptData"));
            auto* lock = static_cast<CRITICAL_SECTION*>(sym::MemberOf(script, L"UScript.mScriptAccessMutex"));
            if (!state || !*state || !lock) return none;
            if (!TryEnterCriticalSection(lock)) return none;
            const std::string t(table), f(field);
            Read read{&api, *state, t.c_str(), f.c_str(), key ? key->c_str() : nullptr, {}};
            const bool ran = fault::InvokeLogged(L"lua.Global", &ReadField, &read);
            LeaveCriticalSection(lock);
            return ran ? read.out : none;
        }
    }

    Value Global(std::string_view table, std::string_view field)
    {
        return ReadPath(table, field, nullptr);
    }

    Value Nested(std::string_view table, std::string_view field, std::string_view key)
    {
        const std::string k(key);
        return ReadPath(table, field, &k);
    }
}
