/*

   Copyright [2008] [Trevor Hogan]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   CODE PORTED FROM THE ORIGINAL GHOST PROJECT: http://ghost.pwner.org/

*/

#ifdef GHOST_MYSQL

#include "ghost.h"
#include "util.h"
#include "config.h"
#include "ghostdb.h"
#include "ghostdbmysql.h"

#include <signal.h>

#ifdef WIN32
 #include <winsock.h>
#endif

#include <mysql/mysql.h>
#include <boost/thread.hpp>

//
// CGHostDBMySQL
//

CGHostDBMySQL :: CGHostDBMySQL( CConfig *CFG ) : CGHostDB( CFG )
{
	m_Server = CFG->GetString( "db_mysql_server", string( ) );
	m_Database = CFG->GetString( "db_mysql_database", "ghost" );
	m_User = CFG->GetString( "db_mysql_user", string( ) );
	m_Password = CFG->GetString( "db_mysql_password", string( ) );
	m_Port = CFG->GetInt( "db_mysql_port", 0 );
	m_BotID = CFG->GetInt( "db_mysql_botid", 0 );
	m_NumConnections = 1;
	m_OutstandingCallables = 0;

	mysql_library_init( 0, NULL, NULL );

	// create the first connection

	CONSOLE_Print( "[MYSQL] connecting to database server" );
	MYSQL *Connection = NULL;

	if( !( Connection = mysql_init( NULL ) ) )
	{
		CONSOLE_Print( string( "[MYSQL] " ) + mysql_error( Connection ) );
		m_HasError = true;
		m_Error = "error initializing MySQL connection";
		return;
	}

	my_bool Reconnect = true;
	mysql_options( Connection, MYSQL_OPT_RECONNECT, &Reconnect );

	if( !( mysql_real_connect( Connection, m_Server.c_str( ), m_User.c_str( ), m_Password.c_str( ), m_Database.c_str( ), m_Port, NULL, 0 ) ) )
	{
		CONSOLE_Print( string( "[MYSQL] " ) + mysql_error( Connection ) );
		m_HasError = true;
		m_Error = "error connecting to MySQL server";
		return;
	}

	m_IdleConnections.push( Connection );
}

CGHostDBMySQL :: ~CGHostDBMySQL( )
{
	boost::mutex::scoped_lock lock(m_DatabaseMutex);
	CONSOLE_Print( "[MYSQL] closing " + UTIL_ToString( m_IdleConnections.size( ) ) + "/" + UTIL_ToString( m_NumConnections ) + " idle MySQL connections" );

	while( !m_IdleConnections.empty( ) )
	{
		mysql_close( (MYSQL *)m_IdleConnections.front( ) );
		m_IdleConnections.pop( );
	}

	if( m_OutstandingCallables > 0 )
		CONSOLE_Print( "[MYSQL] " + UTIL_ToString( m_OutstandingCallables ) + " outstanding callables were never recovered" );

	mysql_library_end( );
}

string CGHostDBMySQL :: GetStatus( )
{
	return "DB STATUS --- Connections: " + UTIL_ToString( m_IdleConnections.size( ) ) + "/" + UTIL_ToString( m_NumConnections ) + " idle. Outstanding callables: " + UTIL_ToString( m_OutstandingCallables ) + ".";
}

void CGHostDBMySQL :: RecoverCallable( CBaseCallable *callable )
{
	boost::mutex::scoped_lock lock(m_DatabaseMutex);
	CMySQLCallable *MySQLCallable = dynamic_cast<CMySQLCallable *>( callable );

	if( MySQLCallable )
	{
		if( m_IdleConnections.size( ) > 30 || !MySQLCallable->GetError( ).empty( ) )
		{
			mysql_close( (MYSQL *)MySQLCallable->GetConnection( ) );
                        --m_NumConnections;
		}
		else
			m_IdleConnections.push( MySQLCallable->GetConnection( ) );

		if( m_OutstandingCallables == 0 )
			CONSOLE_Print( "[MYSQL] recovered a mysql callable with zero outstanding" );
		else
                        --m_OutstandingCallables;

		if( !MySQLCallable->GetError( ).empty( ) )
			CONSOLE_Print( "[MYSQL] error --- " + MySQLCallable->GetError( ) );
	}
	else
		CONSOLE_Print( "[MYSQL] tried to recover a non-mysql callable" );
}

void CGHostDBMySQL :: CreateThread( CBaseCallable *callable )
{
	try
	{
		boost :: thread Thread( boost :: ref( *callable ) );
	}
	catch( boost :: thread_resource_error tre )
	{
		CONSOLE_Print( "[MYSQL] error spawning thread on attempt #1 [" + string( tre.what( ) ) + "], pausing execution and trying again in 50ms" );
		MILLISLEEP( 50 );

		try
		{
			boost :: thread Thread( boost :: ref( *callable ) );
		}
		catch( boost :: thread_resource_error tre2 )
		{
			CONSOLE_Print( "[MYSQL] error spawning thread on attempt #2 [" + string( tre2.what( ) ) + "], giving up" );
			callable->SetReady( true );
		}
	}
}

