#ifndef DEVICEUTILS_H
#define DEVICEUTILS_H
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
#include <time.h>
#include <mutex>

#include <algorithm> 
#include <functional> 
#include <cctype>
#include <locale>

#include "nlohmann/json.hpp"
using json = nlohmann::json;
#define USE_PYTHON_FOR_GPIO
//#define USE_GPIOCLASS_FOR_GPIO
//#define USE_PSEUDO_RASPBERRY_PI

static const float INVALID_FLOAT_VAL = 99999;

#include "PiUtils.h"

namespace DeviceUtils
{
    enum DEVICE_STATE
    {
        ON            = 100,
        OFF           = 101,
        DELAY_ON      = 102,
        DELAY_OFF     = 103,
        FORCE_OFF     = 104,
        ON_TO_OFF     = 105,
        OFF_TO_ON     = 106,
        ENABLED       = 110,
        DISABLED      = 111,
        UP            = 112,
        DOWN          = 113,
        NEITHER_UP_NOR_DOWN = 114,
        SPEED_1       = 201,
        SPEED_2       = 202,
        SPEED_3       = 203,
        SPEED_4       = 204,
        SPEED_5       = 205,
        SPEED_6       = 206,
        SPEED_7       = 207,
        SPEED_8       = 208,
        CUSTOM        ,
        UNKNOWN_STATE
    };

    inline const char * getStateAsString( DEVICE_STATE state )
    {
        switch( state )
        {
            case ON:
                return "On";
            case OFF:
                return "Off";
            case DELAY_ON:
                return "Delay On";
            case DELAY_OFF:
                return "Delay Off";
            case FORCE_OFF:
                return "Force Off";
            case ON_TO_OFF:
                return "On to Off";
            case OFF_TO_ON:
                return "Off to On";
            case ENABLED:
                return "Enabled";
            case DISABLED:
                return "Disabled";
            case UP:
                return "Up";
            case DOWN:
                return "Down";
            case NEITHER_UP_NOR_DOWN:
                return "Neither up nor down";
            case SPEED_1:
                return "Speed 1";
            case SPEED_2:
                return "Speed 2";
            case SPEED_3:
                return "Speed 3";
            case SPEED_4:
                return "Speed 4";
            case SPEED_5:
                return "Speed 5";
            case SPEED_6:
                return "Speed 6";
            case SPEED_7:
                return "Speed 7";
            case SPEED_8:
                return "Speed 8";
            case CUSTOM:
                return "Custom";
            case UNKNOWN_STATE:
                return "Unknown State";
        };
        return "INVALID STATE AS STRING";
    }

    inline DEVICE_STATE getStateFromString( std::string str )
	{
        std::string str_lower = PiUtils::toLowerCase(str);
		for( int i = 0; i < UNKNOWN_STATE; ++i )
		{
			if ( str_lower == PiUtils::toLowerCase(getStateAsString( (DEVICE_STATE) i )) )
			{
				return (DEVICE_STATE) i;
			}
		}
		return UNKNOWN_STATE;
	}

    enum DEVICE_COMMAND
    {
        SET_STATE = 0,
        GET_STATE,
        EVALUATE,
        TURN_ON,
        TURN_OFF,
        SET_SPEED,
        GET_STATUS,
        SEND_STARTUP,
        MUTE,
        HELP,

        INVALID_DEVICE_COMMAND
    };

    inline const char * getCommandAsString( DEVICE_COMMAND command, bool short_name = false )
    {
        switch( command )
        {
            case SET_STATE:
                return short_name ? "SET_STATE" : "Set State";
            case GET_STATE:
                return short_name ? "GET_STATE" : "Get State";
            case EVALUATE:
                return short_name ? "EVALUATE" : "Evaluate";
            case TURN_ON:
                return short_name ? "TURN_ON" : "Turn On";
            case TURN_OFF:
                return short_name ? "TURN_OFF" : "Turn Off";
            case SET_SPEED:
                return short_name ? "SET_SPEED" : "Set Speed";
            case GET_STATUS:
                return short_name ? "GET_STATUS" : "Get status";
            case SEND_STARTUP:
                return short_name ? "SEND_STARTUP" : "Send startup";
            case MUTE:
                return short_name ? "MUTE" : "Mute";
            case HELP:
                return short_name ? "HELP" : "Help";
            case INVALID_DEVICE_COMMAND:
                return "Invalid Device Command";
        };
        return "Invalid Device Command";
    }

