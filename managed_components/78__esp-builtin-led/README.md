# ESP32 Builtin RGB LED Component

This component provides a library for controlling the ESP32's onboard RGB LED.

## Features

- Control the LED color and brightness
- Blink the LED at a specified frequency
- Set the LED to a specific color

## Usage

To use this component, add the following line to your component's CMakeLists.txt file:

```bash
idf.py add-dependency "78/esp-builtin-led"
```

Code example:

```cpp
#include "BuiltinLed.h"

BuiltinLed& builtin_led = BuiltinLed::GetInstance();
builtin_led.SetColor(255, 0, 0);
builtin_led.Blink(BLINK_INFINITE, 500);
```

