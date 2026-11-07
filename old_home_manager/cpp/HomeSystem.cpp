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

#include "Device.h"
#include "DeviceUtils.h"
#include "GPIOManager.h"
#include "PiUtils.h"
#include "HomeConfig.h"
#include "HomeSystem.h"
#include "Logger.h"
//#include "SensorMonitor.h"
#ifdef USE_GPIOCLASS_FOR_GPIO
#include "RaspberryPi-GPIOClass-v1/GPIOClass.h"
#endif

static bool _display_debug = false;

//using namespace DeviceUtils;

HomeSystem::HomeSystem( HomeConfig* config )
    : _config( config )
    , _cycle( 0 )
    , _device_store( 0 )
    , _event_manager( 0 )
    , _gpio_manager( 0 )
{
    _device_store = new DeviceStore( this );
    _evaluation_store = new Evaluation::EvaluationStore( this );
    _evaluation_action_manager = new Evaluation::EvaluationActionManager( this );
    _event_manager = new HomeEventManager( this );
    _gpio_manager = 0;
    if ( config->getGPIOEnabled() )
    {
        if ( config->getGPIOManagerType() == GPIOManager::GPIO_PIGPIO)
            _gpio_manager = GPIOManager::createPiGPIOGPIO( this );
        else if ( config->getGPIOManagerType() == GPIOManager::GPIO_LIBGPIOD)
            _gpio_manager = GPIOManager::createLibGPIOdGPIO( this );
        else
            Logger::log( Logger::ERROR, "HomeSystem gpio_enabled is true but gpio_manager_type is disabled" );
        if ( _gpio_manager )
        {
            if ( !_gpio_manager->initGPIO() )
            {
                Logger::log( Logger::ERROR, "HomeSystem failed to init GPIO" );
            }
        }
    }
    else
    {
        Logger::log( Logger::INFO, "HomeSystem skipped GPIO initialization" );
    }
    initializeDevices( );
    if ( config->getSendStartup() )
    {
        Logger::log( Logger::INFO, "HomeSystem J_SEND_STARTUP" );
        json startup_info;
        startup_info[ JSONUtils::J_VERSION ] = JSONUtils::J_CURRENT_VERSION;
        startup_info[ JSONUtils::J_TIME_IMMEDIATE ] = true;
        startup_info[ JSONUtils::J_COMMAND ] = DeviceUtils::getCommandAsString(DeviceUtils::SEND_STARTUP);
        startup_info[ JSONUtils::J_EVENT_PROCESSOR_TYPE ] = "run_command"; // TODO Get rid of this
        addHomeEvent( startup_info, HomeEvent::STARTUP );
    }

}

HomeSystem::~HomeSystem()
{
}

bool HomeSystem::initializeDevices( )
{
    Logger::log( Logger::TRACE, "HomeSystem::initializeDevices");
    // Go through all config files, load them up and add the json strings for the config info
    std::vector< std::string > config_files;
    _config->getConfigFiles( config_files );
    for( std::vector< std::string >::const_iterator itr = config_files.begin(); itr != config_files.end(); ++itr )
    {
        // Load up the file and convert it to json. Each device will be in it's own section.
        // NOTE: There can/will be multiple devices in each file.
        try
        {
            std::ifstream ifs(*itr);
            json jf = json::parse(ifs);
            if ( jf.contains( JSONUtils::J_DS18B20_CONFIG ) )
            {
                json ds18b20_config = jf[ JSONUtils::J_DS18B20_CONFIG ];
                Device_TemperatureSensor_DS18B20::initializeDS18B20( this, ds18b20_config );
            }
        }
        catch (...)
        {
            Logger::log( Logger::WARNING, "Exception caught while parsing " + *itr );
            return false;
        }
    }
    return true;
}

HomeSystem* HomeSystem::getHomeSystem( HomeConfig* config )
{
    if ( !config ) { return 0; }
    Logger::log( Logger::INFO, "HomeSystem::getHomeSystem called" );
    HomeSystem* home_system = new HomeSystem( config );
    // Load the devices from the file
    if ( !home_system->refreshFromConfig() )
    {
        Logger::log( Logger::ERROR, "HomeSystem::refreshFromConfig failed" );
        delete home_system;
        home_system = 0;
    }
    return home_system;
}

