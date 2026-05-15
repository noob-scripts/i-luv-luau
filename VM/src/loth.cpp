// loth.cpp
// Advanced hooking library for Luau

#include "loth.h"

#include "lapi.h"
#include "laux.h"
#include "lfunc.h"
#include "lobject.h"
#include "lstate.h"
#include "lvm.h"

#include <unordered_map>

struct HookContext
{
    lua_State* originalThread;
    Closure* rootFunction;
    bool active;
};

static thread_local HookContext gHookCtx = {
    nullptr,
    nullptr,
    false
};

static std::unordered_map<Closure*, Closure*> gHooks;
static std::unordered_map<Closure*, Closure*> gOriginals;

static int oth_dispatch(lua_State* L)
{
    Closure* target = clvalue(index2addr(L, lua_upvalueindex(1)));

    if (!target)
        return 0;

    bool oldActive = gHookCtx.active;
    lua_State* oldThread = gHookCtx.originalThread;
    Closure* oldRoot = gHookCtx.rootFunction;

    gHookCtx.active = true;
    gHookCtx.originalThread = L;
    gHookCtx.rootFunction = target;

    int results = 0;

    if (target->isC)
    {
        results = target->c.f(L);
    }
    else
    {
        int nargs = lua_gettop(L);

        lua_pushvalue(L, lua_upvalueindex(1));

        for (int i = 1; i <= nargs; i++)
            lua_pushvalue(L, i);

        lua_call(L, nargs, LUA_MULTRET);

        results = lua_gettop(L);
    }

    gHookCtx.active = oldActive;
    gHookCtx.originalThread = oldThread;
    gHookCtx.rootFunction = oldRoot;

    return results;
}

Closure* oth_makecclosure(lua_State* L, Closure* cl)
{
    if (cl->isC)
        return cl;

    setclvalue(L, L->top, cl);
    incr_top(L);

    lua_pushcclosurek(L, oth_dispatch, "oth_dispatch", 1, NULL);

    Closure* wrapper = clvalue(index2addr(L, -1));

    L->top--;

    return wrapper;
}

static int oth_newcclosure(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);

    Closure* cl = clvalue(index2addr(L, 1));

    Closure* wrapper = oth_makecclosure(L, cl);

    setclvalue(L, L->top, wrapper);
    incr_top(L);

    return 1;
}

static int oth_hook(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    Closure* oldcl = clvalue(index2addr(L, 1));
    Closure* newcl = clvalue(index2addr(L, 2));

    if (oldcl->isC && !newcl->isC)
        newcl = oth_makecclosure(L, newcl);

    if (!gOriginals.count(oldcl))
        gOriginals[oldcl] = oldcl;

    gHooks[oldcl] = newcl;

    lua_pushvalue(L, 1);
    return 1;
}

static int oth_unhook(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);

    Closure* cl = clvalue(index2addr(L, 1));

    gHooks.erase(cl);

    return 0;
}

static int oth_ishooked(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);

    Closure* cl = clvalue(index2addr(L, 1));

    lua_pushboolean(L, gHooks.find(cl) != gHooks.end());

    return 1;
}

static int oth_gethook(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TFUNCTION);

    Closure* cl = clvalue(index2addr(L, 1));

    auto it = gHooks.find(cl);

    if (it == gHooks.end())
    {
        lua_pushnil(L);
        return 1;
    }

    setclvalue(L, L->top, it->second);
    incr_top(L);

    return 1;
}

static int oth_getrootcallback(lua_State* L)
{
    if (!gHookCtx.active || !gHookCtx.rootFunction)
    {
        lua_pushnil(L);
        return 1;
    }

    setclvalue(L, L->top, gHookCtx.rootFunction);
    incr_top(L);

    return 1;
}

static int oth_ishookthread(lua_State* L)
{
    lua_pushboolean(L, gHookCtx.active);
    return 1;
}

static int oth_getoriginalthread(lua_State* L)
{
    if (!gHookCtx.active || !gHookCtx.originalThread)
    {
        lua_pushnil(L);
        return 1;
    }

    lua_pushthread(gHookCtx.originalThread);
    return 1;
}

static int oth_hookmetamethod(lua_State* L)
{
    luaL_checkany(L, 1);

    const char* name = luaL_checkstring(L, 2);

    luaL_checktype(L, 3, LUA_TFUNCTION);

    if (!lua_getmetatable(L, 1))
        luaL_error(L, "object has no metatable");

    lua_getfield(L, -1, name);

    lua_pushvalue(L, 3);
    lua_setfield(L, -3, name);

    return 1;
}

static int oth_unhookmetamethod(lua_State* L)
{
    luaL_checkany(L, 1);

    const char* name = luaL_checkstring(L, 2);

    luaL_checktype(L, 3, LUA_TFUNCTION);

    if (!lua_getmetatable(L, 1))
        luaL_error(L, "object has no metatable");

    lua_pushvalue(L, 3);
    lua_setfield(L, -2, name);

    return 0;
}

Closure* oth_gethooked(Closure* cl)
{
    auto it = gHooks.find(cl);

    if (it != gHooks.end())
        return it->second;

    return cl;
}

static const luaL_Reg othlib[] = {
    {"hook", oth_hook},
    {"hookfunction", oth_hook},

    {"unhook", oth_unhook},
    {"restorefunction", oth_unhook},

    {"newcclosure", oth_newcclosure},

    {"ishooked", oth_ishooked},
    {"gethook", oth_gethook},

    {"get_root_callback", oth_getrootcallback},
    {"is_hook_thread", oth_ishookthread},
    {"get_original_thread", oth_getoriginalthread},

    {"hookmetamethod", oth_hookmetamethod},
    {"unhookmetamethod", oth_unhookmetamethod},

    {NULL, NULL},
};

int luaopen_oth(lua_State* L)
{
    luaL_register(L, "oth", othlib);

    lua_getfield(L, -1, "hook");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "hookfunction");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "hook");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "replaceclosure");

    lua_pop(L, 1);

    lua_getfield(L, -1, "restorefunction");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "restorefunction");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "unhook");

    lua_pop(L, 1);

    lua_getfield(L, -1, "hookmetamethod");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "hookmetamethod");

    lua_pop(L, 1);

    lua_getfield(L, -1, "unhookmetamethod");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "restoremetamethod");

    lua_pop(L, 1);

    lua_getfield(L, -1, "ishooked");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "ishooked");

    lua_pop(L, 1);

    lua_getfield(L, -1, "newcclosure");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "newcclosure");

    lua_pop(L, 1);

    return 1;
}
