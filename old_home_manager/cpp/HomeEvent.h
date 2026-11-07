#ifndef HOME_EVENT_H
#define HOME_EVENT_H
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

#include "DeviceUtils.h"
#include "PiUtils.h"

class HomeSystem;

class HomeEvent
{
    public:
        enum EVENT_ORIGINATOR_TYPE // bitfield
        {
            SCHEDULED = 0x0001, // These come from the file
            USER      = 0x0002, // These come from user entry
            SYSTEM    = 0x0004, // These are applied by the system based on the others (for example, turn the heater off 20 minutes before the pump
            ANALYZED  = 0x0008, // These come from analysis of other events
            STARTUP   = 0x0010, // These come from the startup
            DEBUG_EVENT = 0x0020, // Debugging
            EMAIL     = 0x0040, // From an e-mail
            REPEAT_EVENT    = 0x0080, // Repeating an event at a future time
            RETRY     = 0x0100, // Retry the same event
            UNKNOWN   = 0x8000, // 
        };

        HomeEvent();
        ~HomeEvent();

        void setOriginatorType( HomeEvent::EVENT_ORIGINATOR_TYPE type ) { _originator_type = type; }
        HomeEvent::EVENT_ORIGINATOR_TYPE getOriginatorType( void ) const { return _originator_type; }

        // Convenience method for setting something to happens in the future
        void setFutureTimeOfEvent( int future_in_seconds );

        // This is when the even should be processed (in reality, it will be the first pass of the runSystem after
        // this time.
        void setEventTime( time_t event_time ) { _event_time = event_time; }
        time_t getEventTime( void ) const { return _event_time; }

        void setEventTimeJSON( json time_info );

        void addEventInfo( json info ) { _event_info = info; }
        json getEventInfo( bool include_extra_info = false ) const;

        void setEventId( unsigned int id );
        unsigned int getEventId() const;

        static bool getEventsFromFile( std::string fn, std::vector<HomeEvent*>& events, int clear );
        static HomeEvent* getEventFromJSON( json object, HomeEvent::EVENT_ORIGINATOR_TYPE type );

        bool initialize( HomeSystem* system );
        bool process( HomeSystem* system );

        void dump() const;
        std::string toString( ) const;

        static const char * getEventOriginatorTypeAsString( EVENT_ORIGINATOR_TYPE type )
        {
            switch( type )
            {
                case SCHEDULED:
                    return "Scheduled";
                case USER:
                    return "User";
                case SYSTEM:
                    return "System";
                case ANALYZED:
                    return "Analyzed";
                case STARTUP:
                    return "Startup";
                case DEBUG_EVENT:
                    return "Debug";
                case EMAIL:
                    return "Email";
                case REPEAT_EVENT:
                    return "Repeat";
                case UNKNOWN:
                    return "Unknown";
            };
            return "Unknown";
        }

        static EVENT_ORIGINATOR_TYPE getOriginatorTypeFromString( std::string str )
        {
            for( int i = 0; i < UNKNOWN; ++i )
            {
                if ( strcmp( str.c_str(), getEventOriginatorTypeAsString( (EVENT_ORIGINATOR_TYPE) i ) ) == 0 )
                {
                    return (EVENT_ORIGINATOR_TYPE) i;
                }
            }
            return UNKNOWN;
        }

        std::string getEventProcessorTypeString() const { return _processor_type_str; }
        void setEventProcessorTypeString( const std::string& str ) { _processor_type_str = str; }

    private:
        bool repeatIfNecessary( HomeSystem* system ) const;

    private:
        HomeEvent::EVENT_ORIGINATOR_TYPE _originator_type;
        time_t _event_time;
        std::string _processor_type_str;

        json _event_info;
};

// This compares times
struct CompareHomeEvents
{
    bool operator()( const HomeEvent& event1, const HomeEvent& event2 ) const
    {
        if( event1.getEventTime() == event2.getEventTime())
        {
            return ( event1.getEventId() < event2.getEventId() );
        }
        return event1.getEventTime() < event2.getEventTime();
    }
    bool operator()( const HomeEvent* event1, const HomeEvent* event2 ) const
    {
        if( event1->getEventTime() == event2->getEventTime())
        {
            return ( event1->getEventId() < event2->getEventId() );
        }
        return event1->getEventTime() < event2->getEventTime();
    }
};

class HomeEventManager
{
    public:
        HomeEventManager( HomeSystem* system );
        ~HomeEventManager() {;}

        bool addEvent( HomeEvent* event );
        bool addEvents( std::vector< HomeEvent* > events );

        // NOTE: Any event retrived this way will be moved into the _waiting_for_ack map.
        //       The events much be called with acknowledgeEvent/s to be removed.
        HomeEvent* getEventToProcess( time_t time );
        bool getEventsToProcess( time_t time, std::vector< HomeEvent* >& events );

        bool acknowledgeEvent( HomeEvent* event );
        bool acknowledgeEvents( std::vector< HomeEvent* >& events );
        bool clearAllEvents( );
        bool clearEvents( int type ); // NOTE: type is a bitfield for HomeEvent::EVENT_ORIGINATOR_TYPE

        std::string toString( bool include_events, bool include_waiting_for_ack ) const;
        json getEvents( bool include_events, bool include_waiting_for_ack ) const;

        bool generateEventsTable( json& results, const json& options, PiUtils::TEXT_FORMAT text_format ) const;
    private:
        HomeSystem* _system;
        mutable std::mutex _events_mutex; // This will protect _events AND _waiting_for_ack;
        mutable std::set< HomeEvent*, CompareHomeEvents> _events;
        mutable unsigned int _event_counter; // This is to ensure no duplicate events
        mutable std::set< HomeEvent*, CompareHomeEvents> _waiting_for_ack;

};

#endif