bool HomeSystem::addHomeEvent( json event, HomeEvent::EVENT_ORIGINATOR_TYPE originator )
{
    Logger::log( Logger::TRACE, "HomeSystem::addHomeEvent" );
    HomeEvent* home_event = HomeEvent::getEventFromJSON( event, originator );
    if ( home_event )
    {
        return _event_manager->addEvent( home_event );
    }
    Logger::log( Logger::WARNING, "HomeSystem::addHomeEvent faled" );
    return false;
}

bool HomeSystem::refreshFromConfig()
{
    Logger::log( Logger::INFO, "HomeSystem::refreshFromConfig" );
    if ( !_config )
        return false;
    _evaluation_store->setLoggingEnabled( _config->getLoggingEnabled() );
    std::string logging_location = _config->getLoggingBaseDir() + "/" + _config->getSystemName();

    // Make sure the directory exists
    std::string dir = PiUtils::getDirectoryFromFilename( logging_location );
    std::string cmd = "mkdir -p " + dir;
    PiUtils::runSystemCommand( cmd.c_str() );
    std::string new_cmd = cmd + "/email";
    PiUtils::runSystemCommand( new_cmd.c_str() );
    new_cmd = cmd + "/email/sent";
    PiUtils::runSystemCommand( new_cmd.c_str() );

    _evaluation_store->setLogFileBaseNameLocation( logging_location );

    // Get out the devices
    if ( !_device_store->loadDevices( _config, true ) )
    {
        Logger::log( Logger::ERROR, "HomeSystem::refreshFromConfig failed to load devices" );
        return false;
    }
    Logger::log( Logger::TRACE, "HomeSystem::refreshFromConfig load devices success" );
    std::vector<HomeEvent*> events;
    std::vector< std::string > event_files;
    _config->getEventFiles( event_files );
    for( std::vector< std::string >::const_iterator itr = event_files.begin(); itr != event_files.end(); ++itr )
    {
        Logger::log( Logger::TRACE, "HomeSystem::refreshFromConfig looking at event_file " + *itr);
        if ( !HomeEvent::getEventsFromFile( *itr, events, HomeEvent::SCHEDULED ) )
        {
            Logger::log( Logger::ERROR, "HomeSystem::refreshFromConfig failed to getEventsFromFile: " + *itr );
            return false;
        }
        if ( !_event_manager->addEvents( events ) )
        {
            Logger::log( Logger::ERROR, "HomeSystem::refreshFromConfig failed to add Events.");
            return false;
        }
        
    }
    Logger::log( Logger::TRACE, _event_manager->toString( true, true ) );
    return true;
}

bool HomeSystem::evaluateDevices( time_t evaluation_time, std::vector< Evaluation::device_evaluation >& evaluations )
{
    Logger::log( Logger::TRACE, "HomeSystem::evaluateDevices" );
    // Get all Devices from the _device_store
    // Iterate through all of them calling "process"
    std::vector< Device* > devices;
    _device_store->getDevices( devices );
    for( std::vector< Device* >::iterator itr = devices.begin(); itr != devices.end(); ++itr )
    {
        Evaluation::device_evaluation eval;
        if ( (*itr)->evaluate( evaluation_time, eval ) )
        {
            evaluations.push_back( eval );
        }
    }
    return true;
}

bool HomeSystem::processAnalyzedEvaluations( time_t evaluation_time, json analyzed_results )
{
    bool success = true;
    for( json::iterator itr = analyzed_results.begin(); itr != analyzed_results.end(); ++itr )
    {
        json results = (*itr);
        // First, check for success
        if ( !results.contains( JSONUtils::J_EVALUATION_SUCCESS ) )
        {
            Logger::log( Logger::WARNING, "HomeSystem::processAnalyzedEvaluations no " + JSONUtils::J_EVALUATION_SUCCESS + results.dump() );
            continue;
        }
        if ( results.at( JSONUtils::J_EVALUATION_SUCCESS ) )
        {
            // Even a successfull event can trigger new events. Don't call continue
            //continue;
        }
        // We had a failure. Determine what (if anything) to do. It is more than possible to have a failure with no
        // actions, if, for example, no notification was needed to but we still wanted to log the change in state
        // of the analyzer
        if ( results.contains( JSONUtils::J_NEW_EVENT ) )
        {
            json events = results.at( JSONUtils::J_NEW_EVENT );
            if ( events.is_array() )
            {
                for( json::iterator e_itr = events.begin(); e_itr != events.end(); ++e_itr )
                    addHomeEvent( *e_itr, HomeEvent::ANALYZED ) && success;
            }
            else
            {
                addHomeEvent( events, HomeEvent::ANALYZED ) && success;
            }
        }
    }
    return true;
}

