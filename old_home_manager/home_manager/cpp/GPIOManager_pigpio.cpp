#include <pigpio.h>
#include <stdexcept>
#include "GPIOManager_pigpio.h"
#include "Logger.h"

using namespace DeviceUtils;

int getPigpioPinMode(GPIOManager::PIN_MODE pin_mode)
{
    switch (pin_mode)
    {
        case GPIOManager::__INPUT:
            return PI_INPUT;
        case GPIOManager::__OUTPUT:
            return PI_OUTPUT;
        default:
            throw std::invalid_argument("Unsupported PIN_MODE");
    }
}

int getPigpioPUDState(GPIOManager::PUD_STATE pud_state)
{
    switch (pud_state)
    {
        case GPIOManager::__PUD_OFF:
            return PI_PUD_OFF;
        case GPIOManager::__PUD_DOWN:
            return PI_PUD_DOWN;
        case GPIOManager::__PUD_UP:
            return PI_PUD_UP;
        default:
            throw std::invalid_argument("Unsupported PUD_STATE");
    }
}

// Helper functions to map pigpio enums to pigpio equivalents
int getPigpioHiLo(GPIOManager::HILO hilo)
{
    return (hilo == GPIOManager::__HIGH) ? 1 : 0;
}

GPIOManager::HILO getHiLoFromPigpio(int value)
{
    return (value == 1) ? GPIOManager::__HIGH : GPIOManager::__LOW;
}

GPIOManager_Pigpio::GPIOManager_Pigpio(HomeSystem* system)
    : GPIOManager(system)
{
}

GPIOManager_Pigpio::~GPIOManager_Pigpio()
{
    if (_initted)
    {
        gpioTerminate();
    }
}

bool GPIOManager_Pigpio::initGPIO()
{
    if (!_initted)
    {
        try
        {
            Logger::log(Logger::TRACE, "GPIOManager_Pigpio::initGPIO calling gpioInitialise");
            gpioCfgSetInternals(PI_CFG_NOSIGHANDLER);
            if (gpioInitialise() < 0)
            {
                throw std::runtime_error("Failed to initialize pigpio");
            }
        }
        catch (...)
        {
            Logger::log(Logger::TRACE, "GPIOManager_Pigpio::initGPIO failed gpioInitialise");
            return false;
        }
        _initted = true;
    }
    Logger::log(Logger::TRACE, "GPIOManager_Pigpio::initGPIO initted");
    return _initted;
}

GPIOManager_Pigpio::HILO GPIOManager_Pigpio::getGPIO(int gpio_num) const
{
    int val = gpioRead(gpio_num);
    Logger::log(Logger::TRACE, "GPIOManager_Pigpio::getGPIO: gpio pin " + std::to_string(gpio_num) +
                " val: " + std::to_string(val) + " hilo: " + std::to_string(getHiLoFromPigpio(val)));
    return getHiLoFromPigpio(val);
}

bool GPIOManager_Pigpio::setGPIO(int gpio_num, GPIOManager::HILO hilo)
{
    // This is only good if it is an output device
    if (isOutput(gpio_num))
    {
        gpioWrite(gpio_num, getPigpioHiLo(hilo));
        return true;
    }
    return false;
}

bool GPIOManager_Pigpio::setPinMode(int gpio_num, GPIOManager::PIN_MODE pin_mode)
{
    gpioSetMode(gpio_num, getPigpioPinMode(pin_mode));
    return true;
}

bool GPIOManager_Pigpio::setPullUpDnControl(int gpio_num, GPIOManager::PUD_STATE pud_state)
{
    gpioSetPullUpDown(gpio_num, getPigpioPUDState(pud_state));
    return true;
}
