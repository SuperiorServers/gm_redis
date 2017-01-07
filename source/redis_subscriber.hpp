#pragma once

#include <cstdint>

struct lua_State;

namespace redis_subscriber
{

void Initialize( lua_State *state );
void Deinitialize( lua_State *state );
int32_t Create( lua_State *state ) noexcept;

}