CCallableAdminCount *CGHostDBMySQL :: ThreadedAdminCount( string server )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableAdminCount *Callable = new CMySQLCallableAdminCount( server, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableAdminCheck *CGHostDBMySQL :: ThreadedAdminCheck( string server, string user )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableAdminCheck *Callable = new CMySQLCallableAdminCheck( server, user, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableAdminAdd *CGHostDBMySQL :: ThreadedAdminAdd( string server, string user )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableAdminAdd *Callable = new CMySQLCallableAdminAdd( server, user, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableAdminRemove *CGHostDBMySQL :: ThreadedAdminRemove( string server, string user )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableAdminRemove *Callable = new CMySQLCallableAdminRemove( server, user, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableAdminList *CGHostDBMySQL :: ThreadedAdminList( string server )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableAdminList *Callable = new CMySQLCallableAdminList( server, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableBanCount *CGHostDBMySQL :: ThreadedBanCount( string server )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableBanCount *Callable = new CMySQLCallableBanCount( server, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableBanCheck *CGHostDBMySQL :: ThreadedBanCheck( string server, string user, string ip )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableBanCheck *Callable = new CMySQLCallableBanCheck( server, user, ip, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableBanAdd *CGHostDBMySQL :: ThreadedBanAdd( string server, string user, string ip, string gamename, string admin, string reason, uint32_t expiretime, string context )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableBanAdd *Callable = new CMySQLCallableBanAdd( server, user, ip, gamename, admin, reason, expiretime, context, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableBanRemove *CGHostDBMySQL :: ThreadedBanRemove( string server, string user, string context )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableBanRemove *Callable = new CMySQLCallableBanRemove( server, user, context, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableBanRemove *CGHostDBMySQL :: ThreadedBanRemove( string user, string context )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableBanRemove *Callable = new CMySQLCallableBanRemove( string( ), user, context, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableBanList *CGHostDBMySQL :: ThreadedBanList( string server )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableBanList *Callable = new CMySQLCallableBanList( server, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableCommandList *CGHostDBMySQL :: ThreadedCommandList( )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableCommandList *Callable = new CMySQLCallableCommandList( Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableGameAdd *CGHostDBMySQL :: ThreadedGameAdd( string server, string map, string gamename, string ownername, uint32_t duration, uint32_t gamestate, string creatorname, string creatorserver )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableGameAdd *Callable = new CMySQLCallableGameAdd( server, map, gamename, ownername, duration, gamestate, creatorname, creatorserver, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableGameUpdate *CGHostDBMySQL :: ThreadedGameUpdate( string map, string gamename, string ownername, string creatorname, uint32_t players, string usernames, uint32_t slotsTotal, uint32_t totalGames, uint32_t totalPlayers, bool add )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableGameUpdate *Callable = new CMySQLCallableGameUpdate( map, gamename, ownername, creatorname, players, usernames, slotsTotal, totalGames, totalPlayers, add, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableGamePlayerAdd *CGHostDBMySQL :: ThreadedGamePlayerAdd( uint32_t gameid, string name, string ip, uint32_t spoofed, string spoofedrealm, uint32_t reserved, uint32_t loadingtime, uint32_t left, string leftreason, uint32_t team, uint32_t colour )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableGamePlayerAdd *Callable = new CMySQLCallableGamePlayerAdd( gameid, name, ip, spoofed, spoofedrealm, reserved, loadingtime, left, leftreason, team, colour, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableGamePlayerSummaryCheck *CGHostDBMySQL :: ThreadedGamePlayerSummaryCheck( string name, string realm )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableGamePlayerSummaryCheck *Callable = new CMySQLCallableGamePlayerSummaryCheck( name, realm, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableVampPlayerSummaryCheck *CGHostDBMySQL :: ThreadedVampPlayerSummaryCheck( string name )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableVampPlayerSummaryCheck *Callable = new CMySQLCallableVampPlayerSummaryCheck( name, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableDotAGameAdd *CGHostDBMySQL :: ThreadedDotAGameAdd( uint32_t gameid, uint32_t winner, uint32_t min, uint32_t sec, string saveType )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableDotAGameAdd *Callable = new CMySQLCallableDotAGameAdd( gameid, winner, min, sec, saveType, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableDotAPlayerAdd *CGHostDBMySQL :: ThreadedDotAPlayerAdd( uint32_t gameid, uint32_t colour, uint32_t kills, uint32_t deaths, uint32_t creepkills, uint32_t creepdenies, uint32_t assists, uint32_t gold, uint32_t neutralkills, string item1, string item2, string item3, string item4, string item5, string item6, string hero, uint32_t newcolour, uint32_t towerkills, uint32_t raxkills, uint32_t courierkills, string saveType )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableDotAPlayerAdd *Callable = new CMySQLCallableDotAPlayerAdd( gameid, colour, kills, deaths, creepkills, creepdenies, assists, gold, neutralkills, item1, item2, item3, item4, item5, item6, hero, newcolour, towerkills, raxkills, courierkills, saveType, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableDotAPlayerSummaryCheck *CGHostDBMySQL :: ThreadedDotAPlayerSummaryCheck( string name, string realm, string saveType )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableDotAPlayerSummaryCheck *Callable = new CMySQLCallableDotAPlayerSummaryCheck( name, realm, saveType, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableTreePlayerSummaryCheck *CGHostDBMySQL :: ThreadedTreePlayerSummaryCheck( string name, string realm )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableTreePlayerSummaryCheck *Callable = new CMySQLCallableTreePlayerSummaryCheck( name, realm, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableSnipePlayerSummaryCheck *CGHostDBMySQL :: ThreadedSnipePlayerSummaryCheck( string name, string realm )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableSnipePlayerSummaryCheck *Callable = new CMySQLCallableSnipePlayerSummaryCheck( name, realm, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableShipsPlayerSummaryCheck *CGHostDBMySQL :: ThreadedShipsPlayerSummaryCheck( string name, string realm )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableShipsPlayerSummaryCheck *Callable = new CMySQLCallableShipsPlayerSummaryCheck( name, realm, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableW3MMDPlayerSummaryCheck *CGHostDBMySQL :: ThreadedW3MMDPlayerSummaryCheck( string name, string realm, string category )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableW3MMDPlayerSummaryCheck *Callable = new CMySQLCallableW3MMDPlayerSummaryCheck( name, realm, category, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableDownloadAdd *CGHostDBMySQL :: ThreadedDownloadAdd( string map, uint32_t mapsize, string name, string ip, uint32_t spoofed, string spoofedrealm, uint32_t downloadtime )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableDownloadAdd *Callable = new CMySQLCallableDownloadAdd( map, mapsize, name, ip, spoofed, spoofedrealm, downloadtime, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableConnectCheck *CGHostDBMySQL :: ThreadedConnectCheck( string name, uint32_t sessionkey )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableConnectCheck *Callable = new CMySQLCallableConnectCheck( name, sessionkey, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableScoreCheck *CGHostDBMySQL :: ThreadedScoreCheck( string category, string name, string server )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableScoreCheck *Callable = new CMySQLCallableScoreCheck( category, name, server, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableLeagueCheck *CGHostDBMySQL :: ThreadedLeagueCheck( string category, string name, string server )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableLeagueCheck *Callable = new CMySQLCallableLeagueCheck( category, name, server, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableW3MMDPlayerAdd *CGHostDBMySQL :: ThreadedW3MMDPlayerAdd( string category, uint32_t gameid, uint32_t pid, string name, string flag, uint32_t leaver, uint32_t practicing )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableW3MMDPlayerAdd *Callable = new CMySQLCallableW3MMDPlayerAdd( category, gameid, pid, name, flag, leaver, practicing, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableW3MMDVarAdd *CGHostDBMySQL :: ThreadedW3MMDVarAdd( uint32_t gameid, map<VarP,int32_t> var_ints )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableW3MMDVarAdd *Callable = new CMySQLCallableW3MMDVarAdd( gameid, var_ints, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableW3MMDVarAdd *CGHostDBMySQL :: ThreadedW3MMDVarAdd( uint32_t gameid, map<VarP,double> var_reals )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableW3MMDVarAdd *Callable = new CMySQLCallableW3MMDVarAdd( gameid, var_reals, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

CCallableW3MMDVarAdd *CGHostDBMySQL :: ThreadedW3MMDVarAdd( uint32_t gameid, map<VarP,string> var_strings )
{
	void *Connection = GetIdleConnection( );

	if( !Connection )
                ++m_NumConnections;

	CCallableW3MMDVarAdd *Callable = new CMySQLCallableW3MMDVarAdd( gameid, var_strings, Connection, m_BotID, m_Server, m_Database, m_User, m_Password, m_Port );
	CreateThread( Callable );
        ++m_OutstandingCallables;
	return Callable;
}

void *CGHostDBMySQL :: GetIdleConnection( )
{
	boost::mutex::scoped_lock lock(m_DatabaseMutex);
	void *Connection = NULL;

	if( !m_IdleConnections.empty( ) )
	{
		Connection = m_IdleConnections.front( );
		m_IdleConnections.pop( );
	}

	return Connection;
}

//
// unprototyped global helper functions
//

string MySQLEscapeString( void *conn, string str )
{
	char *to = new char[str.size( ) * 2 + 1];
	unsigned long size = mysql_real_escape_string( (MYSQL *)conn, to, str.c_str( ), str.size( ) );
	string result( to, size );
	delete [] to;
	return result;
}

vector<string> MySQLFetchRow( MYSQL_RES *res )
{
	vector<string> Result;

	MYSQL_ROW Row = mysql_fetch_row( res );

	if( Row )
	{
		unsigned long *Lengths;
		Lengths = mysql_fetch_lengths( res );

                for( unsigned int i = 0; i < mysql_num_fields( res ); ++i )
		{
			if( Row[i] )
				Result.push_back( string( Row[i], Lengths[i] ) );
			else
				Result.push_back( string( ) );
		}
	}

	return Result;
}

//
// global helper functions
//

uint32_t MySQLAdminCount( void *conn, string *error, uint32_t botid, string server )
{
	string EscServer = MySQLEscapeString( conn, server );
	uint32_t Count = 0;
	string Query = "SELECT COUNT(*) FROM admins WHERE server='" + EscServer + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			if( Row.size( ) == 1 )
				Count = UTIL_ToUInt32( Row[0] );
			else
				*error = "error counting admins [" + server + "] - row doesn't have 1 column";

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return Count;
}

bool MySQLAdminCheck( void *conn, string *error, uint32_t botid, string server, string user )
{
	transform( user.begin( ), user.end( ), user.begin( ), (int(*)(int))tolower );
	string EscServer = MySQLEscapeString( conn, server );
	string EscUser = MySQLEscapeString( conn, user );
	bool IsAdmin = false;
	string Query = "SELECT * FROM admins WHERE server='" + EscServer + "' AND name='" + EscUser + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			if( !Row.empty( ) )
				IsAdmin = true;

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return IsAdmin;
}

bool MySQLAdminAdd( void *conn, string *error, uint32_t botid, string server, string user )
{
	transform( user.begin( ), user.end( ), user.begin( ), (int(*)(int))tolower );
	string EscServer = MySQLEscapeString( conn, server );
	string EscUser = MySQLEscapeString( conn, user );
	bool Success = false;
	string Query = "INSERT INTO admins ( botid, server, name ) VALUES ( " + UTIL_ToString( botid ) + ", '" + EscServer + "', '" + EscUser + "' )";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		Success = true;

	return Success;
}

bool MySQLAdminRemove( void *conn, string *error, uint32_t botid, string server, string user )
{
	transform( user.begin( ), user.end( ), user.begin( ), (int(*)(int))tolower );
	string EscServer = MySQLEscapeString( conn, server );
	string EscUser = MySQLEscapeString( conn, user );
	bool Success = false;
	string Query = "DELETE FROM admins WHERE server='" + EscServer + "' AND name='" + EscUser + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		Success = true;

	return Success;
}

vector<string> MySQLAdminList( void *conn, string *error, uint32_t botid, string server )
{
	string EscServer = MySQLEscapeString( conn, server );
	vector<string> AdminList;
	string Query = "SELECT name FROM admins WHERE server='" + EscServer + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			while( !Row.empty( ) )
			{
				AdminList.push_back( Row[0] );
				Row = MySQLFetchRow( Result );
			}

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return AdminList;
}

uint32_t MySQLBanCount( void *conn, string *error, uint32_t botid, string server )
{
	string EscServer = MySQLEscapeString( conn, server );
	uint32_t Count = 0;
	string Query = "SELECT COUNT(*) FROM bans WHERE server='" + EscServer + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			if( Row.size( ) == 1 )
				Count = UTIL_ToUInt32( Row[0] );
			else
				*error = "error counting bans [" + server + "] - row doesn't have 1 column";

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return Count;
}

CDBBan *MySQLBanCheck( void *conn, string *error, uint32_t botid, string server, string user, string ip )
{
	transform( user.begin( ), user.end( ), user.begin( ), (int(*)(int))tolower );
	string EscServer = MySQLEscapeString( conn, server );
	string EscUser = MySQLEscapeString( conn, user );
	string EscIP = MySQLEscapeString( conn, ip );
	CDBBan *Ban = NULL;
	string Query;

	if( ip.empty( ) )
		Query = "SELECT name, ip, DATE(date), gamename, admin, reason, expiredate, context FROM bans WHERE server='" + EscServer + "' AND name='" + EscUser + "'";
	else
		Query = "SELECT name, ip, DATE(date), gamename, admin, reason, expiredate, context FROM bans WHERE (server='" + EscServer + "' AND name='" + EscUser + "') OR ip='" + EscIP + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			if( Row.size( ) == 8 )
				Ban = new CDBBan( server, Row[0], Row[1], Row[2], Row[3], Row[4], Row[5], Row[6], Row[7] );
			/* else
				*error = "error checking ban [" + server + " : " + user + "] - row doesn't have 6 columns"; */

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return Ban;
}

bool MySQLBanAdd( void *conn, string *error, uint32_t botid, string server, string user, string ip, string gamename, string admin, string reason, uint32_t expiretime, string context )
{
	transform( user.begin( ), user.end( ), user.begin( ), (int(*)(int))tolower );
	transform( admin.begin( ), admin.end( ), admin.begin( ), (int(*)(int))tolower );
	transform( context.begin( ), context.end( ), context.begin( ), (int(*)(int))tolower );
	string EscServer = MySQLEscapeString( conn, server );
	string EscUser = MySQLEscapeString( conn, user );
	string EscIP = MySQLEscapeString( conn, ip );
	string EscGameName = MySQLEscapeString( conn, gamename );
	string EscAdmin = MySQLEscapeString( conn, admin );
	string EscReason = MySQLEscapeString( conn, reason );
	string EscContext = MySQLEscapeString( conn, context );
	bool Success = false;
	string Query = "INSERT INTO bans ( botid, server, name, ip, date, gamename, admin, reason, expiredate, context ) VALUES ( " + UTIL_ToString( botid ) + ", '" + EscServer + "', '" + EscUser + "', '" + EscIP + "', CURDATE( ), '" + EscGameName + "', '" + EscAdmin + "', '" + EscReason + "', DATE_ADD( NOW( ), INTERVAL " + UTIL_ToString( expiretime ) + " second ), '" + EscContext + "' )";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		Success = true;

	return Success;
}

bool MySQLBanRemove( void *conn, string *error, uint32_t botid, string server, string user, string context )
{
	transform( user.begin( ), user.end( ), user.begin( ), (int(*)(int))tolower );
	transform( context.begin( ), context.end( ), context.begin( ), (int(*)(int))tolower );
	string EscServer = MySQLEscapeString( conn, server );
	string EscUser = MySQLEscapeString( conn, user );
	string EscContext = MySQLEscapeString( conn, context );
	bool Success = false;
	string Query = "DELETE FROM bans WHERE server='" + EscServer + "' AND name='" + EscUser + "'";
	
	if( EscContext != "" && EscContext != "ttr.cloud" )
		Query += " AND admin='" + EscContext + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		Success = true;

	return Success;
}

bool MySQLBanRemove( void *conn, string *error, uint32_t botid, string user, string context )
{
	transform( user.begin( ), user.end( ), user.begin( ), (int(*)(int))tolower );
	transform( context.begin( ), context.end( ), context.begin( ), (int(*)(int))tolower );
	string EscUser = MySQLEscapeString( conn, user );
	string EscContext = MySQLEscapeString( conn, context );
	bool Success = false;
	string Query = "DELETE FROM bans WHERE name='" + EscUser + "'";
	
	if( EscContext != "" && EscContext != "ttr.cloud" )
		Query += " AND admin='" + EscContext + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		Success = true;

	return Success;
}

vector<CDBBan *> MySQLBanList( void *conn, string *error, uint32_t botid, string server )
{
	vector<CDBBan *> BanList;
	
	// clear expired bans first
	string ClearQuery = "DELETE FROM bans WHERE expiredate < NOW( )";
	
	if( mysql_real_query( (MYSQL *)conn, ClearQuery.c_str( ), ClearQuery.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		string EscServer = MySQLEscapeString( conn, server );
		string Query = "SELECT name, ip, DATE(date), gamename, admin, reason, expiredate, context FROM bans WHERE server='" + EscServer + "'";

		if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
			*error = mysql_error( (MYSQL *)conn );
		else
		{
			MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

			if( Result )
			{
				vector<string> Row = MySQLFetchRow( Result );

				while( Row.size( ) == 8 )
				{
					BanList.push_back( new CDBBan( server, Row[0], Row[1], Row[2], Row[3], Row[4], Row[5], Row[6], Row[7] ) );
					Row = MySQLFetchRow( Result );
				}

				mysql_free_result( Result );
			}
			else
				*error = mysql_error( (MYSQL *)conn );
		}
	}

	return BanList;
}

vector<string> MySQLCommandList( void *conn, string *error, uint32_t botid )
{
	vector<string> CommandList;
	string Query = "SELECT command FROM commands WHERE botid='" + UTIL_ToString(botid) + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			while( Row.size( ) == 1 )
			{
				CommandList.push_back( Row[0] );
				Row = MySQLFetchRow( Result );
			}

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	string DeleteQuery = "DELETE FROM commands WHERE botid='" + UTIL_ToString(botid) + "'";

	if( mysql_real_query( (MYSQL *)conn, DeleteQuery.c_str( ), DeleteQuery.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );

	return CommandList;
}

uint32_t MySQLGameAdd( void *conn, string *error, uint32_t botid, string server, string map, string gamename, string ownername, uint32_t duration, uint32_t gamestate, string creatorname, string creatorserver )
{
	uint32_t RowID = 0;
	string EscServer = MySQLEscapeString( conn, server );
	string EscMap = MySQLEscapeString( conn, map );
	string EscGameName = MySQLEscapeString( conn, gamename );
	string EscOwnerName = MySQLEscapeString( conn, ownername );
	string EscCreatorName = MySQLEscapeString( conn, creatorname );
	string EscCreatorServer = MySQLEscapeString( conn, creatorserver );
	string Query = "INSERT INTO games ( botid, server, map, datetime, gamename, ownername, duration, gamestate, creatorname, creatorserver ) VALUES ( " + UTIL_ToString( botid ) + ", '" + EscServer + "', '" + EscMap + "', NOW( ), '" + EscGameName + "', '" + EscOwnerName + "', " + UTIL_ToString( duration ) + ", " + UTIL_ToString( gamestate ) + ", '" + EscCreatorName + "', '" + EscCreatorServer + "' )";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		RowID = mysql_insert_id( (MYSQL *)conn );

	return RowID;
}

string MySQLGameUpdate( void *conn, string *error, uint32_t botid, string map, string gamename, string ownername, string creatorname, uint32_t players, string usernames, uint32_t slotsTotal, uint32_t totalGames, uint32_t totalPlayers, bool add )
{
	if(add) {
        string EscMap = MySQLEscapeString(conn, map);
        string EscGameName = MySQLEscapeString( conn, gamename );
        string EscOwnerName = MySQLEscapeString( conn, ownername );
        string EscCreatorName = MySQLEscapeString( conn, creatorname );
        string EscUsernames = MySQLEscapeString( conn, usernames );
        string Query = "UPDATE gamelist SET map = '" + EscMap + "', gamename = '" + EscGameName + "', ownername = '" + EscOwnerName + "', creatorname = '" + EscCreatorName + "', slotstaken = '" + UTIL_ToString(players) + "', slotstotal = '" + UTIL_ToString(slotsTotal) + "', usernames = '" + EscUsernames + "', totalgames = '" + UTIL_ToString(totalGames) + "', totalplayers = '" + UTIL_ToString(totalPlayers) + "' WHERE botid='" + UTIL_ToString(botid) + "'";
        
        if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
            *error = mysql_error( (MYSQL *)conn );

        return "";
    } else {
        string Query = "SELECT gamename,slotstaken,slotstotal FROM gamelist";
        
        if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
            *error = mysql_error( (MYSQL *)conn );
        else
            {
                MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );
                string response = "Current games: ";
                int num = 0;
                
                if( Result )
                    {
                        vector<string> Row = MySQLFetchRow( Result );
                        
                        while( !Row.empty( ) )
                            {
                                if(Row[0] != "") {
                                    response += Row[0] + " (" + Row[1] + "/" + Row[2] + "), ";
                                    num++;
                                }
                                
                                Row = MySQLFetchRow( Result );
                            }
                        
                        
                        mysql_free_result( Result );
                    }
                else
                    *error = mysql_error( (MYSQL *)conn );
                
                if(num == 0) {
                    response += "none";
                } else {
                    response = response.substr(0, response.length() - 2);
                }

                return response;
            }

        return "";
    }
}

uint32_t MySQLGamePlayerAdd( void *conn, string *error, uint32_t botid, uint32_t gameid, string name, string ip, uint32_t spoofed, string spoofedrealm, uint32_t reserved, uint32_t loadingtime, uint32_t left, string leftreason, uint32_t team, uint32_t colour )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	uint32_t RowID = 0;
	string EscName = MySQLEscapeString( conn, name );
	string EscIP = MySQLEscapeString( conn, ip );
	string EscSpoofedRealm = MySQLEscapeString( conn, spoofedrealm );
	string EscLeftReason = MySQLEscapeString( conn, leftreason );
	string Query = "INSERT INTO gameplayers ( botid, gameid, name, ip, spoofed, reserved, loadingtime, `left`, leftreason, team, colour, spoofedrealm ) VALUES ( " + UTIL_ToString( botid ) + ", " + UTIL_ToString( gameid ) + ", '" + EscName + "', '" + EscIP + "', " + UTIL_ToString( spoofed ) + ", " + UTIL_ToString( reserved ) + ", " + UTIL_ToString( loadingtime ) + ", " + UTIL_ToString( left ) + ", '" + EscLeftReason + "', " + UTIL_ToString( team ) + ", " + UTIL_ToString( colour ) + ", '" + EscSpoofedRealm + "' )";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		RowID = mysql_insert_id( (MYSQL *)conn );

	return RowID;
}

CDBGamePlayerSummary *MySQLGamePlayerSummaryCheck( void *conn, string *error, uint32_t botid, string name, string realm )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	string EscName = MySQLEscapeString( conn, name );
	string EscRealm = MySQLEscapeString( conn, realm );
	CDBGamePlayerSummary *GamePlayerSummary = NULL;
	string Query = "SELECT IFNULL(SUM(num_games), 0), (IFNULL(SUM(total_leftpercent), 1) / IFNULL(SUM(num_games), 1) * 100) FROM gametrack WHERE name='" + EscName + "'";
	
	if( !realm.empty( ) )
		Query += " AND realm = '" + EscRealm + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			if( Row.size( ) == 2 )
			{
				uint32_t TotalGames = UTIL_ToUInt32( Row[0] );
				double LeftPercent = UTIL_ToDouble( Row[1] );
				GamePlayerSummary = new CDBGamePlayerSummary( realm, name, TotalGames, LeftPercent );
			}
			else
				*error = "error checking gameplayersummary [" + name + "] - row doesn't have 2 columns";

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return GamePlayerSummary;
}

CDBVampPlayerSummary *MySQLVampPlayerSummaryCheck( void *conn, string *error, uint32_t botid, string name )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	string EscName = MySQLEscapeString( conn, name );
	CDBVampPlayerSummary *VampPlayerSummary = NULL;
	string GamesQuery = "SELECT COUNT(*) FROM w3mmdplayers WHERE name='" + EscName + "' AND category='uxvamp'";
	string HumanGamesQuery = "SELECT COUNT(*) FROM w3mmdvars,w3mmdplayers WHERE w3mmdvars.varname='status' AND w3mmdvars.value_int >= '2' AND w3mmdplayers.gameid=w3mmdvars.gameid AND w3mmdplayers.pid=w3mmdvars.pid AND w3mmdplayers.name='" + EscName + "' AND w3mmdplayers.category='uxvamp'";
	//vamp games = total games - human games
	string HumanWinsQuery = "SELECT COUNT(*) FROM w3mmdvars,w3mmdplayers WHERE w3mmdvars.varname='status' AND w3mmdvars.value_int = '4' AND w3mmdplayers.gameid=w3mmdvars.gameid AND w3mmdplayers.pid=w3mmdvars.pid AND w3mmdplayers.name='" + EscName + "' AND w3mmdplayers.category='uxvamp'";
	string VampWinsQuery = "SELECT COUNT(*) FROM w3mmdvars,w3mmdplayers WHERE w3mmdvars.varname='status' AND w3mmdvars.value_int = '0' AND w3mmdplayers.gameid=w3mmdvars.gameid AND w3mmdplayers.pid=w3mmdvars.pid AND w3mmdplayers.name='" + EscName + "' AND w3mmdplayers.category='uxvamp'";
	string HumanLossesQuery = "SELECT COUNT(*) FROM w3mmdvars,w3mmdplayers WHERE w3mmdvars.varname='status' AND (w3mmdvars.value_int = '2' OR w3mmdvars.value_int = '5') AND w3mmdplayers.gameid=w3mmdvars.gameid AND w3mmdplayers.pid=w3mmdvars.pid AND w3mmdplayers.name='" + EscName + "' AND w3mmdplayers.category='uxvamp'";
	//vamp losses = vamp games - vamp wins
	string VampKillsQuery = "SELECT IFNULL(SUM(w3mmdvars.value_int), 0) FROM w3mmdvars,w3mmdplayers WHERE w3mmdvars.varname='vampkills' AND w3mmdplayers.gameid=w3mmdvars.gameid AND w3mmdplayers.pid=w3mmdvars.pid AND w3mmdplayers.name='" + EscName + "' AND w3mmdplayers.category='uxvamp'";
	string CCMinQuery = "SELECT IFNULL(MIN(w3mmdvars.value_real), -1) FROM w3mmdvars,w3mmdplayers WHERE w3mmdvars.varname='commandtime' AND w3mmdvars.value_real > 0 AND w3mmdplayers.gameid=w3mmdvars.gameid AND w3mmdplayers.pid=w3mmdvars.pid AND w3mmdplayers.name='" + EscName + "' AND w3mmdplayers.category='uxvamp'";
	string CCAvgQuery = "SELECT IFNULL(AVG(w3mmdvars.value_real), -1) FROM w3mmdvars,w3mmdplayers WHERE w3mmdvars.varname='commandtime' AND w3mmdvars.value_real > 0 AND w3mmdplayers.gameid=w3mmdvars.gameid AND w3mmdplayers.pid=w3mmdvars.pid AND w3mmdplayers.name='" + EscName + "' AND w3mmdplayers.category='uxvamp'";
	string BaseMinQuery = "SELECT IFNULL(MIN(w3mmdvars.value_real), -1) FROM w3mmdvars,w3mmdplayers WHERE w3mmdvars.varname='basetime' AND w3mmdvars.value_real > 0 AND w3mmdplayers.gameid=w3mmdvars.gameid AND w3mmdplayers.pid=w3mmdvars.pid AND w3mmdplayers.name='" + EscName + "' AND w3mmdplayers.category='uxvamp'";
	string BaseAvgQuery = "SELECT IFNULL(AVG(w3mmdvars.value_real), -1) FROM w3mmdvars,w3mmdplayers WHERE w3mmdvars.varname='basetime' AND w3mmdvars.value_real > 0 AND w3mmdplayers.gameid=w3mmdvars.gameid AND w3mmdplayers.pid=w3mmdvars.pid AND w3mmdplayers.name='" + EscName + "' AND w3mmdplayers.category='uxvamp'";

	if( mysql_real_query( (MYSQL *)conn, GamesQuery.c_str( ), GamesQuery.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );
			uint32_t TotalGames = UTIL_ToUInt32( Row[0] );

			if( TotalGames > 0 )
			{
				uint32_t TotalHumanGames = 0;
				uint32_t TotalVampGames = 0;
				uint32_t TotalHumanWins = 0;
				uint32_t TotalVampWins = 0;
				uint32_t TotalHumanLosses = 0;
				uint32_t TotalVampLosses = 0;
				uint32_t TotalVampKills = 0;
				double MinCommandCenter = 0;
				double AvgCommandCenter = 0;
				double MinBase = 0;
				double AvgBase = 0;

				// calculate total human games
				if( mysql_real_query( (MYSQL *)conn, HumanGamesQuery.c_str( ), HumanGamesQuery.size( ) ) != 0 )
					*error = mysql_error( (MYSQL *)conn );
				else
				{
					MYSQL_RES *Result1 = mysql_store_result( (MYSQL *)conn );

					if( Result1 )
					{
						vector<string> Row1 = MySQLFetchRow( Result1 );

						if( Row1.size( ) == 1)
							TotalHumanGames = UTIL_ToUInt32( Row1[0] );
						else
							*error = "error checking VampPlayerSummary TotalHumanGames [" + name + "] - row doesn't have 1 column";

						mysql_free_result( Result1 );
					}
					else
						*error = mysql_error( (MYSQL *)conn );
				}
				
				TotalVampGames = TotalGames - TotalHumanGames;

				// calculate total human wins
				if( mysql_real_query( (MYSQL *)conn, HumanWinsQuery.c_str( ), HumanWinsQuery.size( ) ) != 0 )
					*error = mysql_error( (MYSQL *)conn );
				else
				{
					MYSQL_RES *Result2 = mysql_store_result( (MYSQL *)conn );

					if( Result2 )
					{
						vector<string> Row2 = MySQLFetchRow( Result2 );

						if( Row2.size( ) == 1)
							TotalHumanWins = UTIL_ToUInt32( Row2[0] );
						else
							*error = "error checking VampPlayerSummary TotalHumanWins [" + name + "] - row doesn't have 1 column";

						mysql_free_result( Result2 );
					}
					else
						*error = mysql_error( (MYSQL *)conn );
				}

				// calculate total vamp wins
				if( mysql_real_query( (MYSQL *)conn, VampWinsQuery.c_str( ), VampWinsQuery.size( ) ) != 0 )
					*error = mysql_error( (MYSQL *)conn );
				else
				{
					MYSQL_RES *Result3 = mysql_store_result( (MYSQL *)conn );

					if( Result3 )
					{
						vector<string> Row3 = MySQLFetchRow( Result3 );

						if( Row3.size( ) == 1)
							TotalVampWins = UTIL_ToUInt32( Row3[0] );
						else
							*error = "error checking VampPlayerSummary TotalVampWins [" + name + "] - row doesn't have 1 column";

						mysql_free_result( Result3 );
					}
					else
						*error = mysql_error( (MYSQL *)conn );
				}

				// calculate total human losses
				if( mysql_real_query( (MYSQL *)conn, HumanLossesQuery.c_str( ), HumanLossesQuery.size( ) ) != 0 )
					*error = mysql_error( (MYSQL *)conn );
				else
				{
					MYSQL_RES *Result4 = mysql_store_result( (MYSQL *)conn );

					if( Result4 )
					{
						vector<string> Row4 = MySQLFetchRow( Result4 );

						if( Row4.size( ) == 1)
							TotalHumanLosses = UTIL_ToUInt32( Row4[0] );
						else
							*error = "error checking VampPlayerSummary TotalHumanLosses [" + name + "] - row doesn't have 1 column";

						mysql_free_result( Result4 );
					}
					else
						*error = mysql_error( (MYSQL *)conn );
				}
				
				TotalVampLosses = TotalVampGames - TotalVampWins;

				// calculate total vamp kills
				if( mysql_real_query( (MYSQL *)conn, VampKillsQuery.c_str( ), VampKillsQuery.size( ) ) != 0 )
					*error = mysql_error( (MYSQL *)conn );
				else
				{
					MYSQL_RES *Result5 = mysql_store_result( (MYSQL *)conn );

					if( Result5 )
					{
						vector<string> Row5 = MySQLFetchRow( Result5 );

						if( Row5.size( ) == 1)
							TotalVampKills = UTIL_ToUInt32( Row5[0] );
						else
							*error = "error checking VampPlayerSummary TotalVampKills [" + name + "] - row doesn't have 1 column";

						mysql_free_result( Result5 );
					}
					else
						*error = mysql_error( (MYSQL *)conn );
				}

				// calculate min cc
				if( mysql_real_query( (MYSQL *)conn, CCMinQuery.c_str( ), CCMinQuery.size( ) ) != 0 )
					*error = mysql_error( (MYSQL *)conn );
				else
				{
					MYSQL_RES *Result6 = mysql_store_result( (MYSQL *)conn );

					if( Result6 )
					{
						vector<string> Row6 = MySQLFetchRow( Result6 );

						if( Row6.size( ) == 1)
							MinCommandCenter = UTIL_ToDouble( Row6[0] );
						else
							*error = "error checking VampPlayerSummary CCMin [" + name + "] - row doesn't have 1 column";

						mysql_free_result( Result6 );
					}
					else
						*error = mysql_error( (MYSQL *)conn );
				}

				// calculate avg cc
				if( mysql_real_query( (MYSQL *)conn, CCAvgQuery.c_str( ), CCAvgQuery.size( ) ) != 0 )
					*error = mysql_error( (MYSQL *)conn );
				else
				{
					MYSQL_RES *Result7 = mysql_store_result( (MYSQL *)conn );

					if( Result7 )
					{
						vector<string> Row7 = MySQLFetchRow( Result7 );

						if( Row7.size( ) == 1)
							AvgCommandCenter = UTIL_ToDouble( Row7[0] );
						else
							*error = "error checking VampPlayerSummary CCAvg [" + name + "] - row doesn't have 1 column";

						mysql_free_result( Result7 );
					}
					else
						*error = mysql_error( (MYSQL *)conn );
				}

				// calculate min base
				if( mysql_real_query( (MYSQL *)conn, BaseMinQuery.c_str( ), BaseMinQuery.size( ) ) != 0 )
					*error = mysql_error( (MYSQL *)conn );
				else
				{
					MYSQL_RES *Result8 = mysql_store_result( (MYSQL *)conn );

					if( Result8 )
					{
						vector<string> Row8 = MySQLFetchRow( Result8 );

						if( Row8.size( ) == 1)
							MinBase = UTIL_ToDouble( Row8[0] );
						else
							*error = "error checking VampPlayerSummary BaseMin [" + name + "] - row doesn't have 1 column";

						mysql_free_result( Result8 );
					}
					else
						*error = mysql_error( (MYSQL *)conn );
				}

				// calculate avg base
				if( mysql_real_query( (MYSQL *)conn, BaseAvgQuery.c_str( ), BaseAvgQuery.size( ) ) != 0 )
					*error = mysql_error( (MYSQL *)conn );
				else
				{
					MYSQL_RES *Result9 = mysql_store_result( (MYSQL *)conn );

					if( Result9 )
					{
						vector<string> Row9 = MySQLFetchRow( Result9 );

						if( Row9.size( ) == 1)
							AvgBase = UTIL_ToDouble( Row9[0] );
						else
							*error = "error checking VampPlayerSummary BaseAvg [" + name + "] - row doesn't have 1 column";

						mysql_free_result( Result9 );
					}
					else
						*error = mysql_error( (MYSQL *)conn );
				}

				// done
				VampPlayerSummary = new CDBVampPlayerSummary( string( ), name, TotalGames, TotalHumanGames, TotalVampGames, TotalHumanWins, TotalVampWins, TotalHumanLosses, TotalVampLosses, TotalVampKills, MinCommandCenter, AvgCommandCenter, MinBase, AvgBase);
			}

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return VampPlayerSummary;
}

uint32_t MySQLDotAGameAdd( void *conn, string *error, uint32_t botid, uint32_t gameid, uint32_t winner, uint32_t min, uint32_t sec, string saveType )
{
	uint32_t RowID = 0;
	
	string table = "dotagames";
	
	if( saveType == "lod" )
		table = "lodgames";
	else if( saveType == "dota2" )
		table = "dota2games";
	else if( saveType == "eihl" )
		table = "eihlgames";
	
	string Query = "INSERT INTO " + table + " ( botid, gameid, winner, min, sec ) VALUES ( " + UTIL_ToString( botid ) + ", " + UTIL_ToString( gameid ) + ", " + UTIL_ToString( winner ) + ", " + UTIL_ToString( min ) + ", " + UTIL_ToString( sec ) + " )";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		RowID = mysql_insert_id( (MYSQL *)conn );

	return RowID;
}

uint32_t MySQLDotAPlayerAdd( void *conn, string *error, uint32_t botid, uint32_t gameid, uint32_t colour, uint32_t kills, uint32_t deaths, uint32_t creepkills, uint32_t creepdenies, uint32_t assists, uint32_t gold, uint32_t neutralkills, string item1, string item2, string item3, string item4, string item5, string item6, string hero, uint32_t newcolour, uint32_t towerkills, uint32_t raxkills, uint32_t courierkills, string saveType )
{
	uint32_t RowID = 0;
	string EscItem1 = MySQLEscapeString( conn, item1 );
	string EscItem2 = MySQLEscapeString( conn, item2 );
	string EscItem3 = MySQLEscapeString( conn, item3 );
	string EscItem4 = MySQLEscapeString( conn, item4 );
	string EscItem5 = MySQLEscapeString( conn, item5 );
	string EscItem6 = MySQLEscapeString( conn, item6 );
	string EscHero = MySQLEscapeString( conn, hero );
	
	string table = "dotaplayers";
	
	if( saveType == "lod" )
		table = "lodplayers";
	else if( saveType == "dota2" )
		table = "dota2players";
	else if( saveType == "eihl" )
		table = "eihlplayers";
	
	string Query = "INSERT INTO " + table + " ( botid, gameid, colour, kills, deaths, creepkills, creepdenies, assists, gold, neutralkills, item1, item2, item3, item4, item5, item6, hero, newcolour, towerkills, raxkills, courierkills ) VALUES ( " + UTIL_ToString( botid ) + ", " + UTIL_ToString( gameid ) + ", " + UTIL_ToString( colour ) + ", " + UTIL_ToString( kills ) + ", " + UTIL_ToString( deaths ) + ", " + UTIL_ToString( creepkills ) + ", " + UTIL_ToString( creepdenies ) + ", " + UTIL_ToString( assists ) + ", " + UTIL_ToString( gold ) + ", " + UTIL_ToString( neutralkills ) + ", '" + EscItem1 + "', '" + EscItem2 + "', '" + EscItem3 + "', '" + EscItem4 + "', '" + EscItem5 + "', '" + EscItem6 + "', '" + EscHero + "', " + UTIL_ToString( newcolour ) + ", " + UTIL_ToString( towerkills ) + ", " + UTIL_ToString( raxkills ) + ", " + UTIL_ToString( courierkills ) + " )";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		RowID = mysql_insert_id( (MYSQL *)conn );

	return RowID;
}

CDBDotAPlayerSummary *MySQLDotAPlayerSummaryCheck( void *conn, string *error, uint32_t botid, string name, string realm, string saveType )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	string EscName = MySQLEscapeString( conn, name );
	string EscRealm = MySQLEscapeString( conn, realm );
	
	string table = "dota_elo_scores";
	
	if( saveType == "lod" )
		table = "lod_elo_scores";
	else if( saveType == "dota2" )
		table = "dota2_elo_scores";
	else if( saveType == "eihl" )
		table = "eihl_elo_scores";
	
	CDBDotAPlayerSummary *DotAPlayerSummary = NULL;
	string Query = "SELECT IFNULL(SUM(games), 0), IFNULL(SUM(kills), 0), IFNULL(SUM(deaths), 0), IFNULL(SUM(creepkills), 0), IFNULL(SUM(creepdenies), 0), IFNULL(SUM(assists), 0), IFNULL(SUM(neutralkills), 0), IFNULL(SUM(towerkills), 0), IFNULL(SUM(raxkills), 0), IFNULL(SUM(courierkills), 0), IFNULL(SUM(wins), 0), IFNULL(SUM(losses), 0), IFNULL(MAX(score), 0) FROM " + table + " WHERE name='" + EscName + "'";
	
	if( !realm.empty( ) )
		Query += " AND server = '" + EscRealm + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			if( Row.size( ) == 13 )
			{
				uint32_t TotalGames = UTIL_ToUInt32( Row[0] );

				if( TotalGames > 0 )
				{
					uint32_t TotalKills = UTIL_ToUInt32( Row[1] );
					uint32_t TotalDeaths = UTIL_ToUInt32( Row[2] );
					uint32_t TotalCreepKills = UTIL_ToUInt32( Row[3] );
					uint32_t TotalCreepDenies = UTIL_ToUInt32( Row[4] );
					uint32_t TotalAssists = UTIL_ToUInt32( Row[5] );
					uint32_t TotalNeutralKills = UTIL_ToUInt32( Row[6] );
					uint32_t TotalTowerKills = UTIL_ToUInt32( Row[7] );
					uint32_t TotalRaxKills = UTIL_ToUInt32( Row[8] );
					uint32_t TotalCourierKills = UTIL_ToUInt32( Row[9] );
					uint32_t TotalWins = UTIL_ToUInt32( Row[10] );
					uint32_t TotalLosses = UTIL_ToUInt32( Row[11] );
					double Score = UTIL_ToDouble( Row[12] );

					// done

					DotAPlayerSummary = new CDBDotAPlayerSummary( realm, name, TotalGames, TotalWins, TotalLosses, TotalKills, TotalDeaths, TotalCreepKills, TotalCreepDenies, TotalAssists, TotalNeutralKills, TotalTowerKills, TotalRaxKills, TotalCourierKills, Score );
				}
			}
			else
				*error = "error checking dotaplayersummary [" + name + "] - row doesn't have 13 columns";

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return DotAPlayerSummary;
}

CDBTreePlayerSummary *MySQLTreePlayerSummaryCheck( void *conn, string *error, uint32_t botid, string name, string realm )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	string EscName = MySQLEscapeString( conn, name );
	string EscRealm = MySQLEscapeString( conn, realm );
	CDBTreePlayerSummary *TreePlayerSummary = NULL;
	string Query = "SELECT IFNULL(SUM(games), 0), IFNULL(SUM(intstats0), 0), IFNULL(SUM(intstats1), 0), IFNULL(SUM(intstats2), 0), IFNULL(SUM(intstats3), 0), IFNULL(SUM(intstats4), 0), IFNULL(SUM(intstats5), 0), IFNULL(SUM(wins), 0), IFNULL(SUM(losses), 0), IFNULL(MAX(score), 0) FROM w3mmd_elo_scores WHERE name='" + EscName + "' AND category = 'treetag'";
	
	if( !realm.empty( ) )
		Query += " AND server = '" + EscRealm + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );
			uint32_t TotalGames = UTIL_ToUInt32( Row[0] );

			if( Row.size( ) == 10 )
			{
				uint32_t TotalGames = UTIL_ToUInt32( Row[0] );
				
				if( TotalGames > 0 )
				{
					uint32_t TotalWins = UTIL_ToUInt32( Row[7] );
					uint32_t TotalLosses = UTIL_ToUInt32( Row[8] );
					uint32_t TotalKills = UTIL_ToUInt32( Row[4] );
		            uint32_t TotalTKs = UTIL_ToUInt32( Row[1] );
					uint32_t TotalDeaths = UTIL_ToUInt32( Row[2] );
					uint32_t TotalSaves = UTIL_ToUInt32( Row[3] );
		            uint32_t TotalEntGames = UTIL_ToUInt32( Row[5] );
		            uint32_t TotalInfernalGames = UTIL_ToUInt32( Row[6] );
					double Score = UTIL_ToDouble( Row[9] );

					// done

					TreePlayerSummary = new CDBTreePlayerSummary( realm, name, TotalGames, TotalWins, TotalLosses, TotalKills, TotalTKs, TotalDeaths, TotalSaves, TotalEntGames, TotalInfernalGames, Score );
				}
			}

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return TreePlayerSummary;
}

CDBShipsPlayerSummary *MySQLShipsPlayerSummaryCheck( void *conn, string *error, uint32_t botid, string name, string realm )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	string EscName = MySQLEscapeString( conn, name );
	string EscRealm = MySQLEscapeString( conn, realm );
	CDBShipsPlayerSummary *ShipsPlayerSummary = NULL;
	string Query = "SELECT IFNULL(SUM(games), 0), IFNULL(SUM(intstats0), 0), IFNULL(SUM(intstats1), 0), IFNULL(SUM(wins), 0), IFNULL(SUM(losses), 0), IFNULL(MAX(score), 0) FROM w3mmd_elo_scores WHERE name='" + EscName + "' AND category = 'battleships'";
	
	if( !realm.empty( ) )
		Query += " AND server = '" + EscRealm + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			if( Row.size( ) == 6 )
			{
				uint32_t TotalGames = UTIL_ToUInt32( Row[0] );
				
				if( TotalGames > 0 )
				{
					uint32_t TotalWins = UTIL_ToUInt32( Row[3] );
					uint32_t TotalLosses = UTIL_ToUInt32( Row[4] );
					uint32_t TotalKills = UTIL_ToUInt32( Row[1] );
					uint32_t TotalDeaths = UTIL_ToUInt32( Row[2] );
					double Score = UTIL_ToDouble( Row[5] );
					
					// done

					ShipsPlayerSummary = new CDBShipsPlayerSummary( realm, name, TotalGames, TotalWins, TotalLosses, TotalKills, TotalDeaths, Score );
				}
			}

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return ShipsPlayerSummary;
}

CDBSnipePlayerSummary *MySQLSnipePlayerSummaryCheck( void *conn, string *error, uint32_t botid, string name, string realm )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	string EscName = MySQLEscapeString( conn, name );
	string EscRealm = MySQLEscapeString( conn, realm );
	CDBSnipePlayerSummary *SnipePlayerSummary = NULL;
	string Query = "SELECT IFNULL(SUM(games), 0), IFNULL(SUM(intstats0), 0), IFNULL(SUM(intstats1), 0), IFNULL(SUM(wins), 0), IFNULL(SUM(losses), 0), IFNULL(MAX(score), 0) FROM w3mmd_elo_scores WHERE name='" + EscName + "' AND category = 'elitesnipers'";
	
	if( !realm.empty( ) )
		Query += " AND server = '" + EscRealm + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );
			
			if( Row.size( ) == 6 )
			{
				uint32_t TotalGames = UTIL_ToUInt32( Row[0] );
				
				if( TotalGames > 0 )
				{
					uint32_t TotalWins = UTIL_ToUInt32( Row[3] );
					uint32_t TotalLosses = UTIL_ToUInt32( Row[4] );
					uint32_t TotalKills = UTIL_ToUInt32( Row[1] );
					uint32_t TotalDeaths = UTIL_ToUInt32( Row[2] );
					double Score = UTIL_ToDouble( Row[5] );
					
					// done

					SnipePlayerSummary = new CDBSnipePlayerSummary( realm, name, TotalGames, TotalWins, TotalLosses, TotalKills, TotalDeaths, Score );
				}
			}

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return SnipePlayerSummary;
}

CDBW3MMDPlayerSummary *MySQLW3MMDPlayerSummaryCheck( void *conn, string *error, uint32_t botid, string name, string realm, string category )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	string EscName = MySQLEscapeString( conn, name );
	string EscRealm = MySQLEscapeString( conn, realm );
	string EscCategory = MySQLEscapeString( conn, category );
	CDBW3MMDPlayerSummary *W3MMDPlayerSummary = NULL;
	string Query = "SELECT IFNULL(SUM(games), 0), IFNULL(SUM(wins), 0), IFNULL(SUM(losses), 0), IFNULL(MAX(score), 0) FROM w3mmd_elo_scores WHERE name='" + EscName + "' AND category = '" + EscCategory + "'";
	
	if( !realm.empty( ) )
		Query += " AND server = '" + EscRealm + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );
			
			if( Row.size( ) == 4 )
			{
				uint32_t TotalGames = UTIL_ToUInt32( Row[0] );
				
				if( TotalGames > 0 )
				{
					uint32_t TotalWins = UTIL_ToUInt32( Row[1] );
					uint32_t TotalLosses = UTIL_ToUInt32( Row[2] );
					double Score = UTIL_ToDouble( Row[3] );
					
					// done

					W3MMDPlayerSummary = new CDBW3MMDPlayerSummary( realm, name, category, TotalGames, TotalWins, TotalLosses, Score );
				}
			}

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return W3MMDPlayerSummary;
}

bool MySQLDownloadAdd( void *conn, string *error, uint32_t botid, string map, uint32_t mapsize, string name, string ip, uint32_t spoofed, string spoofedrealm, uint32_t downloadtime )
{
	bool Success = false;
	string EscMap = MySQLEscapeString( conn, map );
	string EscName = MySQLEscapeString( conn, name );
	string EscIP = MySQLEscapeString( conn, ip );
	string EscSpoofedRealm = MySQLEscapeString( conn, spoofedrealm );
	string Query = "INSERT INTO downloads ( botid, map, mapsize, datetime, name, ip, spoofed, spoofedrealm, downloadtime ) VALUES ( " + UTIL_ToString( botid ) + ", '" + EscMap + "', " + UTIL_ToString( mapsize ) + ", NOW( ), '" + EscName + "', '" + EscIP + "', " + UTIL_ToString( spoofed ) + ", '" + EscSpoofedRealm + "', " + UTIL_ToString( downloadtime ) + " )";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		Success = true;

	return Success;
}

double MySQLScoreCheck( void *conn, string *error, uint32_t botid, string category, string name, string server )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	string EscCategory = MySQLEscapeString( conn, category );
	string EscName = MySQLEscapeString( conn, name );
	string EscServer = MySQLEscapeString( conn, server );
	double Score = -100000.0;
	
	string Query = "SELECT score FROM scores WHERE category='" + EscCategory + "' AND name='" + EscName + "' AND server='" + EscServer + "'";
	
	if( category == "dota2" ) // dota2 checks normal DotA stats
		Query = "SELECT score FROM dota_elo_scores WHERE name='" + EscName + "' AND server='" + EscServer + "' AND wins >= 20";
	else if( category == "castlefight2" ) // castlefight2 checks castlefight stats
		Query = "SELECT score FROM w3mmd_elo_scores WHERE category='castlefight' AND name='" + EscName + "' AND server='" + EscServer + "' AND wins >= 15";
	else if( category == "legionmega2" ) // legionmega2 checks legion mega stats
		Query = "SELECT score FROM w3mmd_elo_scores WHERE category='legionmega' AND name='" + EscName + "' AND server='" + EscServer + "' AND wins >= 15";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			if( Row.size( ) == 1 )
				Score = UTIL_ToDouble( Row[0] );
			/* else
				*error = "error checking score [" + category + " : " + name + " : " + server + "] - row doesn't have 1 column"; */

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return Score;
}

uint32_t MySQLLeagueCheck( void *conn, string *error, uint32_t botid, string category, string name, string server )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	string EscCategory = MySQLEscapeString( conn, category );
	string EscName = MySQLEscapeString( conn, name );
	string EscServer = MySQLEscapeString( conn, server );
	uint32_t SID = 255;
	
	string Query = "SELECT k FROM league_status WHERE category='" + EscCategory + "' AND v='" + EscName + "'";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			if( Row.size( ) == 1 )
				SID = UTIL_ToUInt32( Row[0] );
			/* else
				*error = "error checking score [" + category + " : " + name + " : " + server + "] - row doesn't have 1 column"; */

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return SID;
}

bool MySQLConnectCheck( void *conn, string *error, uint32_t botid, string name, uint32_t sessionkey )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	string EscName = MySQLEscapeString( conn, name );
	bool Check = false;
	string Query = "SELECT sessionkey FROM entconnect WHERE username='" + EscName + "' AND TIMESTAMPDIFF(HOUR, time, NOW()) < 10";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
	{
		MYSQL_RES *Result = mysql_store_result( (MYSQL *)conn );

		if( Result )
		{
			vector<string> Row = MySQLFetchRow( Result );

			if( Row.size( ) == 1 )
			{
				if( UTIL_ToUInt32( Row[0] ) == sessionkey )
					Check = true;
			}

			mysql_free_result( Result );
		}
		else
			*error = mysql_error( (MYSQL *)conn );
	}

	return Check;
}

uint32_t MySQLW3MMDPlayerAdd( void *conn, string *error, uint32_t botid, string category, uint32_t gameid, uint32_t pid, string name, string flag, uint32_t leaver, uint32_t practicing )
{
	transform( name.begin( ), name.end( ), name.begin( ), (int(*)(int))tolower );
	uint32_t RowID = 0;
	string EscCategory = MySQLEscapeString( conn, category );
	string EscName = MySQLEscapeString( conn, name );
	string EscFlag = MySQLEscapeString( conn, flag );
	string Query = "INSERT INTO w3mmdplayers ( botid, category, gameid, pid, name, flag, leaver, practicing ) VALUES ( " + UTIL_ToString( botid ) + ", '" + EscCategory + "', " + UTIL_ToString( gameid ) + ", " + UTIL_ToString( pid ) + ", '" + EscName + "', '" + EscFlag + "', " + UTIL_ToString( leaver ) + ", " + UTIL_ToString( practicing ) + " )";

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		RowID = mysql_insert_id( (MYSQL *)conn );

	return RowID;
}

bool MySQLW3MMDVarAdd( void *conn, string *error, uint32_t botid, uint32_t gameid, map<VarP,int32_t> var_ints )
{
	if( var_ints.empty( ) )
		return false;

	bool Success = false;
	string Query;

        for( map<VarP,int32_t> :: iterator i = var_ints.begin( ); i != var_ints.end( ); ++i )
	{
		string EscVarName = MySQLEscapeString( conn, i->first.second );

		if( Query.empty( ) )
			Query = "INSERT INTO w3mmdvars ( botid, gameid, pid, varname, value_int ) VALUES ( " + UTIL_ToString( botid ) + ", " + UTIL_ToString( gameid ) + ", " + UTIL_ToString( i->first.first ) + ", '" + EscVarName + "', " + UTIL_ToString( i->second ) + " )";
		else
			Query += ", ( " + UTIL_ToString( botid ) + ", " + UTIL_ToString( gameid ) + ", " + UTIL_ToString( i->first.first ) + ", '" + EscVarName + "', " + UTIL_ToString( i->second ) + " )";
	}

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		Success = true;

	return Success;
}

bool MySQLW3MMDVarAdd( void *conn, string *error, uint32_t botid, uint32_t gameid, map<VarP,double> var_reals )
{
	if( var_reals.empty( ) )
		return false;

	bool Success = false;
	string Query;

        for( map<VarP,double> :: iterator i = var_reals.begin( ); i != var_reals.end( ); ++i )
	{
		string EscVarName = MySQLEscapeString( conn, i->first.second );

		if( Query.empty( ) )
			Query = "INSERT INTO w3mmdvars ( botid, gameid, pid, varname, value_real ) VALUES ( " + UTIL_ToString( botid ) + ", " + UTIL_ToString( gameid ) + ", " + UTIL_ToString( i->first.first ) + ", '" + EscVarName + "', " + UTIL_ToString( i->second, 10 ) + " )";
		else
			Query += ", ( " + UTIL_ToString( botid ) + ", " + UTIL_ToString( gameid ) + ", " + UTIL_ToString( i->first.first ) + ", '" + EscVarName + "', " + UTIL_ToString( i->second, 10 ) + " )";
	}

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		Success = true;

	return Success;
}

bool MySQLW3MMDVarAdd( void *conn, string *error, uint32_t botid, uint32_t gameid, map<VarP,string> var_strings )
{
	if( var_strings.empty( ) )
		return false;

	bool Success = false;
	string Query;

        for( map<VarP,string> :: iterator i = var_strings.begin( ); i != var_strings.end( ); ++i )
	{
		string EscVarName = MySQLEscapeString( conn, i->first.second );
		string EscValueString = MySQLEscapeString( conn, i->second );

		if( Query.empty( ) )
			Query = "INSERT INTO w3mmdvars ( botid, gameid, pid, varname, value_string ) VALUES ( " + UTIL_ToString( botid ) + ", " + UTIL_ToString( gameid ) + ", " + UTIL_ToString( i->first.first ) + ", '" + EscVarName + "', '" + EscValueString + "' )";
		else
			Query += ", ( " + UTIL_ToString( botid ) + ", " + UTIL_ToString( gameid ) + ", " + UTIL_ToString( i->first.first ) + ", '" + EscVarName + "', '" + EscValueString + "' )";
	}

	if( mysql_real_query( (MYSQL *)conn, Query.c_str( ), Query.size( ) ) != 0 )
		*error = mysql_error( (MYSQL *)conn );
	else
		Success = true;

	return Success;
}

//
// MySQL Callables
//

void CMySQLCallable :: Init( )
{
	CBaseCallable :: Init( );

#ifndef WIN32
	// disable SIGPIPE since this is (or should be) a new thread and it doesn't inherit the spawning thread's signal handlers
	// MySQL should automatically disable SIGPIPE when we initialize it but we do so anyway here

	signal( SIGPIPE, SIG_IGN );
#endif

	mysql_thread_init( );

	if( !m_Connection )
	{
		if( !( m_Connection = mysql_init( NULL ) ) )
			m_Error = mysql_error( (MYSQL *)m_Connection );

		my_bool Reconnect = true;
		mysql_options( (MYSQL *)m_Connection, MYSQL_OPT_RECONNECT, &Reconnect );

		if( !( mysql_real_connect( (MYSQL *)m_Connection, m_SQLServer.c_str( ), m_SQLUser.c_str( ), m_SQLPassword.c_str( ), m_SQLDatabase.c_str( ), m_SQLPort, NULL, 0 ) ) )
			m_Error = mysql_error( (MYSQL *)m_Connection );
	}
	else if( mysql_ping( (MYSQL *)m_Connection ) != 0 )
		m_Error = mysql_error( (MYSQL *)m_Connection );
}

void CMySQLCallable :: Close( )
{
	mysql_thread_end( );

	CBaseCallable :: Close( );
}

void CMySQLCallableAdminCount :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLAdminCount( m_Connection, &m_Error, m_SQLBotID, m_Server );

	Close( );
}

void CMySQLCallableAdminCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLAdminCheck( m_Connection, &m_Error, m_SQLBotID, m_Server, m_User );

	Close( );
}

void CMySQLCallableAdminAdd :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLAdminAdd( m_Connection, &m_Error, m_SQLBotID, m_Server, m_User );

	Close( );
}

void CMySQLCallableAdminRemove :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLAdminRemove( m_Connection, &m_Error, m_SQLBotID, m_Server, m_User );

	Close( );
}

void CMySQLCallableAdminList :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLAdminList( m_Connection, &m_Error, m_SQLBotID, m_Server );

	Close( );
}

void CMySQLCallableBanCount :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLBanCount( m_Connection, &m_Error, m_SQLBotID, m_Server );

	Close( );
}

void CMySQLCallableBanCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLBanCheck( m_Connection, &m_Error, m_SQLBotID, m_Server, m_User, m_IP );

	Close( );
}

void CMySQLCallableBanAdd :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLBanAdd( m_Connection, &m_Error, m_SQLBotID, m_Server, m_User, m_IP, m_GameName, m_Admin, m_Reason, m_ExpireTime, m_Context );

	Close( );
}

void CMySQLCallableBanRemove :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
	{
		if( m_Server.empty( ) )
			m_Result = MySQLBanRemove( m_Connection, &m_Error, m_SQLBotID, m_User, m_Context );
		else
			m_Result = MySQLBanRemove( m_Connection, &m_Error, m_SQLBotID, m_Server, m_User, m_Context );
	}

	Close( );
}

void CMySQLCallableBanList :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLBanList( m_Connection, &m_Error, m_SQLBotID, m_Server );

	Close( );
}

void CMySQLCallableCommandList :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLCommandList( m_Connection, &m_Error, m_SQLBotID );

	Close( );
}

void CMySQLCallableGameAdd :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLGameAdd( m_Connection, &m_Error, m_SQLBotID, m_Server, m_Map, m_GameName, m_OwnerName, m_Duration, m_GameState, m_CreatorName, m_CreatorServer );

	Close( );
}

void CMySQLCallableGameUpdate :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLGameUpdate( m_Connection, &m_Error, m_SQLBotID, m_Map, m_GameName, m_OwnerName, m_CreatorName, m_Players, m_Usernames, m_SlotsTotal, m_TotalGames, m_TotalPlayers, m_Add );

	Close( );
}

void CMySQLCallableGamePlayerAdd :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLGamePlayerAdd( m_Connection, &m_Error, m_SQLBotID, m_GameID, m_Name, m_IP, m_Spoofed, m_SpoofedRealm, m_Reserved, m_LoadingTime, m_Left, m_LeftReason, m_Team, m_Colour );

	Close( );
}

void CMySQLCallableGamePlayerSummaryCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLGamePlayerSummaryCheck( m_Connection, &m_Error, m_SQLBotID, m_Name, m_Realm );

	Close( );
}

void CMySQLCallableVampPlayerSummaryCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLVampPlayerSummaryCheck( m_Connection, &m_Error, m_SQLBotID, m_Name );

	Close( );
}

void CMySQLCallableDotAGameAdd :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLDotAGameAdd( m_Connection, &m_Error, m_SQLBotID, m_GameID, m_Winner, m_Min, m_Sec, m_SaveType );

	Close( );
}

void CMySQLCallableDotAPlayerAdd :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLDotAPlayerAdd( m_Connection, &m_Error, m_SQLBotID, m_GameID, m_Colour, m_Kills, m_Deaths, m_CreepKills, m_CreepDenies, m_Assists, m_Gold, m_NeutralKills, m_Item1, m_Item2, m_Item3, m_Item4, m_Item5, m_Item6, m_Hero, m_NewColour, m_TowerKills, m_RaxKills, m_CourierKills, m_SaveType );

	Close( );
}

void CMySQLCallableDotAPlayerSummaryCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLDotAPlayerSummaryCheck( m_Connection, &m_Error, m_SQLBotID, m_Name, m_Realm, m_SaveType );

	Close( );
}

void CMySQLCallableTreePlayerSummaryCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLTreePlayerSummaryCheck( m_Connection, &m_Error, m_SQLBotID, m_Name, m_Realm );

	Close( );
}

void CMySQLCallableSnipePlayerSummaryCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLSnipePlayerSummaryCheck( m_Connection, &m_Error, m_SQLBotID, m_Name, m_Realm );

	Close( );
}

void CMySQLCallableShipsPlayerSummaryCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLShipsPlayerSummaryCheck( m_Connection, &m_Error, m_SQLBotID, m_Name, m_Realm );

	Close( );
}

void CMySQLCallableW3MMDPlayerSummaryCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLW3MMDPlayerSummaryCheck( m_Connection, &m_Error, m_SQLBotID, m_Name, m_Realm, m_Category );

	Close( );
}

void CMySQLCallableDownloadAdd :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLDownloadAdd( m_Connection, &m_Error, m_SQLBotID, m_Map, m_MapSize, m_Name, m_IP, m_Spoofed, m_SpoofedRealm, m_DownloadTime );

	Close( );
}

void CMySQLCallableScoreCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLScoreCheck( m_Connection, &m_Error, m_SQLBotID, m_Category, m_Name, m_Server );

	Close( );
}

void CMySQLCallableLeagueCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLLeagueCheck( m_Connection, &m_Error, m_SQLBotID, m_Category, m_Name, m_Server );

	Close( );
}

void CMySQLCallableConnectCheck :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLConnectCheck( m_Connection, &m_Error, m_SQLBotID, m_Name, m_SessionKey );

	Close( );
}

void CMySQLCallableW3MMDPlayerAdd :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
		m_Result = MySQLW3MMDPlayerAdd( m_Connection, &m_Error, m_SQLBotID, m_Category, m_GameID, m_PID, m_Name, m_Flag, m_Leaver, m_Practicing );

	Close( );
}

void CMySQLCallableW3MMDVarAdd :: operator( )( )
{
	Init( );

	if( m_Error.empty( ) )
	{
		if( m_ValueType == VALUETYPE_INT )
			m_Result = MySQLW3MMDVarAdd( m_Connection, &m_Error, m_SQLBotID, m_GameID, m_VarInts );
		else if( m_ValueType == VALUETYPE_REAL )
			m_Result = MySQLW3MMDVarAdd( m_Connection, &m_Error, m_SQLBotID, m_GameID, m_VarReals );
		else
			m_Result = MySQLW3MMDVarAdd( m_Connection, &m_Error, m_SQLBotID, m_GameID, m_VarStrings );
	}

	Close( );
}

#endif
