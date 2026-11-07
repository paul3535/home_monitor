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

#include "HomeEvent.h"
#include "HomeSystem.h"
#include "JSONUtils.h"
#include "Logger.h"
#include "HomeEventProcessors.h"

using namespace JSONUtils;

HomeEvent::HomeEvent()
    : _originator_type( UNKNOWN )
    , _event_time( 0 )
    , _processor_type_str( "" )
{
}

HomeEvent::~HomeEvent()
{
    //std::cerr << "Going to delete:";
    //dump();
}

HomeEvent* HomeEvent::getEventFromJSON( json object, HomeEvent::EVENT_ORIGINATOR_TYPE type )
{
    Logger::log( Logger::TRACE, "HomeEvent::getEventsFromJSON: " + std::string( object.dump(2)) );
    if ( JSONUtils::getJSONIntValue( object, J_VERSION, 0 ) != J_CURRENT_VERSION ) // Only support J_CURRENT_VERSION
    {
        Logger::log( Logger::WARNING, "HomeEvent::getEventsFromJSON skipping incompatible version: " + object.dump() );
        return 0;
    }
    if ( !JSONUtils::getJSONBoolValue( object, J_ENABLED, true ) )
    {
        Logger::log( Logger::TRACE, "HomeEvent::getEventsFromJSON skipping disabled event: " + object.dump() );
        return 0;
    }
    // Get event type
    HomeEvent* event = new HomeEvent( );
    event->setOriginatorType( type );
    event->setEventTime( JSONUtils::getTimeFromJSON( object ) );
    event->addEventInfo( object );
    event->setOriginatorType( type );
    event->setEventProcessorTypeString( JSONUtils::getJSONStringValue( object, J_EVENT_PROCESSOR_TYPE, "" ) );
    Logger::log( Logger::TRACE, "HomeEvent::getEventsFromJSON: done");
    return event;
}

bool HomeEvent::getEventsFromFile( std::string fn, std::vector<HomeEvent*>& events, int events_to_clear )
{
    Logger::log( Logger::INFO, "HomeEvent::getEventsFromFile" + fn );
    if ( events_to_clear )
    {
        std::cout << "TODO fix the clear";
        events.clear();
    }

    std::ifstream ifs( fn );
    json jf = json::parse(ifs);
    for( json::iterator j_itr = jf.begin(); j_itr != jf.end(); ++j_itr )
    {
        for( json::iterator j_itr2 = (*j_itr).begin(); j_itr2 != (*j_itr).end(); ++j_itr2 )
        {
            Logger::log( Logger::TRACE, "HomeEvent::getEventsFromFile loop" );
            json event_json = *j_itr2;
            time_t time = JSONUtils::getTimeFromJSON_futureDayIfInPast( event_json, 30 );
            JSONUtils::clearTimeFromJSON( event_json );
            event_json[ JSONUtils::J_TIME ] = time;
            // Each of these is an event. What we really care about is the time at the moment
            HomeEvent* event = getEventFromJSON( event_json, SCHEDULED );
            if ( event )
            {
                events.push_back( event );
                event->dump();
            }
        }
    }
    Logger::log( Logger::INFO, "HomeEvent::getEventsFromFile added in " + std::to_string( events.size() ) + " event(s)." );
    return true;
}

void HomeEvent::setEventId( unsigned int id )
{
    _event_info[ J_EVENT_ID ] = id;
}

unsigned int HomeEvent::getEventId() const
{
    return ( JSONUtils::getJSONIntValue( _event_info, J_EVENT_ID, 0 ) );
}

void HomeEvent::dump() const
{
    std::cout << toString() << "\n";
}

std::string HomeEvent::toString( ) const
{
    std::string str;
    str = HomeEvent::getEventOriginatorTypeAsString( _originator_type );
    str += " ";
    std::string event_time;
    PiUtils::getPrettyDisplayTime( getEventTime(), event_time );
    str += event_time;
    str += " ";
    str += _event_info.dump();
    return str;
}

