#ifndef LOGGER_H
#define LOGGER_H
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string>
#include <string.h>
//#include <stringstream.h>
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <vector>
#include <set>
#include <map>
#include <time.h>
#include <mutex>

#include <algorithm> 
#include <functional> 
#include <cctype>
#include <locale>
#include "nlohmann/json.hpp"
using json = nlohmann::json;

#include "PiUtils.h"
namespace Logger
{

    enum LOG_LEVEL {
        NONE,
        TRACE,
        LOG_DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    inline const char * getLevelAsString( LOG_LEVEL level, bool short_name = false )
    {
        switch( level )
        {
            case NONE:
                return short_name ? "NONE" : "none";
            case TRACE:
                return short_name ? "TRACE" : "trace";
            case LOG_DEBUG:
                return short_name ? "DEBUG" : "debug";
            case INFO:
                return short_name ? "INFO" : "info";
            case WARNING:
                return short_name ? "WARNING" : "warning";
            case ERROR:
                return short_name ? "ERROR" : "error";
        };
        return "Invalid LOG_LEVEL";
    }

    inline LOG_LEVEL getLevelFromString( std::string str )
	{
		for( int i = NONE; i <= ERROR; ++i )
		{
			if ( strcmp( str.c_str(), getLevelAsString( (LOG_LEVEL) i, true ) ) == 0 )
			{
				return (LOG_LEVEL) i;
			}
			if ( strcmp( str.c_str(), getLevelAsString( (LOG_LEVEL) i ) ) == 0 )
			{
				return (LOG_LEVEL) i;
			}
		}
		return NONE;
	}

    void setLevel( const std::string& level_str );
    void setLevel( LOG_LEVEL level );
    LOG_LEVEL getLevel();
    void log(LOG_LEVEL level, std::string msg );
    void setLogLocation( const std::string& loc, bool enable, LOG_LEVEL separate_file_level = Logger::NONE );

};
#endif
