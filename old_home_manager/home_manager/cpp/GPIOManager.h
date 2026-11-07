#ifndef GPIO_MANAGER_H
#define GPIO_MANAGER_H
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

#include "PiUtils.h"
#include "DeviceUtils.h"

class Device;
class HomeSystem;

class GPIOManager
{
    public:

		enum GPIO_MANAGER_TYPE
		{
			GPIO_PIGPIO = 0,
			GPIO_LIBGPIOD = 1,
			GPIO_DISABLED = 2
		};

		static const char * getGPIOTypeAsString( GPIO_MANAGER_TYPE gpio_type )
		{
			switch( gpio_type )
			{
				case GPIO_PIGPIO:
					return "gpio_pigpio";
				case GPIO_LIBGPIOD:
					return "gpio_libgpiod";
				case GPIO_DISABLED:
					break;
			};
			return "gpio_disabled";
		}

		static GPIO_MANAGER_TYPE getGPIOTypeFromString( std::string str )
		{
			for( int i = 0; i <= GPIO_DISABLED; ++i )
			{
				if ( strcmp( str.c_str(), getGPIOTypeAsString( (GPIO_MANAGER_TYPE) i ) ) == 0 )
				{
					return (GPIO_MANAGER_TYPE) i;
				}
			}
			return GPIO_DISABLED;
		}

		// This is the Pull Up/Pull Down state
        enum PUD_STATE
        {
            __PUD_DOWN = 1,
            __PUD_UP = 2,
            __PUD_OFF = 0 // default
        };

		static const char * getPUDStateAsString( PUD_STATE state )
		{
			switch( state )
			{
				case __PUD_DOWN:
					return "pud_down";
				case __PUD_UP:
					return "pud_up";
				case __PUD_OFF:
					return "pud_off";
			};
			return "Invalid PUD State";
		}

		static PUD_STATE getPUDStateFromString( std::string str )
		{
			for( int i = 0; i <= __PUD_OFF; ++i )
			{
				if ( strcmp( str.c_str(), getPUDStateAsString( (PUD_STATE) i ) ) == 0 )
				{
					return (PUD_STATE) i;
				}
			}
			return __PUD_OFF;
		}

        //#define	LOW			 0
        //#define	HIGH			 1
        enum HILO
        {
            __LOW = 0,
            __HIGH = 1
        };

		static int getWPiHiLo( HILO mode )
		{
			switch( mode )
			{
				case __LOW:
					return 0; // LOW
				case __HIGH:
					return 1; // HIGH
			};
			return 0;
		}

        enum PIN_MODE
        {
            __INPUT = 0, // Default
            __OUTPUT = 1,
            __PWM_OUTPUT = 2
        };

		static const char * getPinModeAsString( PIN_MODE mode )
		{
			switch( mode )
			{
				case __INPUT:
					return "input";
				case __OUTPUT:
					return "output";
				case __PWM_OUTPUT:
					return "pwm_output";
			};
			return "Invalid Pin Mode";
		}

		static PIN_MODE getPinModeFromString( std::string str )
		{
			for( int i = 0; i <= __PWM_OUTPUT; ++i )
			{
				if ( strcmp( str.c_str(), getPinModeAsString( (PIN_MODE) i ) ) == 0 )
				{
					return (PIN_MODE) i;
				}
			}
			return __INPUT;
		}

		static GPIOManager* createPiGPIOGPIO( HomeSystem* system );
		static GPIOManager* createLibGPIOdGPIO( HomeSystem* system );

        GPIOManager( HomeSystem* system );
        virtual ~GPIOManager( void );

        virtual bool initGPIO() = 0;

        // Request to use that GPIO device.
        // If exclusive, no one else can use it.
        // If already non-exclusive and exclusive comes in,
        // or if hilo is different
        // return false
        bool requestGPIO( int gpio_num,
                          Device* device,
                          PUD_STATE pud_state,
                          PIN_MODE pin_mode,
                          bool exclusive
                          );

        // Release Device's hold on the GPIO pin (probably not used all that often)
        bool releaseGPIO( int gpio_num,
                          Device* device
                          );

        // Devices should only interact with GPIOs that it has requested, but this is not enforced
        
        // This will return whether the state of the GPIO matches the HILO state passed in.
        bool testGPIO( int gpio_num, HILO hilo );

        bool setGPIO_HIGH_timed( int gpio_num, int time_ms );

		bool isOutput( int gpio_num ) const;

        void sleep( int cycles ) const;

        virtual HILO getGPIO( int gpio_num ) const = 0;

        virtual bool setGPIO( int gpio_num, HILO hilo ) = 0;

	protected:
		virtual bool setPinMode( int gpio_num, PIN_MODE pin_mode ) = 0;
        virtual bool setPullUpDnControl( int gpio_num, PUD_STATE pud_state ) = 0;

    protected:
        struct gpio_info
        {
            int _gpio_num;
            PUD_STATE _pud_state;
            PIN_MODE _pin_mode;
            std::set< Device* > _devices;
            bool _exclusive;
        };

        HomeSystem* _system;
        bool _initted;
        std::map< int, gpio_info > _requested_gpio;
};

#endif
