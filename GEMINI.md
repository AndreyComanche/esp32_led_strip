### Project Overview

This project is an example of how to drive a WS2812 addressable LED strip with an ESP32 microcontroller using the RMT peripheral. It's built using the ESP-IDF framework. The main logic in `main/led_strip_example_main.c` initializes the RMT peripheral, sets up an encoder for the LED strip data, and then runs a loop to create a "rainbow chase" effect on the LEDs. The color conversion from HSV to RGB is also included. The `main/led_strip_encoder.c` and `main/led_strip_encoder.h` files define the RMT encoder for the WS2812 LED strip.

### Building and Running

*   **Build and Flash:** `idf.py -p PORT flash monitor`
*   **Dependencies:** Requires the ESP-IDF to be installed and configured.

### Development Environment

The project includes a pre-configured development environment using a Dev Container. The configuration is defined in the `.devcontainer/devcontainer.json` file. This setup provides a consistent environment with all the necessary tools and extensions for development. It uses a Dockerfile to build the development container and configures VS Code with the required settings and extensions.

### VS Code Configuration

The project includes a C/C++ configuration for the ESP-IDF, which is defined in the `.vscode/c_cpp_properties.json` file. This file specifies the compiler path, compile commands, and include paths to ensure that IntelliSense and other features of the C/C++ extension work correctly.

### Manual VS Code Configuration

For developers who prefer to set up their environment manually, the `.vscode/settings.json` file provides the necessary paths for the ESP-IDF, the ESP-IDF tools, and the Python interpreter.

### Debugging

The project includes a launch configuration for debugging the ESP-IDF application in VS Code, which is defined in the `.vscode/launch.json` file. This allows developers to debug the application on the target hardware.

### Development Conventions

*   The code follows the standard ESP-IDF project structure.
*   The code is written in C.
*   The project uses `CMake` for building.
*   Logging is done using `esp_log.h`.
*   Error handling is done using `ESP_ERROR_CHECK`.

### Testing

The project uses the `pytest-embedded` framework to run tests on the target hardware. The test cases are located in the `pytest_led_strip.py` file. The tests work by flashing the application to the target device and then verifying the console output to ensure that the application is behaving as expected.
