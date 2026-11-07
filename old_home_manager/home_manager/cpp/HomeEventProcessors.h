#ifndef HOME_EVENT_PROCESSORS_H
#define HOME_EVENT_PROCESSORS_H
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

#include "HomeEvent.h"
#include "PiUtils.h"

namespace HomeEventProcessor
{
    bool process( HomeEvent* event, HomeSystem* system );

    // TODO It would be nice if processors could register themselves
    enum EVENT_PROCESSOR_TYPE
    {
        RELAY = 0,
        VS_PUMP,
        EMAIL,
        RUN_COMMAND,
        NO_PROCESSOR
    };

    static const char * getEventProcessorTypeAsString( EVENT_PROCESSOR_TYPE type )
    {
        switch( type )
        {
            case RELAY:
                return "relay";
            case VS_PUMP:
                return "vs_pump";
            case EMAIL:
                return "email";
            case RUN_COMMAND:
                return "run_command";
            case NO_PROCESSOR:
                return "no_processor";
        };
        return "no_processor";
    }

    static EVENT_PROCESSOR_TYPE getEventProcessorTypeFromString( std::string str )
    {
        for( int i = 0; i < NO_PROCESSOR; ++i )
        {
            if ( strcmp( str.c_str(), getEventProcessorTypeAsString( (EVENT_PROCESSOR_TYPE) i ) ) == 0 )
            {
                return (EVENT_PROCESSOR_TYPE) i;
            }
        }
        return NO_PROCESSOR;
    }

};
#endif
