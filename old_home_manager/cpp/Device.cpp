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

#include "include/uuid.h"
#include "Device.h"
#include "HomeConfig.h"
#include "HomeEventProcessors.h"
#include "EmailProcessor.h"
#include "HomeSystem.h"
#include "JSONUtils.h"
#include "Logger.h"
#include "PiUtils.h"

using namespace DeviceUtils;
using namespace JSONUtils;

Device* DeviceStore::getDeviceFromJSONString( const std::string& str, bool& skipped  )
{
    Logger::log( Logger::TRACE, "DeviceStore::getDeviceFromJSONString" );
    Device* device = 0;
    try {
        json jf = json::parse(str);
        // Check and see if it enabled. If not, skip it.
        if ( jf.contains( J_ENABLED ) )
        {
            if ( !jf.at( J_ENABLED ) )
            {
                Logger::log( Logger::INFO, "Device not enabled: " + jf.dump() );
                skipped = true;
                return device;
            }
        }
        // Do something different based on type
        if ( !jf.contains( J_TYPE ) )
        {
            Logger::log( Logger::WARNING, J_TYPE + " does not exist: " + jf.dump() );
        }
        else if ( jf.at( J_TYPE ) == J_DS18B20 )
        {
            device = Device_TemperatureSensor_DS18B20::getDevice( _system, jf );
        }
        else if ( jf.at( J_TYPE ) == J_EMAIL_CHECK )
        {
            device = Device_EmailCheck::getDevice( _system, jf );
        }
        else if ( jf.at( J_TYPE ) == J_RELAY )
        {
            device = Device_Relay::getDevice( _system, jf );
        }
        else if ( jf.at( J_TYPE ) == J_VS_PUMP )
        {
            device = Device_VSPump::getDevice( _system, jf );
        }
        else if ( jf.at( J_TYPE ) == J_GPIO_DEVICE )
        {
            device = Device_GPIOBase::getDevice( _system, jf );
        }
        else if ( jf.at( J_TYPE ) == J_DEVICE_GARAGE_DOOR )
        {
            device = Device_GarageDoor::getDevice( _system, jf );
        }
        else if ( jf.at( J_TYPE ) == J_DEVICE_WASHING_MACHINE )
        {
            device = Device_WashingMachine::getDevice( _system, jf );
        }
        else
        {
            Logger::log( Logger::WARNING, "Skipping unknown device: " + jf.dump() );
            skipped = true;
        }
        if ( device )
            Logger::log( Logger::INFO, "Added " + std::string( jf.at( J_TYPE ) ) + " device with name: " + device->getName() );
        else
            Logger::log( Logger::ERROR, "Failed to add " + std::string( jf.at( J_TYPE ) ) + " device: " + jf.dump() );
    }
    catch (...)
    {
        Logger::log( Logger::ERROR, "Exception thrown while trying to parse json string." + str );
        if ( device )
            delete device;
        device = 0;
    }
    return device;
}

DeviceStore::DeviceStore( HomeSystem* system )
    : _system( system )
{
    _devices.clear();
}

DeviceStore::~DeviceStore()
{
    clearDevices();
}

void DeviceStore::clearDevices()
{
    Logger::log( Logger::TRACE, "DeviceStore::clearDevices" );
    if ( _devices.empty() )
    {
        return; // Nothing to do
    }
    std::vector<Device*>::iterator itr = _devices.begin();
    while ( itr != _devices.end() )
    {
        delete *itr;
        itr = _devices.begin();
    }
    _devices.clear();
}

bool DeviceStore::loadDevices( const HomeConfig* config, bool clear )
{
    Logger::log( Logger::TRACE, "DeviceStore::loadDevices" );
    if ( !config ) { return false; }
    if ( clear )
        clearDevices();
    std::vector< std::string > device_strings;
    if ( !config->getDeviceStrings( device_strings ) )
        return false;
    bool success = true;
    for( std::vector< std::string >::iterator itr = device_strings.begin(); itr != device_strings.end(); ++itr )
    {
        Logger::log( Logger::TRACE, "DeviceStore::loadDevices going to load: " + *itr );
        // Load up each device as appropriate
        bool skipped = false;
        Device* device = getDeviceFromJSONString( *itr, skipped );
        if ( device )
        {
            Logger::log( Logger::INFO, "Adding device "+ device->getName());
            _devices.push_back( device );
        }
        else if ( skipped )
        {
            Logger::log( Logger::INFO, "Skipped load device from string: " + *itr );
        }
        else
        {
            Logger::log( Logger::WARNING, "unable to load device from string: " + *itr );
            success = false;
        }
    }
    if ( !success )
        clearDevices();
    else
        success = initDevices();
    return success;
}

void DeviceStore::getDevices( std::vector< Device* >& devices ) const
{
    devices = _devices;
}

Device* DeviceStore::getDevice( const std::string name ) const
{
    for( std::vector< Device* >::const_iterator itr = _devices.begin(); itr != _devices.end(); ++itr )
    {
        if ( (*itr)->getName() == name )
        {
            return *itr;
        }
    }
    return 0;
}

std::string DeviceStore::getDevicesAsString( const PiUtils::TEXT_FORMAT text_format ) const
{
    std::string str = "Currently monitoring ";
    str += std::to_string( _devices.size() );
    str += " devices.";
    str += PiUtils::getLineEndDelimiter( text_format );
    for( std::vector< Device* >::const_iterator itr = _devices.begin(); itr != _devices.end(); ++itr )
    {
        const Device* device = *itr;
        json desc = device->getDescriptionJSON();
        _system->getEvaluationActionManager()->addEvaluationActionsForDevice( device, desc );

        str += JSONUtils::getJSONStringValue( desc, J_NAME, "No name" );
        str += " with capability";
        if ( desc.contains( J_EVALUATION_ACTIONS ) )
        {
            str += " has the following evaluation actions:";
            json actions = desc[ J_EVALUATION_ACTIONS ];
            for( json::const_iterator j_itr = actions.begin(); j_itr != actions.end(); ++j_itr )
            {
                str += PiUtils::getLineEndDelimiter( text_format );
                str += (*j_itr).dump();
            }
        }
        else
        {
            str += PiUtils::getLineEndDelimiter( text_format );
            str += " with no thresholds";
        }
        str += PiUtils::getLineEndDelimiter( text_format );
    }
    return str;
}

bool DeviceStore::initDevices( )
{
    bool success = true;
    for( std::vector< Device* >::const_iterator itr = _devices.begin(); itr != _devices.end(); ++itr )
    {
        if ( !(*itr)->initialize() )
        {
            success = false;
        }
    }
    return success;
}

