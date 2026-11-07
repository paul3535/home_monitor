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
#include "HomeConfig.h"
#include "HomeSystem.h"
#include "Device.h"
#include "HomeEventProcessors.h"
#include "JSONUtils.h"
#include "Logger.h"
#include "EmailProcessor.h"

using namespace JSONUtils;


bool processRelay( HomeEvent* event, HomeSystem* system )
{
    // This is mostly handled by the relay. We just need to tell it if it should be ON or OFF
    // Set all the relay devices found in the list as appropriate

    DeviceStore* device_store = system->getDeviceStore();
    bool success = true;

    json event_info = event->getEventInfo();
    Logger::log( Logger::TRACE, "HomeEventProcessor::processRelay start with event_info: " + event_info.dump() );

    if ( !event_info.contains( J_DEVICE ) )
    {
        Logger::log( Logger::WARNING, "HomeEventProcessor::processRelay no device." );
        return false;
    }
    json devices = event_info.at( J_DEVICE );
    for( json::iterator itr = devices.begin(); itr != devices.end(); ++itr )
    {
        Logger::log( Logger::TRACE, "HomeEventProcessor::processRelay for device: " + (*itr).dump() );
        // Check if it is a relay
        if ( DeviceUtils::RELAY != DeviceUtils::getTypeFromString( JSONUtils::getJSONStringValue( (*itr), J_DEVICE_TYPE, "" ) ) )
        {
            Logger::log( Logger::WARNING, "HomeEventProcessor::processRelay Not a RELAY : " + (*itr).dump() );
            continue;
        }

        Device* device = device_store->getDevice( JSONUtils::getJSONStringValue( (*itr), J_NAME, "" ) );

        json result;
        if ( device )
        {
            success = device->processCommand( (*itr), result );
        }
        else
        {
            Logger::log( Logger::ERROR, "HomeEventProcessor::processRelay Can't find RELAY : " + (*itr).dump() );
            success = false;
        }
    }
    if ( !success )
        Logger::log( Logger::ERROR, std::string( "HomeEventProcessor::processRelay failed for event: " ) + event->toString() );
    return success;
}

bool processVSPump( HomeEvent* event, HomeSystem* system )
{
    DeviceStore* device_store = system->getDeviceStore();
    bool success = true;

    json event_info = event->getEventInfo();
    Logger::log( Logger::TRACE, "HomeEventProcessor::processVSPump start with event_info: " + event_info.dump() );

    if ( !event_info.contains( J_DEVICE ) )
    {
        Logger::log( Logger::ERROR, "HomeEventProcessor::processVSPump no device." );
        return false;
    }
    json devices = event_info.at( J_DEVICE );
    for( json::iterator itr = devices.begin(); itr != devices.end(); ++itr )
    {
        Logger::log( Logger::TRACE, "HomeEventProcessor::processVSPump for device: " + (*itr).dump() );
        if ( DeviceUtils::VS_PUMP != DeviceUtils::getTypeFromString( JSONUtils::getJSONStringValue( (*itr), J_DEVICE_TYPE, "" ) ) )
        {
            Logger::log( Logger::ERROR, "HomeEventProcessor::processVSPump Not a VS_PUMP : " + (*itr).dump() );
            continue;
        }

        Device* device = device_store->getDevice( JSONUtils::getJSONStringValue( (*itr), J_NAME, "" ) );

        json result;
        if ( device )
        {
            success = device->processCommand( (*itr), result );
        }
        else
        {
            Logger::log( Logger::ERROR, "HomeEventProcessor::processVSPump Can't find VS_PUMP : " + (*itr).dump() );
            success = false;
        }
    }
    if ( !success )
        Logger::log( Logger::ERROR, std::string( "HomeEventProcessor::processVSPump failed for event: " ) + event->toString() );
    return success;
}

bool processCommandEvent( HomeEvent* event, HomeSystem* system )
{
    Logger::log( Logger::TRACE, "HomeEventProcessor::processCommandEvent" );

    json results;
    time_t time = PiUtils::getCurrentTime_t();
    json event_info = event->getEventInfo();
    DeviceUtils::DEVICE_COMMAND cmd = DeviceUtils::getCommandFromString( JSONUtils::getJSONStringValue( event_info, J_COMMAND, DeviceUtils::getCommandAsString( DeviceUtils::INVALID_DEVICE_COMMAND ) ) );
    json options;
    switch( cmd )
    {
        case DeviceUtils::GET_STATUS:
        case DeviceUtils::SEND_STARTUP:
            return EmailProcessor::processCommandEmailEvent( event, system );
        default:
            Logger::log( Logger::WARNING, "HomeEventProcessor::processCommandEvent: Invalid command: " + event_info.dump() );
            return false;
            break;
    }
    return true;
}
bool HomeEventProcessor::process( HomeEvent* event, HomeSystem* system )
{
    Logger::log( Logger::TRACE, "HomeEventProcessor::process" );
    // Get out the type. The private "process?????Event" calls do most of the work.
    HomeEventProcessor::EVENT_PROCESSOR_TYPE type = getEventProcessorTypeFromString( event->getEventProcessorTypeString() );
    switch( type )
    {
        case RELAY:
            return processRelay( event, system );
        case VS_PUMP:
            return processVSPump( event, system );
        case EMAIL:
            return EmailProcessor::processSendEmail( event, system );
        case RUN_COMMAND:
            return processCommandEvent( event, system );
        default:
            Logger::log( Logger::ERROR, "HomeEventProcessor::process no processor for: " + event->getEventProcessorTypeString() );
            Logger::log( Logger::ERROR, "HomeEventProcessor::process no processor for event: " + event->getEventInfo(true).dump() );
            return false;
    }
    return true;
}

