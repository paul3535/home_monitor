#ifndef DEVICE_H
#define DEVICE_H
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
#include <gpiod.h>
#include <thread>
#include <atomic>
#include <utility>
#include <ctime>
#include "nlohmann/json.hpp"
using json = nlohmann::json;
#define USE_PYTHON_FOR_GPIO
//#define USE_GPIOCLASS_FOR_GPIO
//#define USE_PSEUDO_RASPBERRY_PI

#ifdef USE_GPIOCLASS_FOR_GPIO
class GPIOClass;
#endif

#include "DeviceUtils.h"
#include "Evaluation.h"
#include "GPIOManager.h"
#include "PiUtils.h"


class HomeSystem;
class HomeConfig;
class Device;


class Device
{
    public:
        Device( const std::string& name, const std::string& uuid, HomeSystem* system )
            : _name( name )
            , _system( system )
            , _uuid( uuid )
        {
        }
        virtual ~Device()
        {
        }

        virtual std::string getName() const { return _name; }
        virtual std::string getUUID() const { return _uuid; }
        virtual json getDescriptionJSON() const;

        // Device stuff
        virtual bool initialize( );
        virtual bool processCommand( json input, json& result ) = 0;
        virtual bool evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation ) = 0;
        virtual DeviceUtils::DEVICE_TYPE getType() const = 0;

        HomeSystem* getSystem() const { return _system; }

    protected:
        std::string _name;
        HomeSystem* _system;
        std::string _uuid;
};

class Device_TemperatureSensor_DS18B20 : public Device
{
    public:
        Device_TemperatureSensor_DS18B20( const std::string& name,
                                          const std::string& id,
                                          const std::string& results_temp_c_string,
                                          const std::string& uuid,
                                          HomeSystem* system );

        virtual ~Device_TemperatureSensor_DS18B20( void )
        {
        }

        virtual json getDescriptionJSON() const;

        static bool initializeDS18B20( HomeSystem* system, json j );
        static Device_TemperatureSensor_DS18B20* getDevice( HomeSystem* system, json jf );
        virtual bool processCommand( json input, json& result ) { return false; }
        virtual bool evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation );
        virtual DeviceUtils::DEVICE_TYPE getType() const { return DeviceUtils::DS18B20; }

    private:
        float getTemperatureC( ) const;
        float getTemperatureF( ) const;
        std::string getResultsTempCString() const { return _results_temp_c_string; }
    private:
        std::string _id;
        int _gpio_pin;
        std::string _results_temp_c_string;
};

class Device_GPIOBase : public Device
{
    public:
        Device_GPIOBase( const std::string& name,
                         unsigned int gpio_num,
                         const std::string& uuid,
                         HomeSystem* system );

        virtual ~Device_GPIOBase( void )
        {
        }

        virtual bool initialize( );
        static Device_GPIOBase* getDevice( HomeSystem* system, json jf );
        virtual bool processCommand( json input, json& result );
        virtual bool evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation );
        virtual DeviceUtils::DEVICE_TYPE getType() const { return DeviceUtils::RELAY; }
        void setPUDState( GPIOManager::PUD_STATE state ) { _pud_state = state; }
        void setPinMode( GPIOManager::PIN_MODE mode ) { _pin_mode = mode; }
        void setExclusive( bool exclusive ) { _exclusive = exclusive; }

        // Two options: ON or OFF
        virtual DeviceUtils::DEVICE_STATE getState( ) const;

    protected:
        unsigned int _gpio_num;
        bool _initted;
        GPIOManager* _gpio_manager;
        GPIOManager::PUD_STATE _pud_state;
        GPIOManager::PIN_MODE _pin_mode;
        GPIOManager::HILO _on_hilo_val;
        bool _exclusive;
};

class Device_GPIOSwitch : public Device_GPIOBase
{
    public:
        Device_GPIOSwitch( const std::string& name, unsigned int gpio_num, const std::string& uuid, HomeSystem* system );

        virtual ~Device_GPIOSwitch( void );

        virtual bool initialize( );
        static Device_GPIOSwitch* getDevice( HomeSystem* system, json jf );
        virtual DeviceUtils::DEVICE_TYPE getType() const { return DeviceUtils::GPIO_SWITCH; }

        virtual DeviceUtils::DEVICE_STATE getState( ) const;

    private:
        mutable DeviceUtils::DEVICE_STATE _prev_state;
        mutable unsigned int _cycles;

    // Interrupt stuff
    private:
        void monitorInterrupts();
        void switchTriggered();
        void startMonitoring();
        void stopMonitoring();

    private:
        PiUtils::TriState _use_interrupts;
        mutable std::mutex _interrupt_mutex; // This will protect the _interrupt_states vector
        mutable std::vector< std::pair< time_t, DeviceUtils::DEVICE_STATE> > _interrupt_states;
        std::string _chip_path;
        std::function<void()> _interrupt_callback; // unused
        std::atomic<bool> _running;
        std::thread _monitor_thread;

        struct gpiod_chip *_chip;
        struct gpiod_line *_line;
};

class Device_Relay : public Device
{
    public:
        Device_Relay( const std::string& name, unsigned int gpio_num, const std::string& uuid, HomeSystem* system );

        virtual ~Device_Relay( void )
        {
        }