void HomeEvent::setFutureTimeOfEvent( int future_in_seconds )
{
   time_t rawtime;
   time (&rawtime);
   rawtime += future_in_seconds;
   setEventTime( rawtime );
}

json HomeEvent::getEventInfo( bool include_extra_info ) const
{
    if ( !include_extra_info )
        return _event_info;
    json info = _event_info; 
    std::string event_time;
    PiUtils::getPrettyDisplayTime( getEventTime(), event_time );
    info[ J_RUN_TIME ] = event_time;
    info[ J_EVENT_ORIGINATOR_TYPE ] = HomeEvent::getEventOriginatorTypeAsString( _originator_type );
    return info;
}

bool HomeEvent::initialize( HomeSystem* system )
{
    // TODO this should make sure all needed devices are available.
    return true;
}

// NOTE: We only get here is an event has succeeded.
bool HomeEvent::repeatIfNecessary( HomeSystem* system ) const
{
    Logger::log( Logger::TRACE, "HomeEvent::repeatIfNecessary" + _event_info.dump() );
    // Check and see if the event includes repeats
    if ( !_event_info.contains( JSONUtils::J_REPEAT_SECONDS ) )
    {
        return false; // Not needed.
    }
    time_t new_time = JSONUtils::getTimeFromJSON( _event_info, true, getEventTime() );
    if ( new_time > 0 )
    {
        Logger::log( Logger::TRACE, "HomeEvent::repeatIfNecessary new time" );
        // NOTE: This needs to handle the type as being a bitfield...
        json event_info = _event_info;
        JSONUtils::clearTimeFromJSON( event_info );
        event_info[ J_TIME ] = new_time;
        return system->addHomeEvent( event_info, HomeEvent::REPEAT_EVENT );
    }
    Logger::log( Logger::TRACE, "HomeEvent::repeatIfNecessary no new time" );
    return false;
}

bool HomeEvent::process( HomeSystem* system )
{
    Logger::log( Logger::TRACE, "HomeEvent::process" );
    // Get out the type. The private "process?????Event" calls do most of the work.
    bool success = HomeEventProcessor::process( this, system );
    if ( success )
    {
        repeatIfNecessary( system );
    }
    else
    {
        Logger::log( Logger::ERROR, "HomeEvent::process failed" );
    }
    return success;
}

HomeEventManager::HomeEventManager( HomeSystem* system )
    : _system( system )
    , _event_counter( 1 )
{
    _events.clear();
    _waiting_for_ack.clear();
}

bool HomeEventManager::addEvent( HomeEvent* event )
{
    Logger::log( Logger::TRACE, "HomeEvent::addEvent" );
    // Don't bother checking the time (as it could have been set in the past to make sure it runs on the next
    // iteration
    if ( !event )
    {
        Logger::log( Logger::WARNING, "HomeEvent::addEvent event is null" );
        return false;
    }
    if ( !event->initialize( _system ) )
    {
        Logger::log( Logger::WARNING, "HomeEvent::addEvent event failed to initialize" );
        return false;
    }
    std::unique_lock<std::mutex> lck ( _events_mutex );
    event->setEventId( _event_counter++ );
    Logger::log( Logger::TRACE, "HomeEvent::addEvent added event: " + event->toString());
    _events.insert( event );
    return true;
}

// NOTE HomeEventManager takes ownership of the event
bool HomeEventManager::addEvents( std::vector< HomeEvent* > events )
{
    Logger::log( Logger::TRACE, "HomeEvent::addEvents" );
    // NOTE: Don't lock the mutex here. It is locked in addEvent
    bool success = true;
    for( std::vector< HomeEvent* >::iterator itr = events.begin(); itr != events.end(); ++itr )
    {
        success = addEvent( *itr ) && success;
    }
    return success;
}

