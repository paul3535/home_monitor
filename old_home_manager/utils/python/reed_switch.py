import RPi.GPIO as GPIO
import time

# Set up GPIO mode
GPIO.setmode(GPIO.BCM)

# Define the GPIO pins for each switch
SWITCH1_PIN = 17
SWITCH2_PIN = 27

# Set up pins as input with internal pull-up resistors
GPIO.setup(SWITCH1_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.setup(SWITCH2_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)

print("Monitoring switches... Press Ctrl+C to exit.")

try:
    while True:
        # Check the state of Switch 1
        if GPIO.input(SWITCH1_PIN) == GPIO.LOW:
            print("Switch 1 (GPIO 17) is closed")
        else:
            print("Switch 1 (GPIO 17) is open")
        
        # Check the state of Switch 2
        if GPIO.input(SWITCH2_PIN) == GPIO.LOW:
            print("Switch 2 (GPIO 27) is closed")
        else:
            print("Switch 2 (GPIO 27) is open")

        # Delay to avoid spamming the console
        time.sleep(0.5)

except KeyboardInterrupt:
    print("Exiting program")

finally:
    GPIO.cleanup()
