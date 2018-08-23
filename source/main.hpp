#pragma once

#include <cstdint>

namespace redis
{

extern const char tostring_format[];

bool GetMetaField( GarrysMod::Lua::ILuaBase *LUA, int32_t idx, const char *metafield );

}
