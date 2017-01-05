#include <GarrysMod/Lua/Interface.h>
#include <main.hpp>
#include <redis_client.hpp>

namespace redis
{

static const char *version = "redis 1.0.0";
static uint32_t version_number = 10000;
static const char *table_name = "redis";

#if defined _WIN32

const char *tostring_format = "%s: 0x%p";

#elif defined __linux || defined __APPLE__

const char *tostring_format = "%s: %p";

#endif

}

GMOD_MODULE_OPEN( )
{
	redis_client::Initialize( state );

	LUA->CreateTable( );

	LUA->PushString( redis::version );
	LUA->SetField( -2, "Version" );

	// version num follows LuaJIT style, xxyyzz
	LUA->PushNumber( redis::version_number );
	LUA->SetField( -2, "VersionNum" );

	LUA->PushCFunction( redis_client::Create );
	LUA->SetField( -2, "CreateClient" );

	LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, redis::table_name );

	return 0;
}

GMOD_MODULE_CLOSE( )
{
	LUA->PushNil( );
	LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, redis::table_name );

	redis_client::Deinitialize( state );

	return 0;
}
