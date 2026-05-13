// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "lualib.h"

#include "lvm.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static lua_State* getthread(lua_State* L, int* arg)
{
    if (lua_isthread(L, 1))
    {
        *arg = 1;
        return lua_tothread(L, 1);
    }
    else
    {
        *arg = 0;
        return L;
    }
}

static int db_getinfo(lua_State* L)
{
    lua_State* L1;
    int arg = 0;

    L1 = getthread(L, &arg);

    int level;
    const char* what = "flnSu";

    if (lua_isfunction(L, arg + 1))
    {
        lua_pushvalue(L, arg + 1);
        level = -lua_gettop(L); // pseudo-level (function mode)
    }
    else
    {
        level = (int)luaL_checkinteger(L, arg + 1);
    }

    lua_Debug ar;
    if (!lua_getinfo(L1, level, what, &ar))
        return 0;

    lua_newtable(L);

    lua_pushstring(L, ar.source);
    lua_setfield(L, -2, "source");

    lua_pushstring(L, ar.short_src);
    lua_setfield(L, -2, "short_src");

    lua_pushinteger(L, ar.currentline);
    lua_setfield(L, -2, "currentline");

    lua_pushinteger(L, ar.linedefined);
    lua_setfield(L, -2, "linedefined");

    lua_pushstring(L, ar.what);
    lua_setfield(L, -2, "what");

    lua_pushinteger(L, ar.nparams);
    lua_setfield(L, -2, "nparams");

    lua_pushboolean(L, ar.isvararg);
    lua_setfield(L, -2, "isvararg");

    if (ar.name)
    {
        lua_pushstring(L, ar.name);
        lua_setfield(L, -2, "name");
    }

    return 1;
}

static int db_info(lua_State* L)
{
    int arg;
    lua_State* L1 = getthread(L, &arg);
    int l1top = 0;

    // if L1 != L, L1 can be in any state, and therefore there are no guarantees about its stack space
    if (L != L1)
    {
        // for 'f' option, we reserve one slot and we also record the stack top
        lua_rawcheckstack(L1, 1);

        l1top = lua_gettop(L1);
    }

    int level;
    if (lua_isnumber(L, arg + 1))
    {
        level = (int)lua_tointeger(L, arg + 1);
        luaL_argcheck(L, level >= 0, arg + 1, "level can't be negative");
    }
    else if (arg == 0 && lua_isfunction(L, 1))
    {
        // convert absolute index to relative index
        level = -lua_gettop(L);
    }
    else
        luaL_argerror(L, arg + 1, "function or level expected");

    const char* options = luaL_checkstring(L, arg + 2);

    lua_Debug ar;
    if (!lua_getinfo(L1, level, options, &ar))
        return 0;

    int results = 0;
    bool occurs[26] = {};

    for (const char* it = options; *it; ++it)
    {
        if (unsigned(*it - 'a') < 26)
        {
            if (occurs[*it - 'a'])
            {
                // restore stack state of another thread as 'f' option might not have been visited yet
                if (L != L1)
                    lua_settop(L1, l1top);

                luaL_argerror(L, arg + 2, "duplicate option");
            }
            occurs[*it - 'a'] = true;
        }

        switch (*it)
        {
        case 's':
            lua_pushstring(L, ar.short_src);
            results++;
            break;

        case 'l':
            lua_pushinteger(L, ar.currentline);
            results++;
            break;

        case 'n':
            lua_pushstring(L, ar.name ? ar.name : "");
            results++;
            break;

        case 'f':
            if (L1 == L)
                lua_pushvalue(L, -1 - results); // function is right before results
            else
                lua_xmove(L1, L, 1); // function is at top of L1
            results++;
            break;

        case 'a':
            lua_pushinteger(L, ar.nparams);
            lua_pushboolean(L, ar.isvararg);
            results += 2;
            break;

        default:
            // restore stack state of another thread as 'f' option might not have been visited yet
            if (L != L1)
                lua_settop(L1, l1top);

            luaL_argerror(L, arg + 2, "invalid option");
        }
    }

    return results;
}

static int db_traceback(lua_State* L)
{
    int arg;
    lua_State* L1 = getthread(L, &arg);
    const char* msg = luaL_optstring(L, arg + 1, NULL);
    int level = luaL_optinteger(L, arg + 2, (L == L1) ? 1 : 0);
    luaL_argcheck(L, level >= 0, arg + 2, "level can't be negative");

    luaL_traceback(L, L1, msg, level);

    return 1;
}

