#pragma once

#include <cstdint>

namespace redis
{

extern const char *tostring_format;

bool GetMetaField( lua_State *state, int32_t idx, const char *metafield );

}
