#ifndef GPIO_MANAGER_LIBGPIO_H
#define GPIO_MANAGER_LIBGPIO_H
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
#include <gpiod.h>
#include "GPIOManager.h"

class Device;
class HomeSystem;

class GPIOManager_Libgpiod : public GPIOManager
{
    public:
        GPIOManager_Libgpiod(HomeSystem* system);
        virtual ~GPIOManager_Libgpiod();

        virtual bool initGPIO();

        virtual GPIOManager::HILO getGPIO(int gpio_num) const;
        virtual bool setGPIO(int gpio_num, GPIOManager::HILO hilo);

    protected:
        virtual bool setPinMode(int gpio_num, GPIOManager::PIN_MODE pin_mode); // 0: input, 1: output
        virtual bool setPullUpDnControl(int gpio_num, GPIOManager::PUD_STATE pud_state); // 0: off, 1: pull-up, 2: pull-down

    private:
        gpiod_chip* _chip; // Chip object for accessing GPIOs
        int getGpioLineValue(int gpio_num) const;
        void setGpioLineValue(int gpio_num, int value);
        gpiod_line* getLine(int gpio_num) const;
};
#endif
