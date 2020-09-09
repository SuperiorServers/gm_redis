#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaInterface.h>
#include <cpp_redis/cpp_redis>
#include <cstdint>
#include <vector>
#include <memory>
#include "readerwriterqueue.hpp"
#include "redis_client.hpp"
#include "main.hpp"

namespace redis_client
{

enum class Action
{
	Disconnection,
	Reply,
	Publish
};

struct Response
{
	Action type;
	cpp_redis::reply reply;
	int32_t reference;
};

class Container
{
public:
	cpp_redis::client &GetClient( )
	{
		return client;
	}

	bool EnqueueResponse( const Response &response )
	{
		return queue.enqueue( response );
	}

	bool DequeueResponse( Response &response )
	{
		return queue.try_dequeue( response );
	}

private:
	cpp_redis::client client;
	moodycamel::ReaderWriterQueue<Response> queue;
};

static const char metaname[] = "redis_client";
static int32_t metatype = GarrysMod::Lua::Type::NONE;
static const char invalid_error[] = "invalid redis_client";
static const char table_name[] = "redis_clients";

LUA_FUNCTION( Create )
{
	Container *container = nullptr;
	try
	{
		container = new Container;
	}
	catch( const cpp_redis::redis_error &e )
	{
		LUA->PushNil( );
		LUA->PushString( e.what( ) );
		return 2;
	}

	LUA->PushUserType( container, metatype );

	LUA->PushMetaTable( metatype );
	LUA->SetMetaTable( -2 );

	LUA->CreateTable( );
	lua_setfenv( LUA->GetState( ), -2 );

	LUA->GetField( GarrysMod::Lua::INDEX_REGISTRY, table_name );
	LUA->PushUserdata( &container->GetClient( ) );
	LUA->Push( -3 );
	LUA->SetTable( -4 );
	LUA->Pop( );

	return 1;
}

inline void CheckType( GarrysMod::Lua::ILuaBase *LUA, int32_t index )
{
	if( !LUA->IsType( index, metatype ) )
		luaL_typerror( LUA->GetState( ), index, metaname );
}

inline Container *GetUserData( GarrysMod::Lua::ILuaBase *LUA, int index )
{
	return LUA->GetUserType<Container>( index, metatype );
}

static cpp_redis::client *Get( GarrysMod::Lua::ILuaBase *LUA, int32_t index, Container **container = nullptr )
{
	CheckType( LUA, index );
	Container *udata = GetUserData( LUA, index );
	if( udata == nullptr )
		LUA->ArgError( index, invalid_error );

	if( container != nullptr )
		*container = udata;

	return &udata->GetClient( );
}

LUA_FUNCTION_STATIC( tostring )
{
	LUA->PushFormattedString( redis::tostring_format, metaname, Get( LUA, 1 ) );
	return 1;
}

LUA_FUNCTION_STATIC( eq )
{
	LUA->PushBool( Get( LUA, 1 ) == Get( LUA, 2 ) );
	return 1;
}

LUA_FUNCTION_STATIC( index )
{
	CheckType( LUA, 1 );

	LUA->PushMetaTable( metatype );
	LUA->Push( 2 );
	LUA->RawGet( -2 );
	if( !LUA->IsType( -1, GarrysMod::Lua::Type::NIL ) )
		return 1;

	LUA->Pop( 2 );

	lua_getfenv( LUA->GetState( ), 1 );
	LUA->Push( 2 );
	LUA->RawGet( -2 );
	return 1;
}

LUA_FUNCTION_STATIC( newindex )
{
	CheckType( LUA, 1 );

	lua_getfenv( LUA->GetState( ), 1 );
	LUA->Push( 2 );
	LUA->Push( 3 );
	LUA->RawSet( -3 );
	return 0;
}

LUA_FUNCTION_STATIC( gc )
{
	Container *udata = GetUserData( LUA, 1 );
	if( udata == nullptr )
		return 0;

	delete udata;

	LUA->SetUserType( 1, nullptr );

	return 0;
}

LUA_FUNCTION_STATIC( IsValid )
{
	cpp_redis::client *client = Get( LUA, 1 );
	LUA->PushBool( client != nullptr );
	return 1;
}

LUA_FUNCTION_STATIC( IsConnected )
{
	cpp_redis::client *client = Get( LUA, 1 );
	LUA->PushBool( client->is_connected( ) );
	return 1;
}

LUA_FUNCTION_STATIC( Connect )
{
	Container *container = nullptr;
	cpp_redis::client *client = Get( LUA, 1, &container );
	const char *host = LUA->CheckString( 2 );
	size_t port = static_cast<size_t>( LUA->CheckNumber( 3 ) );

	try
	{
		client->connect( host, port, [container]( auto, auto, auto status )
		{
			using state = cpp_redis::connect_state;
			if( status == state::dropped || status == state::failed ||
				status == state::lookup_failed || status == state::stopped )
				container->EnqueueResponse( { Action::Disconnection } );
		} );
	}
	catch( const cpp_redis::redis_error &e )
	{
		LUA->PushNil( );
		LUA->PushString( e.what( ) );
		return 2;
	}

	LUA->PushBool( true );
	return 1;
}

LUA_FUNCTION_STATIC( Disconnect )
{
	cpp_redis::client *client = Get( LUA, 1 );
	client->disconnect( );
	return 0;
}

inline void BuildTable( GarrysMod::Lua::ILuaBase *LUA, const std::vector<cpp_redis::reply> &replies )
{
	LUA->CreateTable( );

	for( size_t k = 0; k < replies.size( ); ++k )
	{
		LUA->PushNumber( static_cast<double>( k ) );
		
		const cpp_redis::reply &reply = replies[k];
		switch( reply.get_type( ) )
		{
		case cpp_redis::reply::type::error:
		case cpp_redis::reply::type::bulk_string:
		case cpp_redis::reply::type::simple_string:
			LUA->PushString( reply.as_string( ).c_str( ) );
			break;

		case cpp_redis::reply::type::integer:
			LUA->PushNumber( static_cast<double>( reply.as_integer( ) ) );
			break;

		case cpp_redis::reply::type::array:
			BuildTable( LUA, reply.as_array( ) );
			break;

		case cpp_redis::reply::type::null:
			LUA->PushNil( );
			break;
		}

		LUA->SetTable( -3 );
	}
}

LUA_FUNCTION_STATIC( Poll )
{
	Container *container = nullptr;
	Get( LUA, 1, &container );

	bool had_responses = false;
	Response response;
	while( container->DequeueResponse( response ) )
	{		
		LUA->ReferencePush(redis::iRefDebugTraceBack);

		switch( response.type )
		{
		case Action::Disconnection:
			if (!redis::GetMetaField(LUA, 1, "OnDisconnected"))
			{
				LUA->Pop();
				break;
			}

			LUA->Push( 1 );
			if (LUA->PCall(1, 1, -3) != 0)
				redis::ErrorNoHalt(LUA, "[redis client callback error] ");

			break;

		case Action::Reply:
			LUA->ReferencePush( response.reference );
			LUA->Push( 1 );

			switch( response.reply.get_type( ) )
			{
			case cpp_redis::reply::type::error:
			case cpp_redis::reply::type::bulk_string:
			case cpp_redis::reply::type::simple_string:
				LUA->PushString( response.reply.as_string( ).c_str( ) );
				break;

			case cpp_redis::reply::type::integer:
				LUA->PushNumber( static_cast<double>( response.reply.as_integer( ) ) );
				break;

			case cpp_redis::reply::type::array:
				BuildTable( LUA, response.reply.as_array( ) );
				break;

			case cpp_redis::reply::type::null:
				LUA->PushNil( );
				break;
			}

			if (LUA->PCall(2, 0, -4) != 0)
				redis::ErrorNoHalt(LUA, "[redis client callback error] ");

			LUA->ReferenceFree( response.reference );

			break;
		default:
			LUA->Pop();
		}

		had_responses = true;
	}
	
	LUA->Pop();

	LUA->PushBool( had_responses );
	return 1;
}

inline const char *ToString( GarrysMod::Lua::ILuaBase *LUA, int32_t idx, size_t *len = nullptr )
{
	if( LUA->CallMeta( idx, "__tostring" ) == 0 )
		switch( LUA->GetType( idx ) )
		{
		case GarrysMod::Lua::Type::NUMBER:
			LUA->PushFormattedString( "%f", LUA->GetNumber( idx ) );
			break;

		case GarrysMod::Lua::Type::STRING:
			LUA->Push( idx );
			break;

		case GarrysMod::Lua::Type::BOOL:
			LUA->PushString( LUA->GetBool( idx ) ? "true" : "false" );
			break;

		case GarrysMod::Lua::Type::NIL:
			LUA->PushString( "nil" );
			break;

		default:
			LUA->PushFormattedString( "%s: %p", LUA->GetTypeName( LUA->GetType( idx ) ), lua_topointer( LUA->GetState( ), idx ) );
			break;
		}

	return LUA->GetString( -1, len );
}

LUA_FUNCTION_STATIC( Send )
{
	Container *container = nullptr;
	cpp_redis::client *client = Get( LUA, 1, &container );

	int32_t reference = GarrysMod::Lua::Type::NONE;
	if( LUA->Top( ) >= 3 && !LUA->IsType( 3, GarrysMod::Lua::Type::NIL ) )
	{
		LUA->CheckType( 3, GarrysMod::Lua::Type::FUNCTION );
		LUA->Push( 3 );
		reference = LUA->ReferenceCreate( );
	}

	{
		std::vector<std::string> keys;
		if( LUA->IsType( 2, GarrysMod::Lua::Type::TABLE ) )
		{
			int32_t k = 1;
			do
			{
				LUA->PushNumber( k );
				LUA->GetTable( 2 );
				if( LUA->IsType( -1, GarrysMod::Lua::Type::NIL ) )
				{
					LUA->Pop( );
					break;
				}

				keys.push_back( ToString( LUA, -1 ) );
				LUA->Pop( 2 );
				++k;
			}
			while( true );
		}
		else
			keys.push_back( ToString( LUA, 2 ) );

		if( reference != GarrysMod::Lua::Type::NONE )
		{
			try
			{
				client->send( keys, [container, reference]( cpp_redis::reply &reply )
				{
					container->EnqueueResponse( { Action::Reply, reply, reference } );
				} );
			}
			catch( const cpp_redis::redis_error &e )
			{
				LUA->ReferenceFree( reference );
				LUA->PushNil( );
				LUA->PushString( e.what( ) );
				return 2;
			}
		}
		else
		{
			try
			{
				client->send( keys );
			}
			catch( const cpp_redis::redis_error &e )
			{
				LUA->PushNil( );
				LUA->PushString( e.what( ) );
				return 2;
			}
		}
	}

	LUA->PushBool( true );
	return 1;
}

LUA_FUNCTION_STATIC( Commit )
{
	cpp_redis::client *client = Get( LUA, 1 );

	try
	{
		client->commit( );
	}
	catch( const cpp_redis::redis_error &e )
	{
		LUA->PushNil( );
		LUA->PushString( e.what( ) );
		return 2;
	}

	LUA->PushBool( true );
	return 1;
}

void Initialize( GarrysMod::Lua::ILuaBase *LUA )
{
	metatype = LUA->CreateMetaTable( metaname );

	LUA->PushCFunction( tostring );
	LUA->SetField( -2, "__tostring" );

	LUA->PushCFunction( eq );
	LUA->SetField( -2, "__eq" );

	LUA->PushCFunction( index );
	LUA->SetField( -2, "__index" );

	LUA->PushCFunction( newindex );
	LUA->SetField( -2, "__newindex" );

	LUA->PushCFunction( gc );
	LUA->SetField( -2, "__gc" );

	LUA->PushCFunction( gc );
	LUA->SetField( -2, "Destroy" );

	LUA->PushCFunction( IsValid );
	LUA->SetField( -2, "IsValid" );

	LUA->PushCFunction( IsConnected );
	LUA->SetField( -2, "IsConnected" );

	LUA->PushCFunction( Connect );
	LUA->SetField( -2, "Connect" );

	LUA->PushCFunction( Disconnect );
	LUA->SetField( -2, "Disconnect" );

	LUA->PushCFunction( Poll );
	LUA->SetField( -2, "Poll" );
	
	LUA->PushCFunction( Send );
	LUA->SetField( -2, "Send" );

	LUA->PushCFunction( Commit );
	LUA->SetField( -2, "Commit" );

	LUA->Pop( 1 );
}

void Deinitialize( GarrysMod::Lua::ILuaBase *LUA )
{
	LUA->PushNil( );
	LUA->SetField( GarrysMod::Lua::INDEX_REGISTRY, metaname );
}

}
