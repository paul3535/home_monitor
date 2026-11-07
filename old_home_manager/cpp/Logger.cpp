#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string>
#include <string.h>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fstream>
#include <map>

#include "Logger.h"

using namespace PiUtils;
namespace Logger {

static LOG_LEVEL g_logLevel = LOG_LEVEL::TRACE;
static std::string g_logLocation = "";
static bool g_logToFile = false;
static LOG_LEVEL g_logToSeparateFile = LOG_LEVEL::NONE;

void setLevel( const std::string& level_str )
{
    LOG_LEVEL level = getLevelFromString( level_str );
    setLevel( level );
}
void setLevel( LOG_LEVEL level )
{
    g_logLevel = level;
    Logger::log( Logger::TRACE, std::string( "Setting log level to: " ) + getLevelAsString( level ) );
}

LOG_LEVEL getLevel() {
    return g_logLevel;
}

void setLogLocation( const std::string& loc, bool enable, LOG_LEVEL separate_file_level )
{
    g_logLocation = loc;
    g_logToFile = enable;
    g_logToSeparateFile = separate_file_level;
    Logger::log( Logger::TRACE, std::string( "Setting log loc to: " ) + g_logLocation + " and logToFile to: " + ( g_logToFile ? " true" :" false" ) );
}

// NOTE: NONE means don't prepend anything
void logToFile(LOG_LEVEL level, std::string msg )
{
    std::string day_str;
    struct tm* timeinfo = getCurrentTime( );
    PiUtils::getYYMMDDDisplayTime( timeinfo, day_str );
    std::string filename = g_logLocation;
    filename.append( "/" );
    if ( level != Logger::NONE)
    {
        filename.append( Logger::getLevelAsString(level) );
        filename.append( "_" );
    }
    filename.append( day_str );
    filename.append( ".log" );
    std::fstream file;
    file.open(filename, std::ios_base::app | std::ios_base::in );
    if ( file.is_open() )
    {
        file << msg;
        file.close();
    }
}

void log(LOG_LEVEL level, std::string msg )
{
    // Determine if we need to do anything, as we can have 3 types of output:
    // std::cerr - If we are level >= g_logLevel
    // common file - If we have g_logToFile (this is dependent on the previous case )
    // separate files - If we have level >= g_logToSeparateFile
    // We need to build the string if any of the following are true
    bool output_cerr = level >= g_logLevel;
    bool output_separate = level >= g_logToSeparateFile;
    if ( !output_cerr && !output_separate ){
        // Nothing to do.
        return;
    }

    // At this point, we need to output something
    struct tm* timeinfo = getCurrentTime( );
    std::string time_str;
    getPrettyDisplayTime( timeinfo, time_str );

    std::string str = time_str;
    str.append( ": " ); 
    str.append( Logger::getLevelAsString(level));
    str.append( ": " ); 
    str.append( msg );
    str.append( "\n" );

    if ( output_cerr ) {
        std::cerr << str;
        if ( g_logToFile )
        {
            logToFile( Logger::NONE, str);
        }
    }
    if ( output_separate ) {
 	    for( int i = level; i > Logger::NONE; --i )
        {
            logToFile( Logger::LOG_LEVEL(i), str);
        }
    }

}
}
