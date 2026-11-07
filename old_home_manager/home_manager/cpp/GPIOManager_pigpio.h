#ifndef GPIO_MANAGER_PIGPIO_H
#define GPIO_MANAGER_PIGPIO_H
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

#include "GPIOManager.h"

class Device;
class HomeSystem;

class GPIOManager_Pigpio : public GPIOManager
{
    public:
        GPIOManager_Pigpio( HomeSystem* system );
        virtual ~GPIOManager_Pigpio( void );

        virtual bool initGPIO();
       
        virtual GPIOManager::HILO getGPIO( int gpio_num ) const;

        virtual bool setGPIO( int gpio_num, GPIOManager::HILO hilo );

	protected:

		bool setPinMode( int gpio_num, GPIOManager::PIN_MODE pin_mode );
		bool setPullUpDnControl( int gpio_num, GPIOManager::PUD_STATE pud_state );
};

#endif
