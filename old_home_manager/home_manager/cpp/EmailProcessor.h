#ifndef EMAIL_PROCESSOR_H
#define EMAIL_PROCESSOR_H
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

namespace EmailProcessor
{
    // This does both send and receive
    bool processCommandEmailEvent( HomeEvent* event, HomeSystem* system );
    bool processSendEmail( HomeEvent* event, HomeSystem* system );
    bool processGetEmail( HomeSystem* system );
    bool sendStartupStatusEmail( bool is_startup, json event_info, json status_info, HomeSystem* system, HomeEvent::EVENT_ORIGINATOR_TYPE event_originator_type );

    bool checkEmail( HomeSystem* system, json email_options, json& received_emails );
    //static json convertEmailToCommand( json email );
};
#endif