bool HomeSystem::processEvents( time_t evaluation_time )
{
    Logger::log( Logger::TRACE, "HomeSystem::processEvents" );
    bool success = true;
    HomeEvent* event_to_process = _event_manager->getEventToProcess( evaluation_time );
    while( event_to_process )
    {
        Logger::log( Logger::TRACE, "HomeSystem::processEvents going to process event: " + event_to_process->toString() );
        success = event_to_process->process( this ) && success;
        _event_manager->acknowledgeEvent( event_to_process );
        event_to_process = _event_manager->getEventToProcess( evaluation_time );
    }
    return success;
    
}

// NOTE! This is meant to be called as a thread
bool HomeSystem::runSystem()
{
    Logger::log( Logger::INFO, "HomeSystem::runSystem" );
/*
evsluate devides
store result
analyze results
  - check boundaries
  - check new states, if anything is triggered by a state change
  - note: this may add nrw events
check events ti run
while not wll events processed
  - run event. events csn produce new events. if that hapens, add the tp the current list (if they need to be run
    now)
slee until next run.

*/
#if 0
    std::string error_string = "";
    loadStartupEventsFromFile( "", error_string );
    if ( _config->sendStartup() )
    {
        DeviceUtils::command_options options;
        options._command = DeviceUtils::SEND_STARTUP;
        options._text_format = _config->getEmailTextFormat();
        // Create a new email event
        HomeEvent* event = new HomeEvent();
        event->setType( HomeEvent::SCHEDULED );
        event->setEventTime( 0 ); // Now
        event->addCommandOptions( options );
        addEvent( event, true );
    }
#endif
        _evaluation_action_manager->initializeActions( this );

    unsigned int sleep_time = _config->getEvaluationFrequencySeconds();
    while( true )
    {
        Logger::log( Logger::TRACE, "HomeSystem::runSystem starting loop" );
        // Evaluate the DeviceGroups
        std::string cycle_error_message = "Errors for cycle: ";
        cycle_error_message.append( PiUtils::tostr( _cycle ) );
        cycle_error_message.append( "\n" );
        bool success = true;
        std::string error_message = "";
        time_t evaluation_time;
        time (&evaluation_time);
        std::vector< Evaluation::device_evaluation > evaluations;

        if ( !evaluateDevices( evaluation_time, evaluations ) )
        {
            success = false;
        }
        Logger::log( Logger::TRACE, std::string( "HomeSystem::runSystem dumping evaluations with success: " ) + ( success ? "true" : "false" ) );
        for( std::vector< Evaluation::device_evaluation >::iterator itr = evaluations.begin(); itr != evaluations.end(); ++itr )
        {
            Logger::log( Logger::TRACE, (*itr).evaluation_results.dump() );
        }
        if ( success )
        {
            // If logging has been enabled, these will be stored to disk.
            success = _evaluation_store->addEvaluations( evaluation_time, evaluations );
        }
        // Need to analyze the evaluations. We could end up with some more events (probably emails)
        // Analyze the evaluations
        json analyzed_results = _evaluation_action_manager->runEvaluationActions( evaluations );
        if ( !processAnalyzedEvaluations( evaluation_time, analyzed_results ) )
        {
            //cycle_error_message.append( error_message );
            success = false;
        }
        if ( !processEvents( evaluation_time ) )
        {
            success = false;
        }
        // Sleep until next cycle;
        unsigned int time_to_sleep = PiUtils::timeLeftToSleep( evaluation_time, sleep_time );
        Logger::log( Logger::INFO, "HomeSystem::runSystem sleeping for " + std::to_string( time_to_sleep ) + " seconds.");
        sleep( time_to_sleep );
        ++_cycle;
    }
    return true;
}
