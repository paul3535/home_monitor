// g++ -o read_gpio read_gpio.cpp -lwiringPi

#include <wiringPi.h>
#include <iostream>
#include <cstdlib> // for atoi

int main(int argc, char *argv[]) {
    // Check if pin number is provided
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <pin_number>" << std::endl;
        return 1;
    }

    // Convert the command line argument to an integer
    int pin = atoi(argv[1]);

    // Initialize wiringPi using the wiringPi pin numbering
    if (wiringPiSetup() == -1) {
        std::cerr << "Failed to initialize wiringPi" << std::endl;
        return 1;
    }

    // Set the specified pin as input
    pinMode(pin, INPUT);

    // Read the value of the pin
    int value = digitalRead(pin);

    // Print the value
    std::cout << "Pin " << pin << " value: " << (value == HIGH ? "HIGH" : "LOW") << " INPUT " << INPUT << std::endl;

    return 0;
}
