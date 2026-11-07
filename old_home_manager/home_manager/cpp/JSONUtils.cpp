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
#include <climits>

#include "JSONUtils.h"
#include "Logger.h"

time_t JSONUtils::getTimeFromJSON_futureDayIfInPast( json object, int num_allowed_seconds )
{
    time_t new_time = getTimeFromJSON( object );
    if ( new_time < ( PiUtils::getCurrentTime_t() - num_allowed_seconds ) ) // Allow 1 minute in the past
    {
        // Add a day
        new_time = PiUtils::getTimeInAdditionHourMinuteSeconds( 1, 0, 0, 0, new_time );
    }
    return new_time;
}

time_t JSONUtils::getTimeFromJSON( json object, bool is_repeat, time_t start_time )
{
	time_t time = 0;
    // We have multiple ways of determining a time
    if ( object.contains( J_TIME_IMMEDIATE ) )
    {
        return 0;
    }
    if ( is_repeat )
    {
        Logger::log( Logger::ERROR, "JSONUtils::getTimeFromJSON repeat start." + object.dump() + " with start_time: " + std::to_string( start_time ) );
        // The original time is what we want. Then add the REPEAT_SECONDS to it.
        if ( start_time == 0 )
        {
            Logger::log( Logger::ERROR, "JSONUtils::getTimeFromJSON doing a repeat, but start_time was 0." + object.dump() );
            return INT_MAX;
        }
        int extra_seconds = JSONUtils::getJSONIntValue( object, J_REPEAT_SECONDS, 0 );
        if ( extra_seconds == 0 )
        {
            Logger::log( Logger::ERROR, "JSONUtils::getTimeFromJSON doing a repeat, but J_REPEAT_SECONDS was 0 (or missing)." + object.dump() );
            return INT_MAX;
        }
        time_t new_time = PiUtils::getTimeInAdditionHourMinuteSeconds( 0, 0, 0, extra_seconds, start_time );
        std::string str;
        PiUtils::getYYMMDD_HHMMSSDisplayTime( new_time, str );
        if ( new_time < PiUtils::getCurrentTime_t() )
            new_time = PiUtils::getCurrentTime_t();
        Logger::log( Logger::TRACE, "JSONUtils::getTimeFromJSON doing a repeat, new time is." + str + " time: " + std::to_string( new_time ) );
        return new_time;
    }
    else if ( object.contains( J_TIME_FULL ) )
    {
    }
    else if ( object.contains( J_TIME_DAY_TIME ) )
    {
		struct tm* cmd_time = new tm;
        std::string time_str = object.at( J_TIME_DAY_TIME );
		getdate_r( time_str.c_str(), cmd_time );
		if ( NULL != cmd_time )
		{
			time = mktime( cmd_time );
			delete cmd_time;
		}
        std::string str;
        PiUtils::getYYMMDD_HHMMSSDisplayTime( cmd_time, str );
        Logger::log( Logger::INFO, "J_TIME_DAY_TIME of: " + time_str + " parsed as: " + str );
    }
    else if ( object.contains( J_TIME_TIME ) )
    {
        // Get the hour minute. Ignore seconds.
		struct tm* cmd_time = new tm;
        std::string time_str = object.at( J_TIME_TIME );
        std::size_t end_hour_pos = time_str.find( ':' );
        int hour = 0;
        int minute = 0;
        if ( end_hour_pos != std::string::npos )
        {
            hour = atol( time_str.c_str() );
            std::string leftover = time_str.substr( end_hour_pos + 1 ); // +1 for the :
            std::size_t end_minute_pos = leftover.find( ':' );
            if ( end_hour_pos != std::string::npos )
            {
                minute = atol( leftover.c_str() );
            }
        }

        time = PiUtils::getNextTimeFromHourMinute( hour, minute, 0, JSONUtils::getJSONIntValue( object, J_REPEAT_SECONDS, 0 ) );
        std::string str;
        PiUtils::getYYMMDD_HHMMSSDisplayTime( time, str );
        Logger::log( Logger::INFO, "J_TIME_TIME of: " + time_str + " parsed as: " + str );
    }
    else if ( object.contains( J_TIME_DAY ) && !is_repeat )
    {
    }
    else if ( object.contains( J_TIME_OFFSET_SECONDS ) )
    {
        return PiUtils::getTimeInAdditionHourMinuteSeconds( 0, 0, 0, object.at( J_TIME_OFFSET_SECONDS ) );
    }
    else if ( object.contains( J_TIME ) )
    {
        return object.at( J_TIME );
    }
    return time;
}

void JSONUtils::clearTimeFromJSON( json& object )
{
    // We have multiple ways of determining a time
    if ( object.contains( J_TIME_IMMEDIATE ) )
    {
        object.erase( J_TIME_IMMEDIATE );
    }
    if ( object.contains( J_TIME_FULL ) )
    {
        object.erase( J_TIME_FULL );
    }
    if ( object.contains( J_TIME_DAY_TIME ) )
    {
        object.erase( J_TIME_DAY_TIME );
    }
    if ( object.contains( J_TIME_TIME ) )
    {
        object.erase( J_TIME_TIME );
    }
    if ( object.contains( J_TIME_DAY ) )
    {
        object.erase( J_TIME_DAY );
    }
    if ( object.contains( J_TIME_OFFSET_SECONDS ) )
    {
        object.erase( J_TIME_OFFSET_SECONDS );
    }
    if ( object.contains( J_TIME ) )
    {
        object.erase( J_TIME );
    }
}

std::string JSONUtils::getJSONStringValue( json j, std::string property, std::string default_val )
{
    return j.contains( property ) ? std::string( j.at( property ) ) : default_val;
}

int JSONUtils::getJSONIntValue( json j, std::string property, int default_val )
{
    return j.contains( property ) ? int( j.at( property ) ) : default_val;
}

bool JSONUtils::getJSONBoolValue( json j, std::string property, bool default_val )
{
    return j.contains( property ) ? bool( j.at( property ) ) : default_val;
}

float JSONUtils::getJSONFloatValue( json j, std::string property, float default_val )
{
    return j.contains( property ) ? float( j.at( property ) ) : default_val;
}

json JSONUtils::getJSONArrayValue( json j, std::string property )
{
    json result;
    return j.contains( property ) ? j.at( property ) : result;
}