    inline const char * getCommandAsEmailString( DEVICE_COMMAND command )
    {
        switch( command )
        {
            case GET_STATUS:
                return "status";
        };
        return getCommandAsString(HELP);
    }

    inline DEVICE_COMMAND getCommandFromString( std::string str )
	{
        std::string str_lower = PiUtils::toLowerCase(str);
		for( int i = 0; i < INVALID_DEVICE_COMMAND; ++i )
		{
			if ( ( str_lower == PiUtils::toLowerCase(getCommandAsString( (DEVICE_COMMAND) i, true )) ) ||
                 ( str_lower == PiUtils::toLowerCase(getCommandAsString( (DEVICE_COMMAND) i ) ) ) ||
                 ( str_lower == PiUtils::toLowerCase(getCommandAsEmailString( (DEVICE_COMMAND) i ) ) ) )
			{
				return (DEVICE_COMMAND) i;
			}
		}
		return INVALID_DEVICE_COMMAND;
    }
    enum TREND
    {
        NO_TREND = 0,
        FURTHER,
        CLOSER,

        UNKNOWN_TREND
    };


    enum DEVICE_TYPE
    {
        EMAIL = 0,
        RELAY,
        GPIO_SWITCH,
        DS18B20,
        VS_PUMP,
        GPIOPIN,
        GARAGE_DOOR,
        EMAIL_CHECK,
        UNKNOWN_TYPE
    };

    inline const char * getTypeAsString( DEVICE_TYPE type )
    {
        switch( type )
        {
            case EMAIL:
                return "email";
            case RELAY:
                return "relay";
            case GPIO_SWITCH:
                return "gpio_switch";
            case DS18B20:
                return "ds18b20";
            case VS_PUMP:
                return "vs_pump";
            case GPIOPIN:
                return "gpio_pin";
            case GARAGE_DOOR:
                return "garage_door";
            case EMAIL_CHECK:
                return "email_check";
        };
        return "UNKNOWN_TYPE";
    }

    inline DEVICE_TYPE getTypeFromString( std::string str )
    {
        std::string str_lower = PiUtils::toLowerCase(str);
		for( int i = 0; i < UNKNOWN_TYPE; ++i )
		{
			if ( str_lower == PiUtils::toLowerCase(getTypeAsString( (DEVICE_TYPE) i )) )
			{
				return (DEVICE_TYPE) i;
			}
		}
		return UNKNOWN_TYPE;
    }

    static bool isValValid( float val ) { return val < INVALID_FLOAT_VAL; }
    enum EVALUATION_PROPERTIES
    {
        TEMP_C = 0,
        TEMP_F = 1,
        STATE,
        PREVIOUS_STATE
    };
    inline const char * getShortNameForProperty( EVALUATION_PROPERTIES prop )
    {
        switch( prop ) {
            case TEMP_C: return "temp_c";
            case TEMP_F: return "temp_f";
            case STATE: return "state";
            case PREVIOUS_STATE: return "prev_state";
        }
        return "";
    }
    inline const char * getLongNameForProperty( EVALUATION_PROPERTIES prop )
    {
        switch( prop ) {
            case TEMP_C: return "Temperature (C)";
            case TEMP_F: return "Temperature (F)";
            case STATE: return "State";
            case PREVIOUS_STATE: return "Previous State";
        }
        return "";
    }
    enum EVALUATION_PROPERTY_TYPE
    {
        TYPE_FLOAT = 0,
        TYPE_INTEGER,
        TYPE_BOOL,
        TYPE_STATE,
        INVALID_PROPERTY_TYPE
    };
    inline const char * getNameForPropertyType( EVALUATION_PROPERTY_TYPE type, bool get_short )
    {
        switch( type ) {
            case TYPE_FLOAT: return get_short ? "float" : "Float";
            case TYPE_INTEGER: return get_short ? "int" : "Integer";
            case TYPE_BOOL: return get_short ? "bool" : "Boolean";
            case TYPE_STATE: return get_short ? "state" : "State";
            case INVALID_PROPERTY_TYPE: return get_short ? "invalid" : "Invalid Property Type";
        }
        return "";
    }
    inline EVALUATION_PROPERTY_TYPE getPropertyTypeFromString( std::string str )
	{
        std::string str_lower = PiUtils::toLowerCase(str);
		for( int i = 0; i < INVALID_PROPERTY_TYPE; ++i )
		{
			if ( ( str_lower == PiUtils::toLowerCase(getNameForPropertyType( (EVALUATION_PROPERTY_TYPE) i, true )) ) ||
			     ( str_lower == PiUtils::toLowerCase(getNameForPropertyType( (EVALUATION_PROPERTY_TYPE) i, false )) ) )
			{
				return (EVALUATION_PROPERTY_TYPE) i;
			}
		}
		return INVALID_PROPERTY_TYPE;
	}