bool Device::initialize( )
{
    if ( _uuid == "" )
    {
        if (_system == nullptr) {
            Logger::log( Logger::ERROR, "Device::initialize: system is null, cannot generate UUID for device: " + _name );
            return false;
        }
        else if (_system->getHomeConfig() == nullptr) {
            Logger::log( Logger::ERROR, "Device::initialize: home config is null, cannot generate UUID for device: " + _name );
            return false;
        }
        else if (_name.empty()) {
            Logger::log( Logger::ERROR, "Device::initialize: device name is empty, cannot generate UUID" );
            return false;
        }
        std::string namespace_uuid_str = "aba7b810-9dad-11d1-80b4-00c04fd430c8"; // Randomly chosen namespace UUID
        auto namespace_uuid_opt = uuids::uuid::from_string(namespace_uuid_str);
        if (!namespace_uuid_opt.has_value()) {
            Logger::log(Logger::ERROR, "Device::initialize: failed to parse fixed namespace UUID");
            return false;
        }
        uuids::uuid namespace_uuid = namespace_uuid_opt.value();
        std::string name_seed = _system->getHomeConfig()->getSystemName() + ":" + _name;
        _uuid = uuids::to_string(uuids::uuid_name_generator{namespace_uuid}(name_seed));
    }
    return true;
}

bool Device::evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation )
{
    evaluation.device = this;
    std::string str;
    PiUtils::getYYMMDD_HHMMSSDisplayTime( evaluation_time, str );
    evaluation.evaluation_metadata[ J_TIME ] = str;
    evaluation.evaluation_metadata[ J_NAME ] = _name;
    evaluation.evaluation_time = evaluation_time;
    return true;
}

json Device::getDescriptionJSON() const
{
    json desc;
    desc[ J_NAME ] = getName();
    desc[ J_TYPE ] = getTypeAsString( getType() );
    desc[ J_UUID ] = getUUID();
    return desc;
}

const static std::string DS18B20_ID_STR = "id";
const static std::string DS18B20_TEMP_C_STR = "temp_c_override";
bool Device_TemperatureSensor_DS18B20::initializeDS18B20( HomeSystem* system, json j )
{
    Logger::log( Logger::TRACE, "Device_TemperatureSensor_DS18B20::initializeDS18B20: " + j.dump() );
    // Need to make sure the pull up resistor is set.
    if ( j.contains( J_DS18B20_ONE_WIRE_PIN ) )
    {
        int gpio_num = JSONUtils::getJSONIntValue( j, J_DS18B20_ONE_WIRE_PIN, -1 );
        if ( gpio_num >= 0 )
        {
            Logger::log( Logger::TRACE, "Device_TemperatureSensor_DS18B20::initializeDS18B20 Pulling up gpio: " + std::to_string( gpio_num ) );
            system->getGPIOManager()->requestGPIO( gpio_num,
                                                   0,
                                                   GPIOManager::__PUD_UP,
                                                   GPIOManager::__INPUT,
                                                   true);
        }
        else
            Logger::log( Logger::WARNING, "Device_TemperatureSensor_DS18B20::initializeDS18B20: unable to pull up" + j.dump() );
    }
    return true;
}

Device_TemperatureSensor_DS18B20::Device_TemperatureSensor_DS18B20( const std::string& name,
                                                                    const std::string& id,
                                                                    const std::string& results_temp_c_string,
                                                                    const std::string& uuid,
                                                                    HomeSystem* system ) :
    Device( name, uuid, system )
    , _id( id )
    , _results_temp_c_string( results_temp_c_string )
{
    if ( _results_temp_c_string.empty() )
    {
        _results_temp_c_string = std::string( _name + " - " + DeviceUtils::getLongNameForProperty( DeviceUtils::TEMP_C ) );
    }
}

Device_TemperatureSensor_DS18B20* Device_TemperatureSensor_DS18B20::getDevice( HomeSystem* system, json jf )
{
    Logger::log( Logger::TRACE, "Device_TemperatureSensor_DS18B20::getDevice: " + jf.dump() );
    Device_TemperatureSensor_DS18B20* device = new Device_TemperatureSensor_DS18B20(
            JSONUtils::getJSONStringValue( jf, J_NAME, "no name" ),
            JSONUtils::getJSONStringValue( jf, DS18B20_ID_STR, "no ds18b20 id" ),
            JSONUtils::getJSONStringValue( jf, DS18B20_TEMP_C_STR, "" ),
            JSONUtils::getJSONStringValue( jf, J_UUID, "" ),
            system );
    // Check and see if there are any evaluation_actions
    if( jf.contains( J_EVALUATION_ACTIONS ) )
    {
        Logger::log( Logger::TRACE, "Device_TemperatureSensor_DS18B20::getDevice: has action");
        json actions = jf.at( J_EVALUATION_ACTIONS );
        for( json::const_iterator j_itr = actions.begin(); j_itr != actions.end(); ++j_itr )
        {
            // Make sure the name is there
            json action = *j_itr;
            Logger::log( Logger::TRACE, "Device_TemperatureSensor_DS18B20::getDevice: action:" + action.dump() );
            if ( !action.contains( J_DEVICE_NAME ) )
            {
                action[ J_DEVICE_NAME ] = device->getName();
            }
            system->getEvaluationActionManager()->addEvaluationAction( action );
        }
    }
    else
    {
        Logger::log( Logger::TRACE, "Device_TemperatureSensor_DS18B20::getDevice: has no actions");
    }
    return device;
}

bool Device_TemperatureSensor_DS18B20::evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation )
{
    Logger::log( Logger::TRACE, "Device_TemperatureSensor_DS18B20::evaluate" );
    if ( Device::evaluate( evaluation_time, evaluation ) )
    {
        std::map< std::string, DeviceUtils::EVALUATION_PROPERTY_TYPE > properties;
        float temp = getTemperatureC();
        evaluation.evaluation_validation[ DeviceUtils::getShortNameForProperty( DeviceUtils::TEMP_C ) ] = temp;
        evaluation.evaluation_results[ _results_temp_c_string ] = temp;
        json results;
        results[ J_PROPERTY_NAME ] = DeviceUtils::getShortNameForProperty( DeviceUtils::TEMP_C );
        results[ J_PROPERTY_LONG_NAME ] = _results_temp_c_string;
        results[ J_PROPERTY_TYPE ] = DeviceUtils::getNameForPropertyType( TYPE_FLOAT, true );
        results[ J_VAL ] = temp;
        std::vector< json > results_vec;
        results_vec.push_back( results );
        evaluation.results = results_vec;
        properties[DeviceUtils::getShortNameForProperty( DeviceUtils::TEMP_C )] = DeviceUtils::TYPE_FLOAT;
        evaluation.evaluation_metadata[ J_PROPERTY_NAMES ] = properties;
    }
    return true;
}

