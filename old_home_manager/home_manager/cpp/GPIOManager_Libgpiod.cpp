#include <gpiod.h>
#include <stdexcept>
#include "GPIOManager_Libgpiod.h"
#include "Logger.h"

using namespace DeviceUtils;

int getLibgpiodPinMode(GPIOManager::PIN_MODE pin_mode)
{
    switch (pin_mode)
    {
        case GPIOManager::__INPUT:
            return 0;
        case GPIOManager::__OUTPUT:
            return 1;
        default:
            throw std::invalid_argument("Unsupported PIN_MODE");
    }
}

int getLibgpiodPUDState(GPIOManager::PUD_STATE pud_state)
{
    switch (pud_state)
    {
        case GPIOManager::__PUD_OFF:
            return GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE;
        case GPIOManager::__PUD_DOWN:
            return GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;
        case GPIOManager::__PUD_UP:
            return GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
        default:
            throw std::invalid_argument("Unsupported PUD_STATE");
    }
}

// Constructor
GPIOManager_Libgpiod::GPIOManager_Libgpiod(HomeSystem* system) 
    : GPIOManager( system )
    , _chip( nullptr )
{
    // constructor implementation
}

// Destructor
GPIOManager_Libgpiod::~GPIOManager_Libgpiod() {
    if (_chip) {
        gpiod_chip_close(_chip);
    }
}

// Initialize GPIO
bool GPIOManager_Libgpiod::initGPIO() {
    if (!_initted) {
        try {
            _chip = gpiod_chip_open("/dev/gpiochip0"); // Open the GPIO chip (usually gpiochip0)
            if (!_chip) {
                throw std::runtime_error("Failed to open GPIO chip.");
            }
            std::cout << "GPIO initialized with libgpiod" << std::endl;
        }
        catch (...) {
            return false;
        }
        _initted = true;
    }
    return _initted;
}

// Get GPIO value (HIGH or LOW)
GPIOManager::HILO GPIOManager_Libgpiod::getGPIO(int gpio_num) const {
    int val = getGpioLineValue(gpio_num);
    Logger::log( Logger::TRACE, "GPIOManager_Libgpiod::getGPIO: gpio pin " + std::to_string( gpio_num ) + " val: " + std::to_string( val ) + " hilo: " + std::to_string( val));
    return ((val == 1) ? GPIOManager::__HIGH : GPIOManager::__LOW);
}

// Set GPIO value (HIGH or LOW)
bool GPIOManager_Libgpiod::setGPIO(int gpio_num, GPIOManager::HILO hilo) {
    if (setPinMode(gpio_num, GPIOManager::__OUTPUT)) { // Assuming it's an output mode
        setGpioLineValue(gpio_num, hilo == GPIOManager::__HIGH ? 1 : 0);
        return true;
    }
    return false;
}

// Set Pin Mode (input or output)
bool GPIOManager_Libgpiod::setPinMode(int gpio_num, GPIOManager::PIN_MODE pin_mode) {
    gpiod_line* line = getLine(gpio_num);
    if (pin_mode == GPIOManager::__INPUT) {
        gpiod_line_request_input(line, "GPIO_input");
    } else {
        gpiod_line_request_output(line, "GPIO_output", 0);
    }
    return true;
}

// Set Pull-up/Pull-down resistor configuration
bool GPIOManager_Libgpiod::setPullUpDnControl(int gpio_num, GPIOManager::PUD_STATE pud_state) {
    gpiod_line* line = getLine(gpio_num);
    gpiod_line_set_flags(line, getLibgpiodPUDState(pud_state));
    return true;
}

// Helper function to get GPIO line object
gpiod_line* GPIOManager_Libgpiod::getLine(int gpio_num) const {
    gpiod_line* line = gpiod_chip_get_line(_chip, gpio_num);
    if (!line) {
        throw std::runtime_error("Failed to get GPIO line");
    }
    return line;
}

// Helper function to get GPIO value
int GPIOManager_Libgpiod::getGpioLineValue(int gpio_num) const {
    gpiod_line* line = getLine(gpio_num);
    int value = gpiod_line_get_value(line);
    return value;
}

// Helper function to set GPIO value
void GPIOManager_Libgpiod::setGpioLineValue(int gpio_num, int value) {
    gpiod_line* line = getLine(gpio_num);
    gpiod_line_set_value(line, value);
}