    enum EVAL_ACTION_TYPE {
        BOUNDS_THRESHOLD,
        STATE_THRESHOLD,
        STATE_CHANGE,
        PROCESS_CHECK_EMAIL,
        CUSTOM_EVAL,
        INVALID_EVAL_ACTION_TYPE
    };

    inline const char * getEvalActionTypeAsString( EVAL_ACTION_TYPE command, bool short_name = false )
    {
        switch( command )
        {
            case BOUNDS_THRESHOLD:
                return short_name ? "BOUNDS_THRESHOLD" : "Bounds threshold";
            case STATE_THRESHOLD:
                return short_name ? "STATE_THRESHOLD" : "State threshold";
            case STATE_CHANGE:
                return short_name ? "STATE_CHANGE" : "State change";
            case PROCESS_CHECK_EMAIL:
                return short_name ? "PROCESS_CHECK_EMAIL" : "Process check email";
            case CUSTOM_EVAL:
                return short_name ? "CUSTOM_EVAL" : "Custom";
            case INVALID_EVAL_ACTION_TYPE:
                break;
        };
        return short_name ? "INVALID_EVAL_ACTION_TYPE" : "Invalid eval action type";
    }

    inline EVAL_ACTION_TYPE getEvalActionTypeFromString( std::string str )
	{
        std::string str_lower = PiUtils::toLowerCase(str);
		for( int i = 0; i < INVALID_EVAL_ACTION_TYPE; ++i )
		{
			if ( ( str_lower == PiUtils::toLowerCase(getEvalActionTypeAsString( (EVAL_ACTION_TYPE) i, true ) ) ) ||
			     ( str_lower == PiUtils::toLowerCase(getEvalActionTypeAsString( (EVAL_ACTION_TYPE) i ) ) ) )
			{
				return (EVAL_ACTION_TYPE) i;
			}
		}
		return INVALID_EVAL_ACTION_TYPE;
	}

    // TODO Ideally, these would get registered by the Evaluation that can handle them
    enum CUSTOM_EVAL_TYPE {
        DOOR_FAILED_TO_CLOSE,
        WACHING_MACHINE_LID_CLOSED_TOO_LONG,
        INVALID_CUSTOM_EVAL_TYPE
    };

    inline const char * getCustomEvalTypeAsString( CUSTOM_EVAL_TYPE command, bool short_name = false )
    {
        switch( command )
        {
            case DOOR_FAILED_TO_CLOSE:
                return short_name ? "DOOR_FAILED_TO_CLOSE" : "Door failed to close";
            case INVALID_CUSTOM_EVAL_TYPE:
                break;
        };
        return short_name ? "INVALID_CUSTOM_EVAL_TYPE" : "Invalid custom eval type";
    }

    inline CUSTOM_EVAL_TYPE getCustomEvalTypeFromString( std::string str )
	{
        std::string str_lower = PiUtils::toLowerCase(str);
		for( int i = 0; i < INVALID_CUSTOM_EVAL_TYPE; ++i )
		{
			if ( ( str_lower == PiUtils::toLowerCase(getCustomEvalTypeAsString( (CUSTOM_EVAL_TYPE) i, true ) ) ) ||
			     ( str_lower == PiUtils::toLowerCase(getCustomEvalTypeAsString( (CUSTOM_EVAL_TYPE) i ) ) ) )
			{
				return (CUSTOM_EVAL_TYPE) i;
			}
		}
		return INVALID_CUSTOM_EVAL_TYPE;
	}
};
#endif
