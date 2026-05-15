#pragma once

#include "lua.h"
#include "lobject.h"

struct lua_State;
struct Closure;

LUAMOD_API int luaopen_oth(lua_State* L);

Closure* oth_gethooked(Closure* cl);

Closure* oth_makecclosure(lua_State* L, Closure* cl);