HomeEvent* HomeEventManager::getEventToProcess( time_t time )
{
    Logger::log( Logger::TRACE, "HomeEvent::getEventToProcess" );
    std::unique_lock<std::mutex> lck ( _events_mutex ); // This will stay locked while in scope
    std::set<HomeEvent*>::iterator itr = _events.begin();
    if ( itr != _events.end() )
    {
        if ( (*itr)->getEventTime() < time )
        {
            HomeEvent* event = *itr;
            Logger::log( Logger::TRACE, "HomeEvent::getEventsToProcess going to process: " + event->toString() );
            _waiting_for_ack.insert( *itr );
            _events.erase( itr );
            return event;
        }
    }
    return 0;
}
// NOTE: Any event retrived this way will be moved into the _waiting_for_ack map.
//       The events much be called with acknowledgeEvent/s to be removed.
bool HomeEventManager::getEventsToProcess( time_t time, std::vector< HomeEvent* >& events )
{
    Logger::log( Logger::TRACE, "HomeEvent::getEventsToProcess" );
    events.clear();
    std::unique_lock<std::mutex> lck ( _events_mutex ); // This will stay locked while in scope
    while( !_events.empty() )
    {
        std::set<HomeEvent*>::iterator itr = _events.begin();
        if ( (*itr)->getEventTime() < time )
        {
            Logger::log( Logger::TRACE, "HomeEvent::getEventsToProcess going to process: " + (*itr)->toString() );
            events.push_back( *itr );
            _waiting_for_ack.insert( *itr );
            _events.erase( itr );
        }
        else
        {
            break;
        }
    }
    return true;
}

bool HomeEventManager::acknowledgeEvent( HomeEvent* event )
{
    Logger::log( Logger::TRACE, "HomeEvent::acknowledgeEvent" );
    std::unique_lock<std::mutex> lck ( _events_mutex ); // This will stay locked while in scope
    if ( !event )
    {
        Logger::log( Logger::WARNING, "HomeEvent::acknowledgeEvent event is null" );
        return false;
    }
    std::set< HomeEvent*, CompareHomeEvents >::iterator itr = _waiting_for_ack.find( event );
    if ( itr == _waiting_for_ack.end() )
    {
        Logger::log( Logger::WARNING, "HomeEvent::acknowledgeEvent event not found in _waiting_for_ack" );
        return false;
    }

    _waiting_for_ack.erase( itr );
    delete event; // Delete it here.
    return true;
}

bool HomeEventManager::acknowledgeEvents( std::vector< HomeEvent* >& events )
{
    Logger::log( Logger::TRACE, "HomeEvent::acknowledgeEvents with " + std::to_string( events.size() ) + " events" );
    // NOTE: Don't lock the mutex here. It is locked in acknowledgeEvent
    bool success = true;
    for( std::vector< HomeEvent* >::const_iterator itr = events.begin(); itr != events.end(); ++itr )
    {
        success = acknowledgeEvent( *itr ) && success;
    }
    return success;
}

bool HomeEventManager::clearAllEvents( )
{
    // TODO make this work.
    return false;
}

bool HomeEventManager::clearEvents( int type ) // NOTE: type is a bitfield for HomeEvent::EVENT_ORIGINATOR_TYPE
{
    // TODO make this work.
    return false;
}

std::string HomeEventManager::toString( bool include_events, bool include_waiting_for_ack ) const
{
    std::unique_lock<std::mutex> lck ( _events_mutex ); // This will stay locked while in scope
    Logger::log( Logger::TRACE, "HomeEventManager::toString()" );
    std::string str;
    if ( include_events )
    {
        str += "Events: ";
        for( std::set< HomeEvent*, CompareHomeEvents >::const_iterator itr = _events.begin(); itr != _events.end(); ++itr )
        {
            str += "\n";
            str += (*itr)->toString();
        }
    }
    if ( include_waiting_for_ack )
    {
        str += "Events waiting for ack: ";
        for( std::set< HomeEvent*, CompareHomeEvents >::const_iterator itr = _waiting_for_ack.begin(); itr != _waiting_for_ack.end(); ++itr )
        {
            str += "\n";
            str += (*itr)->toString();
        }
    }
    return str;
}

