// sudo apt-get install libgpiod-dev
// gcc -o gpio_read example.c -lgpiod


#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>

#define CHIP "/dev/gpiochip0"  // Default GPIO chip

void usage(const char *progname) {
    printf("Usage: %s <gpio_pin_number>\n", progname);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    int gpio_pin = atoi(argv[1]);
    if (gpio_pin < 0) {
        fprintf(stderr, "Invalid GPIO pin number\n");
        return 1;
    }

    // Open the GPIO chip
    //struct gpiod_chip *chip = gpiod_chip_open_by_name(CHIP);
    struct gpiod_chip *chip = gpiod_chip_open(CHIP);
    if (!chip) {
        perror("gpiod_chip_open_by_name");
        return 1;
    }

    // Get the line for the specified GPIO pin
    struct gpiod_line *line = gpiod_chip_get_line(chip, gpio_pin);
    if (!line) {
        perror("gpiod_chip_get_line");
        gpiod_chip_close(chip);
        return 1;
    }

    // Request the line as input
    if (gpiod_line_request_input(line, "gpio_read") < 0) {
        perror("gpiod_line_request_input");
        gpiod_chip_close(chip);
        return 1;
    }

    // Read the value from the GPIO pin
    int value = gpiod_line_get_value(line);
    if (value < 0) {
        perror("gpiod_line_get_value");
        gpiod_chip_close(chip);
        return 1;
    }

    // Output the result
    printf("GPIO pin %d value: %d\n", gpio_pin, value);

    // Cleanup and close
    gpiod_chip_close(chip);
    return 0;
}