// TEMPERATURE
float Device_TemperatureSensor_DS18B20::getTemperatureC() const {
    Logger::log( Logger::TRACE, "Device_TemperatureSensor_DS18B20::getTemperatureC for id:" + _id + "." );
    char sensor_path[256];
    FILE *fp;
    char status_buf[256];
    char temp_buf[256];
    char *temp_str;
    int temp_raw;

    // Build the path to the sensor file using the provided device ID
    snprintf(sensor_path, sizeof(sensor_path), "/sys/bus/w1/devices/%s/w1_slave", _id.c_str());

    // Open the sensor data file
    fp = fopen(sensor_path, "r");
    if (fp == NULL) {
        perror("Failed to open the sensor file");
        return -1.0f;
    }

    // Read the file contents - read the first line
    if (fgets(status_buf, sizeof(status_buf), fp) == NULL) {
        perror("Failed to read the sensor file");
        fclose(fp);
        return -1.0f;
    }

    // Read the second line for temperature data
    if (fgets(temp_buf, sizeof(temp_buf), fp) == NULL) {
        perror("Failed to read the second line of the sensor file");
        fclose(fp);
        return -1.0f;
    }

    // Close the file
    fclose(fp);
    // Check if the read data is valid
    if (strstr(status_buf, "YES") == NULL) {
        fprintf(stderr, "Sensor data not valid\n");
        return -3.0f;
    }

    // Find the temperature data (on the second line)
    temp_str = strstr(temp_buf, "t=");
    if (temp_str == NULL) {
        fprintf(stderr, "Temperature data not found\n");
        return -4.0f;
    }

    // Convert the raw temperature data (t=XXXXX) into an integer
    temp_raw = atoi(temp_str + 2);

    // Convert to Celsius (raw value divided by 1000)
    return temp_raw / 1000.0f;
}

float Device_TemperatureSensor_DS18B20::getTemperatureF( ) const
{
    float temp = getTemperatureC( );
    if ( DeviceUtils::isValValid( temp ) )
    {
        return PiUtils::convertCelciusToFahrenheit( temp );
    }
    return INVALID_FLOAT_VAL;
}

json Device_TemperatureSensor_DS18B20::getDescriptionJSON() const
{
    json desc = Device::getDescriptionJSON();
    std::vector< std::string> caps;
    caps.push_back( _results_temp_c_string );
    desc[ J_CAPABILITIES_LIST ] = caps;
    return desc;
}

Device_Relay::Device_Relay( const std::string& name, unsigned int gpio_num, const std::string& uuid, HomeSystem* system )
    : Device( name, uuid, system )
    , _gpio_num( gpio_num )
    , _initted( false )
    , _gpio_manager( system->getGPIOManager() )
    , _cycles( 0 )
    , _on_state_val( GPIOManager::__HIGH ) // Default, can change on init
    , _off_state_val( GPIOManager::__LOW )
    , _pud_state( GPIOManager::__PUD_OFF )
    , _pin_mode( GPIOManager::__INPUT )
    , _exclusive( true )
    , _initial_state( DeviceUtils::UNKNOWN_STATE )
{
}

const static std::string GPIO_ON_OFF_REVERSED_STR = "gpio_reversed";
const static std::string GPIO_PUD_STATE = "gpio_pud_state";
const static std::string GPIO_PIN_MODE = "gpio_pin_mode";
const static std::string GPIO_EXCLUSIVE = "gpio_exclusive";
const static std::string GPIO_INITIAL_STATE = "gpio_initial_state";

Device_Relay* Device_Relay::getDevice( HomeSystem* system, json jf )
{
    Logger::log( Logger::TRACE, "Device_Relay::getDevice" );
    Device_Relay* device = new Device_Relay(
            JSONUtils::getJSONStringValue( jf, J_NAME, "no name" ),
            JSONUtils::getJSONIntValue( jf, J_GPIO_NUM, 99999 ),
            JSONUtils::getJSONStringValue( jf, J_UUID, "" ),
            system );
    if ( jf.contains( GPIO_ON_OFF_REVERSED_STR ) )
    {
        device->setOnOffReversed( jf.at( GPIO_ON_OFF_REVERSED_STR ) );
    }
    if ( jf.contains( GPIO_PUD_STATE ) )
    {
        device->setPUDState( GPIOManager::getPUDStateFromString( jf.at( GPIO_PUD_STATE ) ) );
    }
    if ( jf.contains( GPIO_PIN_MODE ) )
    {
        device->setPinMode( GPIOManager::getPinModeFromString( jf.at( GPIO_PIN_MODE ) ) );
    }
    if ( jf.contains( GPIO_EXCLUSIVE ) )
    {
        device->setExclusive( jf.at( GPIO_EXCLUSIVE ) );
    }
    if ( jf.contains( GPIO_INITIAL_STATE ) )
    {
        device->setInitialState( DeviceUtils::getStateFromString( jf.at( GPIO_INITIAL_STATE ) ) );
    }
    return device;
}

bool Device_Relay::initialize( )
{
    Logger::log( Logger::INFO, "Device_Relay::initialize");
    if ( _gpio_num < 0 )
        return false;
    if ( !_initted )
    {
        Logger::log( Logger::INFO, "Device_Relay::requestGPIO:");
        _initted = _gpio_manager->requestGPIO( _gpio_num,
                                               this,
                                               _pud_state,
                                               _pin_mode,
                                               true );
        if ( _initted )
        {
            setState( _initial_state );
        }
    }
    return _initted;
}