json HomeEventManager::getEvents( bool include_events, bool include_waiting_for_ack ) const
{
    std::unique_lock<std::mutex> lck ( _events_mutex ); // This will stay locked while in scope
    Logger::log( Logger::TRACE, "HomeEventManager::getEvents()" );
    std::vector< json > events_vec;
    std::vector< json > events_to_ack_vec;
    if ( include_events )
    {
        for( std::set< HomeEvent*, CompareHomeEvents >::const_iterator itr = _events.begin(); itr != _events.end(); ++itr )
        {
            events_vec.push_back( (*itr)->getEventInfo( true ) );
        }
    }
    if ( include_waiting_for_ack )
    {
        for( std::set< HomeEvent*, CompareHomeEvents >::const_iterator itr = _waiting_for_ack.begin(); itr != _waiting_for_ack.end(); ++itr )
        {
            events_to_ack_vec.push_back( (*itr)->getEventInfo( true ) );
        }
    }
    json events;
    events[ J_EVENT_INFO ] = events_vec;
    events[ J_EVENT_ACK_INFO ] = events_to_ack_vec;
    return events;
}

std::string generateTable( std::string header_str, const json& event_info, const json& options, PiUtils::TEXT_FORMAT text_format )
{
    Logger::log( Logger::TRACE, "generateTable()" );
    std::string table = header_str;
    table += PiUtils::getLineEndDelimiter( text_format );
    table += PiUtils::getTableStartDelimiter( text_format );
    // Header
    table += PiUtils::getTableLineStartDelimiter( text_format );
    // Time
    table += PiUtils::getTableCellStartDelimiter( text_format );
    table += "Time";
    table += PiUtils::getTableCellEndDelimiter( text_format );

    // Event
    table += PiUtils::getTableCellStartDelimiter( text_format );
    table += "Event type";
    table += PiUtils::getTableCellEndDelimiter( text_format );

    // Command (if exists)
    table += PiUtils::getTableCellStartDelimiter( text_format );
    table += "Command";
    table += PiUtils::getTableCellEndDelimiter( text_format );

    table += PiUtils::getTableLineEndDelimiter( text_format );
    for( json::const_iterator j_itr = event_info.begin(); j_itr != event_info.end(); ++j_itr )
    {
        table += PiUtils::getTableLineStartDelimiter( text_format );
        // Time
        table += PiUtils::getTableCellStartDelimiter( text_format );
        table += JSONUtils::getJSONStringValue( *j_itr, J_RUN_TIME, "" );
        table += PiUtils::getTableCellEndDelimiter( text_format );

        // Event
        table += PiUtils::getTableCellStartDelimiter( text_format );
        table += JSONUtils::getJSONStringValue( *j_itr, J_EVENT_PROCESSOR_TYPE, "" );
        table += PiUtils::getTableCellEndDelimiter( text_format );

        // Command (if exists)
        table += PiUtils::getTableCellStartDelimiter( text_format );
        table += JSONUtils::getJSONStringValue( *j_itr, J_COMMAND, "None" );
        table += PiUtils::getTableCellEndDelimiter( text_format );
    }
    table += PiUtils::getTableEndDelimiter( text_format );
    return table;
}

bool HomeEventManager::generateEventsTable( json& results, const json& options, PiUtils::TEXT_FORMAT text_format ) const
{
    Logger::log( Logger::TRACE, "generateEventsTable()" );
    json events_and_acks = getEvents( true, true );
    json event_info = events_and_acks[ J_EVENT_INFO ];
    results[ J_EVENTS_TABLE ] = generateTable( "Events", events_and_acks[ J_EVENT_INFO ], options, text_format );
    results[ J_EVENTS_ACK_TABLE ] = generateTable( "Events to ack:", events_and_acks[ J_EVENT_ACK_INFO ], options, text_format );
    return true;
}
