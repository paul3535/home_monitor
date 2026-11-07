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

//#include "HomeEvent.h"
#include "Device.h"
#include "HomeConfig.h"
#include "HomeEvent.h"
#include "PiUtils.h"
#include "GPIOManager.h"
#include "GPIOManager_pigpio.h"
#include "GPIOManager_Libgpiod.h"
#include "Logger.h"

using namespace DeviceUtils;

GPIOManager* GPIOManager::createPiGPIOGPIO( HomeSystem* system )
{
    GPIOManager* manager = new GPIOManager_Pigpio(system);
    if ( manager->initGPIO())
    {
        return manager;
    }
    delete manager;
    return 0;
}

GPIOManager* GPIOManager::createLibGPIOdGPIO( HomeSystem* system )
{
    GPIOManager* manager = new GPIOManager_Libgpiod(system);
    if ( manager->initGPIO())
    {
        return manager;
    }
    delete manager;
    return 0;
}

GPIOManager::GPIOManager( HomeSystem* system )
    : _system( system )
    , _initted( false )
{
    _requested_gpio.clear();
}

GPIOManager::~GPIOManager( )
{
}

bool GPIOManager::requestGPIO( int gpio_num,
                               Device* device,
                               PUD_STATE pud_state,
                               PIN_MODE pin_mode,
                               bool exclusive
                               )
{
    Logger::log( Logger::TRACE, "GPIOManager::requestGPIO:");
    if ( !_initted )
    {
        Logger::log( Logger::TRACE, "GPIOManager::requestGPIO: not initted");
        return false;
    }
    std::map< int, gpio_info >::iterator itr = _requested_gpio.find( gpio_num );
    if ( _requested_gpio.end() == itr )
    {
        Logger::log( Logger::TRACE, "GPIOManager::requestGPIO: new gpio");
        // We have a new one. Go ahead and add it to the map
        gpio_info info;
        info._gpio_num = gpio_num;
        info._pud_state = pud_state;
        info._pin_mode = pin_mode;
        info._devices.insert( device );
        info._exclusive = exclusive;
        _requested_gpio[gpio_num] = info;
        Logger::log( Logger::TRACE, "GPIOManager::requestGPIO: Setting gpio for pin " + std::to_string( gpio_num ) + " to getPinModeAsString: " + getPinModeAsString(pin_mode) + " and pud_state: " + getPUDStateAsString(pud_state));
        setPinMode( gpio_num, pin_mode );
        setPullUpDnControl( gpio_num, pud_state );
    }
    else
    {
        Logger::log( Logger::TRACE, "GPIOManager::requestGPIO: existing gpio");
        // It already exists. If it was exclusive, return false.
        // If we want exclusive, return false.
        // If we have different hilo, return false.
        if ( exclusive ||
             (*itr).second._exclusive ||
             ( pud_state != (*itr).second._pud_state ) ||
             ( pin_mode != (*itr).second._pin_mode ) )
        {
            Logger::log( Logger::ERROR, "GPIOManager::requestGPIO: Can't access it: " + std::to_string( gpio_num ) );
            return false;
        }
        // We are good. Add us to the _devices
        (*itr).second._devices.insert( device );
    }
    Logger::log( Logger::TRACE, "GPIOManager::requestGPIO: all good");
    return true;
}

bool GPIOManager::isOutput( int gpio_num ) const
{
    std::map< int, gpio_info >::const_iterator itr = _requested_gpio.find( gpio_num );
    if ( _requested_gpio.end() == itr )
    {
        return false; // Not registered
    }
    return (*itr).second._pin_mode == __OUTPUT;
}

bool GPIOManager::testGPIO( int gpio_num, GPIOManager::HILO hilo )
{
    GPIOManager::HILO val = getGPIO( gpio_num );
    return (val == getWPiHiLo(hilo));
}

bool GPIOManager::setGPIO_HIGH_timed( int gpio_num, int time_ms )
{
    setGPIO( gpio_num, __HIGH );
    sleep( time_ms);
    setGPIO( gpio_num, __LOW );
    return true;
}
    
void GPIOManager::sleep( int cycles ) const
{
    usleep( 500 * cycles );
}
