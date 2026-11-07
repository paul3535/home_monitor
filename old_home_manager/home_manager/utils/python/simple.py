import sys
import os

if __name__ == "__main__":
    # Print Python version and executable path
    print("Python version:", sys.version)
    print("Python executable:", sys.executable)

    # Print virtual environment (if any)
    print("VIRTUAL_ENV:", os.getenv("VIRTUAL_ENV"))

    # Print system path
    print("sys.path:", sys.path)

    # Print any other relevant environment variables
    print("PATH:", os.getenv("PATH"))