static int db_getupvalue(lua_State* L)
{
    Closure* cl = (Closure*)luaA_toobject(L, 1);
    luaL_argexpected(L, cl->isC == 0, 1, "Lua function");

    int idx = (int)luaL_checkinteger(L, 2) - 1;

    if (idx < 0 || idx >= cl->nupvalues)
        return 0;

    UpVal* uv = cl->l.uprefs[idx];

    const char* name =
        (cl->l.p && cl->l.p->upvalues && idx < cl->l.p->sizeupvalues)
            ? getstr(cl->l.p->upvalues[idx])
            : "";

    TValue* v = (uv->v == &uv->u.value) ? &uv->u.value : uv->v;

    setobj2s(L, L->top, v);
    incr_top(L);

    lua_pushstring(L, name);
    return 2;
}

static int db_getupvalues(lua_State* L)
{
    Closure* cl = (Closure*)luaA_toobject(L, 1);
    luaL_argexpected(L, cl->isC == 0, 1, "Lua function");

    lua_newtable(L);

    for (int i = 0; i < cl->nupvalues; i++)
    {
        UpVal* uv = cl->l.uprefs[i];
        TValue* v = (uv->v == &uv->u.value) ? &uv->u.value : uv->v;

        lua_pushinteger(L, i + 1);
        setobj2s(L, L->top, v);
        incr_top(L);
        lua_settable(L, -3);
    }

    return 1;
}

static int db_setupvalue(lua_State* L)
{
    Closure* cl = (Closure*)luaA_toobject(L, 1);
    luaL_argexpected(L, cl->isC == 0, 1, "Lua function");

    int idx = (int)luaL_checkinteger(L, 2) - 1;

    if (idx < 0 || idx >= cl->nupvalues)
        return 0;

    luaL_checkany(L, 3);

    UpVal* uv = cl->l.uprefs[idx];

    TValue* target = (uv->v == &uv->u.value) ? &uv->u.value : uv->v;

    setobj(L, target, L->top - 1);

    const char* name =
        (cl->l.p && cl->l.p->upvalues && idx < cl->l.p->sizeupvalues)
            ? getstr(cl->l.p->upvalues[idx])
            : "";

    L->top--;

    lua_pushstring(L, name);
    return 1;
}

static int db_getconstant(lua_State* L)
{
    Closure* cl = (Closure*)luaA_toobject(L, 1);
    luaL_argexpected(L, cl->isC == 0, 1, "Lua function");

    Proto* p = cl->l.p;

    int idx = (int)luaL_checkinteger(L, 2) - 1;

    if (!p || !p->k || idx < 0 || idx >= p->sizek)
        return 0;

    setobj2s(L, L->top, &p->k[idx]);
    incr_top(L);

    return 1;
}

static int db_getconstants(lua_State* L)
{
    Closure* cl = (Closure*)luaA_toobject(L, 1);
    luaL_argexpected(L, cl->isC == 0, 1, "Lua function");

    Proto* p = cl->l.p;
    if (!p || !p->k)
        return 0;

    lua_newtable(L);

    for (int i = 0; i < p->sizek; i++)
    {
        lua_pushinteger(L, i + 1);
        setobj2s(L, L->top, &p->k[i]);
        incr_top(L);
        lua_settable(L, -3);
    }

    return 1;
}

static int db_getmetatable(lua_State* L)
{
    luaL_checkany(L, 1);

    if (!lua_getmetatable(L, 1))
    {
        lua_pushnil(L);
        return 1;
    }

    return 1;
}

static int db_setmetatable(lua_State* L)
{
    int t = lua_type(L, 2);
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_argexpected(L, t == LUA_TNIL || t == LUA_TTABLE, 2, "nil or table");
    lua_settop(L, 2);
    lua_setmetatable(L, 1);
    return 1;
}

static int db_getregistry(lua_State* L)
{
    lua_pushvalue(L, LUA_REGISTRYINDEX);
    return 1;
}

static const luaL_Reg dblib[] = {
    {"getinfo", db_getinfo},
    {"info", db_info},
    {"traceback", db_traceback},
    {"getconstant", db_getconstant},
    {"getconstants", db_getconstants},
    {"getupvalue", db_getupvalue},
    {"getupvalues", db_getupvalues},
    {"setupvalue", db_setupvalue},
    {"setmetatable", db_setmetatable},
    {"getmetatable", db_getmetatable},
    {"getregistry", db_getregistry},
    {NULL, NULL},
};

int luaopen_debug(lua_State* L)
{
    luaL_register(L, LUA_DBLIBNAME, dblib);
    return 1;
}