        virtual bool initialize( );
        static Device_Relay* getDevice( HomeSystem* system, json jf );
        virtual bool processCommand( json input, json& result );
        virtual bool evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation );
        virtual DeviceUtils::DEVICE_TYPE getType() const { return DeviceUtils::RELAY; }
        void setOnOffReversed( bool reverse );
        void setPUDState( GPIOManager::PUD_STATE state ) { _pud_state = state; }
        void setPinMode( GPIOManager::PIN_MODE mode ) { _pin_mode = mode; }
        void setExclusive( bool exclusive ) { _exclusive = exclusive; }
        void setInitialState( DeviceUtils::DEVICE_STATE state ) { _initial_state = state; }

        // Two options: ON or OFF
        DeviceUtils::DEVICE_STATE getState( ) const;
        bool setState( DeviceUtils::DEVICE_STATE state );

    private:
        unsigned int _gpio_num;
        bool _initted;
        GPIOManager* _gpio_manager;
        unsigned int _cycles;
        GPIOManager::HILO _on_state_val;
        GPIOManager::HILO _off_state_val;
        GPIOManager::PUD_STATE _pud_state;
        GPIOManager::PIN_MODE _pin_mode;
        bool _exclusive;
        DeviceUtils::DEVICE_STATE _initial_state;
};

class Device_VSPump : public Device
{
    public:
        Device_VSPump( const std::string& name, const std::string& uuid, HomeSystem* system );

        virtual ~Device_VSPump( void )
        {
        }

        virtual bool initialize( );
        static Device_VSPump* getDevice( HomeSystem* system, json jf );
        virtual bool processCommand( json input, json& result );
        virtual bool evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation );
        virtual DeviceUtils::DEVICE_TYPE getType() const { return DeviceUtils::VS_PUMP; }

        void setOnOffReversed( bool reverse );
        void setPUDState( GPIOManager::PUD_STATE state ) { _pud_state = state; }
        void setPinMode( GPIOManager::PIN_MODE mode ) { _pin_mode = mode; }
        void setExclusive( bool exclusive ) { _exclusive = exclusive; }
        void setInitialState( DeviceUtils::DEVICE_STATE state ) { _initial_state = state; }
        bool setRelayInput( unsigned int, json relay_json );
        bool setRelayPumpEnable( json relay );

        DeviceUtils::DEVICE_STATE getState( ) const;
        bool setState( DeviceUtils::DEVICE_STATE state );

    private:
        unsigned int _gpio_num;
        bool _initted;
        GPIOManager* _gpio_manager;
        unsigned int _cycles;
        GPIOManager::HILO _on_state_val;
        GPIOManager::HILO _off_state_val;
        GPIOManager::PUD_STATE _pud_state;
        GPIOManager::PIN_MODE _pin_mode;
        bool _exclusive;
        DeviceUtils::DEVICE_STATE _initial_state;
        Device_Relay* _relay_inp1;
        Device_Relay* _relay_inp2;
        Device_Relay* _relay_inp3;
        Device_Relay* _relay_pump_enable;
};

class Device_GarageDoor : public Device
{
    public:
        Device_GarageDoor( const std::string& name, const std::string& uuid, HomeSystem* system, Device_GPIOSwitch* up, Device_GPIOSwitch* down );

        virtual ~Device_GarageDoor( void )
        {
        }

        virtual bool initialize( );
        static Device_GarageDoor* getDevice( HomeSystem* system, json jf );
        virtual bool processCommand( json input, json& result );
        virtual bool evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation );
        virtual DeviceUtils::DEVICE_TYPE getType() const { return DeviceUtils::GARAGE_DOOR; }

        DeviceUtils::DEVICE_STATE getState( ) const;

    private:
        bool _initted;
        GPIOManager* _gpio_manager;
        unsigned int _cycles;
        Device_GPIOSwitch* _up;
        Device_GPIOSwitch* _down;
        DeviceUtils::DEVICE_STATE _last_evaluation_state;
};

class Device_WashingMachine : public Device
{
    public:
        Device_WashingMachine( const std::string& name, const std::string& uuid, HomeSystem* system, Device_GPIOSwitch* lid_up );

        virtual ~Device_WashingMachine( void )
        {
        }

        virtual bool initialize( );
        static Device_WashingMachine* getDevice( HomeSystem* system, json jf );
        virtual bool processCommand( json input, json& result );
        virtual bool evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation );
        virtual DeviceUtils::DEVICE_TYPE getType() const { return DeviceUtils::GARAGE_DOOR; }

        DeviceUtils::DEVICE_STATE getState( ) const;

    private:
        bool _initted;
        GPIOManager* _gpio_manager;
        unsigned int _cycles;
        Device_GPIOSwitch* _lid_up;
        DeviceUtils::DEVICE_STATE _last_evaluation_state;
};

class Device_EmailCheck : public Device
{
    public:
        Device_EmailCheck( const std::string& name, unsigned int max_frequency_seconds, const std::string& uuid, HomeSystem* system );

        virtual ~Device_EmailCheck( void )
        {
        }

        virtual bool initialize( );
        static Device_EmailCheck* getDevice( HomeSystem* system, json jf );
        virtual bool processCommand( json input, json& result );
        virtual bool evaluate( time_t evaluation_time, Evaluation::device_evaluation& evaluation );
        virtual DeviceUtils::DEVICE_TYPE getType() const { return DeviceUtils::EMAIL_CHECK; }

    private:
        bool _initted;
        std::string _email_label;
        unsigned int _max_frequency_seconds;
        time_t _last_check_time;
        time_t _next_earliest_check_time;
};


class DeviceStore
{
    public:
        DeviceStore( HomeSystem* system );
        ~DeviceStore();

        bool loadDevices( const HomeConfig* config, bool clear );
        void getDevices( std::vector< Device* >& devices ) const;

        Device* getDevice( const std::string name ) const;

        std::string getDevicesAsString( const PiUtils::TEXT_FORMAT format ) const;
        void clearDevices();

    private:
        Device* getDeviceFromJSONString( const std::string& str, bool& skipped );
        bool initDevices( );

    private:
        std::vector<Device*> _devices;
        HomeSystem* _system;

};
#endif