bool Device_Relay::evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation )
{
    if ( Device::evaluate( evaluation_time, evaluation ) )
    {
        evaluation.evaluation_validation[ std::string( DeviceUtils::getShortNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( getState() );
        evaluation.evaluation_results[ std::string( _name + " - " + DeviceUtils::getLongNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( getState() );
    }
    return true;
}

bool Device_Relay::processCommand( json command_json, json& result_json )
{
    Logger::log( Logger::TRACE, "Device_Relay::processCommand" );
    // Determine what to do.
    // A relay needs to look at J_COMMAND
    std::string command_str = JSONUtils::getJSONStringValue( command_json, J_COMMAND, "" );
    DEVICE_COMMAND cmd = getCommandFromString( command_str );
    if ( cmd == DeviceUtils::TURN_ON )
    {
        // Just set the state
        return setState( DeviceUtils::ON );
    }
    else if ( cmd == DeviceUtils::TURN_OFF )
    {
        // Just set the state
        return setState( DeviceUtils::OFF );
    }
    else
    {
        Logger::log( Logger::WARNING, "Device_Relay::processCommand invalid command: " + command_str );
        return false;
    }
    return false; // Should never get here
}

void Device_Relay::setOnOffReversed( bool reverse )
{
    if ( reverse )
    {
        _off_state_val = GPIOManager::__HIGH;
        _on_state_val = GPIOManager::__LOW;
    }
    else
    {
        _on_state_val = GPIOManager::__HIGH;
        _off_state_val = GPIOManager::__LOW;
    }
}

DeviceUtils::DEVICE_STATE Device_Relay::getState( ) const
{
    Logger::log( Logger::TRACE, "Device_Relay::getState" );
    if ( _initted )
    {
        if ( _gpio_manager->testGPIO( _gpio_num, _on_state_val ) )
        {
            return DeviceUtils::ON;
        }
        else if ( _gpio_manager->testGPIO( _gpio_num, _off_state_val ) )
        {
            return DeviceUtils::OFF;
        }
    }
    return UNKNOWN_STATE;
}

bool Device_Relay::setState( DeviceUtils::DEVICE_STATE state )
{
    Logger::log( Logger::TRACE, "Device_Relay::setState" );
    // Get the current state.
    DeviceUtils::DEVICE_STATE cur_state = getState();
    if ( state == DeviceUtils::UNKNOWN_STATE )
    {
        return UNKNOWN_STATE; // Don't do anything
    }
    if ( cur_state == state )
    {
        return cur_state; // We are done
    }
	++_cycles;
    if ( DeviceUtils::ON == state )
    {
        _gpio_manager->setGPIO( _gpio_num, _on_state_val );
    }
    else if ( DeviceUtils::OFF == state )
    {
        _gpio_manager->setGPIO( _gpio_num, _off_state_val );
    }
	_gpio_manager->sleep(1); // Sleep for 1 cycle of whatever the GPIOManager thinks
    return getState() == state;
}

Device_VSPump::Device_VSPump( const std::string& name, const std::string& uuid, HomeSystem* system )
    : Device( name, uuid, system )
    , _initted( false )
    , _gpio_manager( system->getGPIOManager() )
    , _cycles( 0 )
    , _on_state_val( GPIOManager::__HIGH ) // Default, can change on init
    , _off_state_val( GPIOManager::__LOW )
    , _pud_state( GPIOManager::__PUD_OFF )
    , _pin_mode( GPIOManager::__OUTPUT )
    , _exclusive( true )
    , _initial_state( DeviceUtils::UNKNOWN_STATE )
{
}

const static std::string RELAY_INP1 = "relay_inp1";
const static std::string RELAY_INP2 = "relay_inp2";
const static std::string RELAY_INP3 = "relay_inp3";
const static std::string RELAY_PUMP_ENABLE = "relay_pump_enable";
Device_VSPump* Device_VSPump::getDevice( HomeSystem* system, json jf )
{
    Logger::log( Logger::TRACE, "Device_VSPump::getDevice" );
    if ( !jf.contains( J_RELAY ) )
    {
        Logger::log( Logger::WARNING, "Device_VSPump::getDevice no relays" );
        return 0;
    }

    Device_VSPump* device = new Device_VSPump(
                                JSONUtils::getJSONStringValue( jf, J_NAME, "no name" ),
                                JSONUtils::getJSONStringValue( jf, J_UUID, "" ),
                                system );
    if ( jf.contains( GPIO_ON_OFF_REVERSED_STR ) )
    {
        device->setOnOffReversed( jf.at( GPIO_ON_OFF_REVERSED_STR ) );
    }
    if ( jf.contains( GPIO_PUD_STATE ) )
    {
        device->setPUDState( GPIOManager::getPUDStateFromString( jf.at( GPIO_PUD_STATE ) ) );
    }
    if ( jf.contains( GPIO_PIN_MODE ) )
    {
        device->setPinMode( GPIOManager::__OUTPUT );
    }
    if ( jf.contains( GPIO_EXCLUSIVE ) )
    {
        device->setExclusive( true );
    }
    if ( jf.contains( GPIO_INITIAL_STATE ) )
    {
        device->setInitialState( DeviceUtils::getStateFromString( jf.at( GPIO_INITIAL_STATE ) ) );
    }
    // Need to get out the relays
    // We can have up to 4 relays.
    json relays_json = jf.at( J_RELAY );
    for( json::const_iterator j_itr = relays_json.begin(); j_itr != relays_json.end(); ++j_itr )
    {
        if ( !(*j_itr).contains( J_NAME ) )
            continue;
        if ( RELAY_INP1 == (*j_itr).at( J_NAME ) )
        {
            device->setRelayInput( 1, *j_itr );
        }
        if ( RELAY_INP2 == (*j_itr).at( J_NAME ) )
        {
            device->setRelayInput( 2, *j_itr );
        }
        if ( RELAY_INP3 == (*j_itr).at( J_NAME ) )
        {
            device->setRelayInput( 3, *j_itr );
        }
        if ( RELAY_PUMP_ENABLE == (*j_itr).at( J_NAME ) )
        {
            device->setRelayPumpEnable( *j_itr );
        }
    }
    return device;
}

bool Device_VSPump::setRelayInput( unsigned int input, json relay_json )
{
    Device_Relay* relay = Device_Relay::getDevice( _system, relay_json );
    if ( !relay )
    {
        Logger::log( Logger::ERROR, "Device_VSPump::setRelayInput can't create relay for input: " + std::to_string( input ) + " for json: " + relay_json.dump());
        return false;
    }
    Logger::log( Logger::TRACE, "Device_VSPump::setRelayInput input: " + std::to_string( input ) + " for json: " + relay_json.dump());
    switch( input )
    {
        case 1:
            _relay_inp1 = relay;
            break;
        case 2:
            _relay_inp2 = relay;
            break;
        case 3:
            _relay_inp3 = relay;
            break;
        default:
            Logger::log( Logger::ERROR, "Device_VSPump::setRelayInput Unknown input: " + std::to_string( input ) + " for json: " + relay_json.dump());
            delete relay;
            return false;
    }
    return true;
}

bool Device_VSPump::setRelayPumpEnable( json relay_json )
{
    _relay_pump_enable = Device_Relay::getDevice( _system, relay_json );
    return _relay_pump_enable;
}
   
bool Device_VSPump::initialize( )
{
    Logger::log( Logger::INFO, "Device_VSPump::initialize");
    if ( !_initted )
    {
        if ( _relay_inp1 && _relay_inp2 && _relay_inp3 && _relay_pump_enable )
        {
            _initted = _relay_inp1->initialize();
            _initted = _initted && _relay_inp2->initialize();
            _initted = _initted && _relay_inp3->initialize();
            _initted = _initted && _relay_pump_enable->initialize();
        }
        else
        {
            Logger::log( Logger::WARNING, "Device_VSPump::initialize not all relays found.");
        }
        if ( _initted )
        {
            Logger::log( Logger::TRACE, "Device_VSPump::initted");
            setState( _initial_state );
        }
        else
        {
            Logger::log( Logger::ERROR, "Device_VSPump::initted failed");
        }
    }
    return _initted;
}

bool Device_VSPump::evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation )
{
    if ( Device::evaluate( evaluation_time, evaluation ) )
    {
        evaluation.evaluation_validation[ std::string( DeviceUtils::getShortNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( getState() );
        evaluation.evaluation_results[ std::string( _name + " - " + DeviceUtils::getLongNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( getState() );
    }
    return true;
}

DeviceUtils::DEVICE_STATE getStateFromSpeed( unsigned int speed, DeviceUtils::DEVICE_STATE unknown )
{
    switch( speed )
    {
        case 1:
            return DeviceUtils::SPEED_1;
        case 2:
            return DeviceUtils::SPEED_2;
        case 3:
            return DeviceUtils::SPEED_3;
        case 4:
            return DeviceUtils::SPEED_4;
        case 5:
            return DeviceUtils::SPEED_5;
        case 6:
            return DeviceUtils::SPEED_6;
        case 7:
            return DeviceUtils::SPEED_7;
        case 8:
            return DeviceUtils::SPEED_8;
        case 0:
            return DeviceUtils::OFF;
    }
    return unknown;
}

bool Device_VSPump::processCommand( json command_json, json& result_json )
{
    Logger::log( Logger::TRACE, "Device_VSPump::processCommand" );
    // Determine what to do.
    // A relay needs to look at J_COMMAND
    std::string command_str = JSONUtils::getJSONStringValue( command_json, J_COMMAND, "" );
    DEVICE_COMMAND cmd = getCommandFromString( command_str );
    DeviceUtils::DEVICE_STATE state = DeviceUtils::OFF;
    if( DeviceUtils::TURN_OFF == cmd )
    {
        state = DeviceUtils::OFF;
    }
    else if ( DeviceUtils::SET_SPEED == cmd )
    {
        int speed = 0;
        if ( command_json.contains( J_SPEED ) )
        {
            speed = command_json.at( J_SPEED );
        }
        state = getStateFromSpeed( speed, DeviceUtils::OFF );
    }
    else
    {
        Logger::log( Logger::ERROR, "Device_VSPump::processCommand invalid command: " + command_str );
        return false;
    }
    return setState( state );
}

void Device_VSPump::setOnOffReversed( bool reverse )
{
    if ( reverse )
    {
        _off_state_val = GPIOManager::__HIGH;
        _on_state_val = GPIOManager::__LOW;
    }
    else
    {
        _on_state_val = GPIOManager::__HIGH;
        _off_state_val = GPIOManager::__LOW;
    }
    // Set this to all relays
}

DeviceUtils::DEVICE_STATE Device_VSPump::getState( ) const
{
    Logger::log( Logger::TRACE, "Device_VSPump::getState" );
    if ( _initted )
    {
        // Calculate based on # of inputs
        unsigned int val = 1;
        if ( _relay_pump_enable )
        {
            if ( _relay_pump_enable->getState() == DeviceUtils::ON )
            {
                if ( _relay_inp1->getState() == DeviceUtils::ON )
                    val += 1;
                if ( _relay_inp2->getState() == DeviceUtils::ON )
                    val += 2;
                if ( _relay_inp3->getState() == DeviceUtils::ON )
                    val += 4;
            }
        }
        return getStateFromSpeed( val, UNKNOWN_STATE );
    }
    return UNKNOWN_STATE;
}

bool Device_VSPump::setState( DeviceUtils::DEVICE_STATE state )
{
    Logger::log( Logger::TRACE, "Device_VSPump::setState to state " + std::string( DeviceUtils::getStateAsString( state ) ) );
    // Get the current state.
    DeviceUtils::DEVICE_STATE cur_state = getState();
    if ( state == DeviceUtils::UNKNOWN_STATE )
    {
        return UNKNOWN_STATE; // Don't do anything
    }
    if ( cur_state == state )
    {
        return cur_state; // We are done
    }
	++_cycles;
    bool inp1_on = false;
    bool inp2_on = false;
    bool inp3_on = false;
    bool pump_enable = true;
    switch( state )
    {
        case DeviceUtils::SPEED_1:
            break;
        case DeviceUtils::SPEED_2:
            inp1_on = true;
            break;
        case DeviceUtils::SPEED_3:
            inp2_on = true;
            break;
        case DeviceUtils::SPEED_4:
            inp1_on = true;
            inp2_on = true;
            break;
        case DeviceUtils::SPEED_5:
            inp3_on = true;
            break;
        case DeviceUtils::SPEED_6:
            inp1_on = true;
            inp3_on = true;
            break;
        case DeviceUtils::SPEED_7:
            inp2_on = true;
            inp3_on = true;
            break;
        case DeviceUtils::SPEED_8:
            inp1_on = true;
            inp2_on = true;
            inp3_on = true;
            break;
        default:
            pump_enable = false;
            inp1_on = false;
            inp2_on = false;
            inp3_on = false;
            break;
    }
    // If the pump is going to be disabled, go ahead and turn if off first.
    if ( !pump_enable ) 
    {
        _relay_pump_enable->setState( DeviceUtils::OFF );
    }
    _relay_inp3->setState( inp3_on ? DeviceUtils::ON : DeviceUtils::OFF );
    _relay_inp2->setState( inp2_on ? DeviceUtils::ON : DeviceUtils::OFF );
    _relay_inp1->setState( inp1_on ? DeviceUtils::ON : DeviceUtils::OFF );
    if ( pump_enable ) 
        _relay_pump_enable->setState( DeviceUtils::ON );
    return getState() == state;
}

Device_GPIOBase::Device_GPIOBase( const std::string& name, unsigned int gpio_num, const std::string& uuid, HomeSystem* system )
    : Device( name, uuid, system )
    , _gpio_num( gpio_num )
    , _initted( false )
    , _gpio_manager( system->getGPIOManager() )
    , _pud_state( GPIOManager::__PUD_UP )
    , _pin_mode( GPIOManager::__INPUT )
    , _on_hilo_val( GPIOManager::__LOW ) // Since we pull up, it will be high when no magnet is present
    , _exclusive( true )
{
}

Device_GPIOBase* Device_GPIOBase::getDevice( HomeSystem* system, json jf )
{
    Logger::log( Logger::TRACE, "Device_GPIOBase::getDevice" );
    // Check the type
    Device_GPIOBase* device = 0;
    std::string gpio_type = JSONUtils::getJSONStringValue( jf, J_GPIO_TYPE, "");
    if ( gpio_type == "reed_switch_normally_open") {
        device = Device_GPIOSwitch::getDevice(system, jf);
    }
    if ( !device )
    {
        Logger::log( Logger::ERROR, "Device_GPIOBase::getDevice Unable to get device of type: " + gpio_type );
        return 0;
    }
    if ( jf.contains( GPIO_PUD_STATE ) )
    {
        device->setPUDState( GPIOManager::getPUDStateFromString( jf.at( GPIO_PUD_STATE ) ) );
    }
    if ( jf.contains( J_GPIO_MODE ) )
    {
        device->setPinMode( GPIOManager::getPinModeFromString( jf.at( J_GPIO_MODE ) ) );
    }
    device->setExclusive( true );
    return device;
}

bool Device_GPIOBase::initialize( )
{
    Logger::log( Logger::INFO, "Device_GPIOBase::initialize");
    if ( _gpio_num < 0 )
        return false;
    if ( !_initted )
    {
        Logger::log( Logger::INFO, "Device_GPIOBase::requestGPIO");
        _initted = _gpio_manager->requestGPIO( _gpio_num,
                                               this,
                                               _pud_state,
                                               _pin_mode,
                                               true );
    }
    return _initted;
}

bool Device_GPIOBase::processCommand( json command_json, json& result_json )
{
    Logger::log( Logger::TRACE, "Device_GPIOBase::processCommand" );
    return false; // Can't do anything.
}

bool Device_GPIOBase::evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation )
{
    if ( Device::evaluate( evaluation_time, evaluation ) )
    {
        evaluation.evaluation_validation[ std::string( DeviceUtils::getShortNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( getState() );
        evaluation.evaluation_results[ std::string( _name + " - " + DeviceUtils::getLongNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( getState() );
    }
    return true;
}

DeviceUtils::DEVICE_STATE Device_GPIOBase::getState( ) const
{
    Logger::log( Logger::TRACE, "Device_GPIOBase::getState" );
    if ( _initted )
    {
        // getGPIO return __HIGH or __LOW.
        if ( _gpio_manager->getGPIO(_gpio_num) == _on_hilo_val )
            return DeviceUtils::ON;
        return DeviceUtils::OFF;
    }
    return DeviceUtils::UNKNOWN_STATE;
}


Device_GPIOSwitch::Device_GPIOSwitch( const std::string& name, unsigned int gpio_num, const std::string& uuid, HomeSystem* system )
    : Device_GPIOBase( name, gpio_num, uuid, system )
    , _prev_state( DeviceUtils::UNKNOWN_STATE )
    , _cycles( 0 )
    , _chip_path("/dev/gpiochip0") // TODO Get this from somewhere
    , _running(false)
    , _chip(nullptr)
    , _line(nullptr)
    , _use_interrupts( PiUtils::TriState::No)
{
}

Device_GPIOSwitch::~Device_GPIOSwitch( void )
{
    stopMonitoring();
    if (_line) {
        gpiod_line_release(_line);
    }
    if (_chip) {
        gpiod_chip_close(_chip);
    }
}

bool Device_GPIOSwitch::initialize( )
{
    if ( _initted )
    {
        return true;
    }
    if ( !Device_GPIOBase::initialize())
    {
        return _initted;
    }
    #if 0
    Logger::log( Logger::INFO, "Device_GPIOSwitch::initialize");
    // Open the GPIO chip
    _chip = gpiod_chip_open(_chip_path.c_str());
    if (!_chip) {
        Logger::log( Logger::WARNING, "Failed to open chip: " + _chip_path + "\n");
        _initted = false;
        return _initted;
    }

    // Get the GPIO line
    _line = gpiod_chip_get_line(_chip, _gpio_num);
    if (!_line) {
        Logger::log( Logger::WARNING, "Failed to get line: " + std::to_string( _gpio_num ) + "\n");
        gpiod_chip_close(_chip);
        _chip = nullptr;
        _initted = false;
        return _initted;
    }

    // Request the line as an input with edge detection
    if (gpiod_line_request_both_edges_events(_line, "Device_GPIOSwitch") < 0) {
        Logger::log( Logger::WARNING, "Failed to request line as input with edge detection\n");
        gpiod_line_release(_line);
        _line = nullptr;
        gpiod_chip_close(_chip);
        _chip = nullptr;
        _initted = false;
        return _initted;
    }
    #endif
    return _initted;
}

Device_GPIOSwitch* Device_GPIOSwitch::getDevice( HomeSystem* system, json jf )
{
    Logger::log( Logger::TRACE, "Device_GPIOSwitch::getDevice" );
    Device_GPIOSwitch* device = new Device_GPIOSwitch(
            JSONUtils::getJSONStringValue( jf, J_NAME, "no name" ),
            JSONUtils::getJSONIntValue( jf, J_GPIO_NUM, 99999 ),
            JSONUtils::getJSONStringValue( jf, J_UUID, "" ),
            system );
    device->setPinMode( GPIOManager::__INPUT ); // A switch is only an input
    return device;
}

DeviceUtils::DEVICE_STATE Device_GPIOSwitch::getState( ) const
{
    Logger::log( Logger::TRACE, "Device_GPIOSwitch::getState" );
    DeviceUtils::DEVICE_STATE state = Device_GPIOBase::getState();
    if ( ( DeviceUtils::UNKNOWN_STATE == state ) || ( state == _prev_state ) )
        return state;
    ++_cycles;
    return state;
}

// Start monitoring for interrupts
void Device_GPIOSwitch::startMonitoring() {
    if (_running) return;

    _running = true;
    _monitor_thread = std::thread(&Device_GPIOSwitch::monitorInterrupts, this);
}

// Stop monitoring for interrupts
void Device_GPIOSwitch::stopMonitoring() {
    _running = false;
    if (_monitor_thread.joinable()) {
        _monitor_thread.join();
    }
}

// Private method to monitor events
void Device_GPIOSwitch::monitorInterrupts() {
    while (_running) {
        // Wait for an event (with a timeout of 1 second to check `running`)
        struct timespec timeout = {1, 0}; // 1 second
        int ret = gpiod_line_event_wait(_line, &timeout);

        if (ret < 0) {
            std::cerr << "Error waiting for event\n";
            break;
        } else if (ret == 0) {
            // Timeout - continue checking `running`
            continue;
        }

        // Read the event
        struct gpiod_line_event event;
        if (gpiod_line_event_read(_line, &event) < 0) {
            std::cerr << "Error reading event\n";
            break;
        }
        time_t evaluation_time;
        time (&evaluation_time);
        // Update internal state based on the event
        {
            std::lock_guard<std::mutex> lock(_interrupt_mutex);
            if (event.event_type == GPIOD_LINE_EVENT_RISING_EDGE) {
                _interrupt_states.emplace_back(evaluation_time, DeviceUtils::ON);
            } else if (event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE) {
                _interrupt_states.emplace_back(evaluation_time, DeviceUtils::OFF);
            }
        }
    }
}

Device_GarageDoor::Device_GarageDoor( const std::string& name, const std::string& uuid, HomeSystem* system, Device_GPIOSwitch* up, Device_GPIOSwitch* down )
    : Device( name, uuid, system )
    , _initted( false )
    , _gpio_manager( system->getGPIOManager() )
    , _cycles( 0 )
    , _up( up )
    , _down( down )
    , _last_evaluation_state( UNKNOWN_STATE )
{
}

Device_GarageDoor* Device_GarageDoor::getDevice( HomeSystem* system, json jf )
{
    Logger::log( Logger::TRACE, "Device_GarageDoor::getDevice" );
    if ( !jf.contains( J_DEVICE_GARAGE_UP ) )
    {
        Logger::log( Logger::WARNING, "Device_GarageDoor::getDevice Missing " + J_DEVICE_GARAGE_UP );
        return 0;
    }
    if ( !jf.contains( J_DEVICE_GARAGE_DOWN ) )
    {
        Logger::log( Logger::WARNING, "Device_GarageDoor::getDevice Missing " + J_DEVICE_GARAGE_DOWN );
        return 0;
    }
    // Look for the devices.
    Device_GPIOSwitch* up = dynamic_cast<Device_GPIOSwitch*>(system->getDeviceStore()->getDevice( jf.at( J_DEVICE_GARAGE_UP ) ));
    Device_GPIOSwitch* down = dynamic_cast<Device_GPIOSwitch*>(system->getDeviceStore()->getDevice( jf.at( J_DEVICE_GARAGE_DOWN ) ));
    if ( !up || !down)
    {
        Logger::log( Logger::WARNING, "Device_GarageDoor::getDevice missing up or down device " );
        return 0;
    }
    Device_GarageDoor* device = new Device_GarageDoor(
                                JSONUtils::getJSONStringValue( jf, J_NAME, "garage door" ),
                                JSONUtils::getJSONStringValue( jf, J_UUID, "" ),
                                system,
                                up,
                                down );
    // Check and see if there are any evaluation_actions
    if( jf.contains( J_EVALUATION_ACTIONS ) )
    {
        Logger::log( Logger::TRACE, "Device_GarageDoor::getDevice: has action");
        json actions = jf.at( J_EVALUATION_ACTIONS );
        for( json::const_iterator j_itr = actions.begin(); j_itr != actions.end(); ++j_itr )
        {
            // Make sure the name is there
            json action = *j_itr;
            Logger::log( Logger::TRACE, "Device_GarageDoor::getDevice: action:" + action.dump() );
            if ( !action.contains( J_DEVICE_NAME ) )
            {
                action[ J_DEVICE_NAME ] = device->getName();
            }
            system->getEvaluationActionManager()->addEvaluationAction( action );
        }
    }
    else
    {
        Logger::log( Logger::INFO, "Device_GarageDoor::getDevice: has no actions");
    }
    return device;
}

bool Device_GarageDoor::initialize( )
{
    Logger::log( Logger::TRACE, "Device_GarageDoor::initialize");
    if ( !_initted )
    {
        // Nothing to do?
        _initted = _up && _down;
    }
    return _initted;
}

bool Device_GarageDoor::evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation )
{
    if ( Device::evaluate( evaluation_time, evaluation ) )
    {
        DeviceUtils::DEVICE_STATE state = getState();
        evaluation.evaluation_validation[ std::string( DeviceUtils::getShortNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( state );
        evaluation.evaluation_validation[ std::string( DeviceUtils::getShortNameForProperty( DeviceUtils::PREVIOUS_STATE ) ) ] = DeviceUtils::getStateAsString( _last_evaluation_state );
        evaluation.evaluation_results[ std::string( _name + " - " + DeviceUtils::getLongNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( state );
        _last_evaluation_state = state;
    }
    return true;
}

bool Device_GarageDoor::processCommand( json /*command_json*/, json& /*result_json*/ )
{
    Logger::log( Logger::TRACE, "Device_GarageDoor::processCommand NOT_IMPLEMENTED" );
    return false;
}

DeviceUtils::DEVICE_STATE Device_GarageDoor::getState( ) const
{
    Logger::log( Logger::TRACE, "Device_GarageDoor::getState" );
    if ( _initted )
    {
        bool up_state = _up->getState() == DeviceUtils::ON;
        bool down_state = _down->getState() == DeviceUtils::ON;

        if ( up_state && !down_state)
        {
            return DeviceUtils::UP;
        }
        else if ( !up_state && down_state)
        {
            return DeviceUtils::DOWN;
        }        
        else if ( !up_state && !down_state)
        {
            return DeviceUtils::NEITHER_UP_NOR_DOWN;
        }
        else
        {
            // Impossible.
            Logger::log( Logger::WARNING, "Device_GarageDoor::getState is UNKNOWN_STATE" );
            return DeviceUtils::UNKNOWN_STATE;
        }
    }
    return UNKNOWN_STATE;
}

Device_WashingMachine::Device_WashingMachine( const std::string& name, const std::string& uuid, HomeSystem* system, Device_GPIOSwitch* lid_up )
    : Device( name, uuid, system )
    , _initted( false )
    , _gpio_manager( system->getGPIOManager() )
    , _cycles( 0 )
    , _lid_up( lid_up )
    , _last_evaluation_state( UNKNOWN_STATE )
{
}

Device_WashingMachine* Device_WashingMachine::getDevice( HomeSystem* system, json jf )
{
    Logger::log( Logger::TRACE, "Device_WashingMachine::getDevice" );
    if ( !jf.contains( J_DEVICE_WASHING_MACHINE_LID_UP ) )
    {
        Logger::log( Logger::WARNING, "Device_WashingMachine::getDevice Missing " + J_DEVICE_WASHING_MACHINE_LID_UP );
        return 0;
    }

    // Look for the devices.
    Device_GPIOSwitch* lid_up = dynamic_cast<Device_GPIOSwitch*>(system->getDeviceStore()->getDevice( jf.at( J_DEVICE_WASHING_MACHINE_LID_UP ) ));
    if ( !lid_up )
    {
        Logger::log( Logger::WARNING, "Device_WashingMachine::getDevice missing lid_up device " );
        return 0;
    }
    Device_WashingMachine* device = new Device_WashingMachine(
                                JSONUtils::getJSONStringValue( jf, J_NAME, "washing machine" ),
                                JSONUtils::getJSONStringValue( jf, J_UUID, "" ),
                                system,
                                lid_up
                                );
    // Check and see if there are any evaluation_actions
    if( jf.contains( J_EVALUATION_ACTIONS ) )
    {
        Logger::log( Logger::TRACE, "Device_WashingMachine::getDevice: has action");
        json actions = jf.at( J_EVALUATION_ACTIONS );
        for( json::const_iterator j_itr = actions.begin(); j_itr != actions.end(); ++j_itr )
        {
            // Make sure the name is there
            json action = *j_itr;
            Logger::log( Logger::TRACE, "Device_WashingMachine::getDevice: action:" + action.dump() );
            if ( !action.contains( J_DEVICE_NAME ) )
            {
                action[ J_DEVICE_NAME ] = device->getName();
            }
            system->getEvaluationActionManager()->addEvaluationAction( action );
        }
    }
    else
    {
        Logger::log( Logger::INFO, "Device_WashingMachine::getDevice: has no actions");
    }
    return device;
}

bool Device_WashingMachine::initialize( )
{
    Logger::log( Logger::TRACE, "Device_WashingMachine::initialize");
    if ( !_initted )
    {
        // Nothing to do?
        _initted = _lid_up;
    }
    return _initted;
}

bool Device_WashingMachine::evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation )
{
    if ( Device::evaluate( evaluation_time, evaluation ) )
    {
        DeviceUtils::DEVICE_STATE state = getState();
        evaluation.evaluation_validation[ std::string( DeviceUtils::getShortNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( state );
        evaluation.evaluation_validation[ std::string( DeviceUtils::getShortNameForProperty( DeviceUtils::PREVIOUS_STATE ) ) ] = DeviceUtils::getStateAsString( _last_evaluation_state );
        evaluation.evaluation_results[ std::string( _name + " - " + DeviceUtils::getLongNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( state );
        _last_evaluation_state = state;
    }
    return true;
}

bool Device_WashingMachine::processCommand( json /*command_json*/, json& /*result_json*/ )
{
    Logger::log( Logger::TRACE, "Device_WashingMachine::processCommand NOT_IMPLEMENTED" );
    return false;
}

DeviceUtils::DEVICE_STATE Device_WashingMachine::getState( ) const
{
    Logger::log( Logger::TRACE, "Device_WashingMachine::getState" );
    if ( _initted )
    {
        bool lid_up_state = _lid_up->getState() == DeviceUtils::ON;

        if ( lid_up_state )
        {
            return DeviceUtils::UP;
        }
        else
        {
            // Might not be down, but it certainly isn't up, so assume it is down.
            return DeviceUtils::DOWN;
        }
    }
    return UNKNOWN_STATE;
}

Device_EmailCheck::Device_EmailCheck( const std::string& label, unsigned int max_frequency_seconds, const std::string& uuid, HomeSystem* system )
    : Device( "Email Check", uuid, system )
    , _email_label( label )
    , _max_frequency_seconds( max_frequency_seconds )
    , _last_check_time( 0 )
    , _next_earliest_check_time( 0 )
{
}

Device_EmailCheck* Device_EmailCheck::getDevice( HomeSystem* system, json jf )
{
    // We need the script as well as the label to look at.

    Logger::log( Logger::TRACE, "Device_EmailCheck::getDevice" );
    if ( !jf.contains( J_EMAIL_CHECK_LABEL ) )
    {
        Logger::log( Logger::WARNING, "Device_EmailCheck::getDevice Missing " + J_EMAIL_CHECK_LABEL );
        return 0;
    }
    Device_EmailCheck* device = new Device_EmailCheck(
                                JSONUtils::getJSONStringValue( jf, J_EMAIL_CHECK_LABEL, system->getHomeConfig()->getSystemName()),
                                (unsigned int) JSONUtils::getJSONIntValue( jf, J_EMAIL_CHECK_FREQUENCY_SECONDS, 60 ),
                                JSONUtils::getJSONStringValue( jf, J_UUID, ""),
                                system);
    // Check and see if there are any evaluation_actions
    if( jf.contains( J_EVALUATION_ACTIONS ) )
    {
        Logger::log( Logger::TRACE, "Device_EmailCheck::getDevice: has action");
        json actions = jf.at( J_EVALUATION_ACTIONS );
        for( json::const_iterator j_itr = actions.begin(); j_itr != actions.end(); ++j_itr )
        {
            json action = *j_itr;
            Logger::log( Logger::TRACE, "Device_EmailCheck::getDevice: action:" + action.dump() );
            system->getEvaluationActionManager()->addEvaluationAction( action );
        }
    }
    else
    {
        Logger::log( Logger::INFO, "Device_EmailCheck::getDevice: has no actions");
    }
    return device;

}

bool Device_EmailCheck::initialize( )
{
    Logger::log( Logger::TRACE, "Device_EmailCheck::initialize");
    if ( !_initted )
    {
        // Nothing to do?
        _initted = _email_label.size() > 0;
    }
    return _initted;
}

bool Device_EmailCheck::evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation )
{
    if ( Device::evaluate( evaluation_time, evaluation ) )
    {
        if ( evaluation_time > _next_earliest_check_time )
        {
            // Check the email. No matter what (ie even on failure), we want to wait at least _max_frequency_seconds
            _next_earliest_check_time = PiUtils::getTimeInAdditionHourMinuteSeconds(0, 0, 0, _max_frequency_seconds, evaluation_time);

            // Step 1, just download the emails.
            json email_options;
            email_options[J_EMAIL_CHECK_LABEL] = "monitor";
            json received_emails;
            if ( EmailProcessor::checkEmail(_system, email_options, received_emails) )
            {
                Logger::log( Logger::TRACE, "Device_EmailCheck::evaluate passed" );
            }
            else
            {
                
            }

            // Based on the results, we may want to do different things for different commands. For now, just get the emails and let the "Evaluation" handle it.
            //evaluation.evaluation_validation[ std::string( DeviceUtils::getShortNameForProperty( DeviceUtils::STATE ) ) ] = DeviceUtils::getStateAsString( state );
            //evaluation.evaluation_validation[ std::string( DeviceUtils::getShortNameForProperty( DeviceUtils::PREVIOUS_STATE ) ) ] = DeviceUtils::getStateAsString( _last_evaluation_state );
            evaluation.evaluation_results[ std::string( _name + " - " + _email_label ) ] = "Downloaded " + PiUtils::tostr(received_emails.size()) + " emails";          
            evaluation.evaluation_results[ std::string( J_EMAIL_RECEIVED_EMAILS ) ] = received_emails;          
       }
        else
        {
            std::string next_time_str;
            PiUtils::getPrettyDisplayTime(_next_earliest_check_time, next_time_str);
            evaluation.evaluation_results[ std::string( _name + " - " + _email_label ) ] = "Skipping until " + next_time_str;          
        }

    }
    return true;
}

bool Device_EmailCheck::processCommand( json /*command_json*/, json& /*result_json*/ )
{
    Logger::log( Logger::TRACE, "Device_EmailCheck::processCommand UNIMPLEMENTED" );
    return false;
}
