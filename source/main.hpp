#pragma once

#include <cstdint>


namespace redis
{

extern const char tostring_format[];

extern int iRefErrorNoHalt;
extern int iRefDebugTraceBack;

void ErrorNoHalt(GarrysMod::Lua::ILuaBase* LUA, const char* msg);


bool GetMetaField( GarrysMod::Lua::ILuaBase *LUA, int32_t idx, const char *metafield );

}
