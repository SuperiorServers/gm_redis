#include <GarrysMod/Lua/Interface.h>
#include <main.hpp>
#include <redis_client.hpp>
#include <redis_subscriber.hpp>
#include <lua.hpp>

namespace redis
{

static const char *version = "redis 1.0.1";
static uint32_t version_number = 10001;
static const char *table_name = "redis";

#if defined _WIN32

const char *tostring_format = "%s: 0x%p";

#elif defined __linux || defined __APPLE__

const char *tostring_format = "%s: %p";

#endif

bool GetMetaField( GarrysMod::Lua::ILuaBase *LUA, int32_t idx, const char *metafield )
{
	LUA->GetMetaTable( idx );
	LUA->PushString( metafield );
	LUA->RawGet( -2 );
	if( LUA->IsType( -1, GarrysMod::Lua::Type::FUNCTION ) )
	{
		LUA->Remove( -2 );
		return true;
	}

	LUA->Pop( 2 );

	lua_getfenv( LUA->state, idx );
	LUA->PushString( metafield );
	LUA->RawGet( -2 );
	if( LUA->IsType( -1, GarrysMod::Lua::Type::FUNCTION ) )
	{
		LUA->Remove( -2 );
		return true;
	}

	LUA->Pop( 2 );
	return false;
}

}

GMOD_MODULE_OPEN( )
{
	redis_client::Initialize( LUA );
	redis_subscriber::Initialize( LUA );

	LUA->CreateTable( );

	LUA->PushString( redis::version );
	LUA->SetField( -2, "Version" );

	// version num follows LuaJIT style, xxyyzz
	LUA->PushNumber( redis::version_number );
	LUA->SetField( -2, "VersionNum" );

	LUA->PushCFunction( redis_client::Create );
	LUA->SetField( -2, "CreateClient" );

	LUA->PushCFunction( redis_subscriber::Create );
	LUA->SetField( -2, "CreateSubscriber" );

	LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, redis::table_name );

	return 0;
}

GMOD_MODULE_CLOSE( )
{
	LUA->PushNil( );
	LUA->SetField( GarrysMod::Lua::INDEX_GLOBAL, redis::table_name );

	redis_subscriber::Deinitialize( LUA );
	redis_client::Deinitialize( LUA );

	return 0;
}
