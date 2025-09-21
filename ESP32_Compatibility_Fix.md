# ESP_Async_WebServer Compatibility Fix

## Issue Description

When compiling examples for ESP32 with newer Arduino cores, you may encounter this error:

```
error: passing 'const AsyncServer' as 'this' argument discards qualifiers [-fpermissive]
return static_cast<tcp_state>(_server.status());
```

This is a const-correctness issue in the ESP_Async_WebServer library itself, not in your code.

## Solutions

### Solution 1: Use Pragma Directives (Recommended)

Add these lines before including ESPAsyncWebServer.h:

```cpp
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-fpermissive"

#include <ESPAsyncWebServer.h>

#pragma GCC diagnostic pop
```

This solution is implemented in:
- `BasicStreaming.ino` (original file, modified)
- `BasicStreaming_Fixed.ino` (dedicated fixed version)

### Solution 2: Compiler Flags in Arduino IDE

Add this line to your platform.txt or use Arduino IDE's Additional Board Manager URLs:

```
compiler.cpp.extra_flags=-fpermissive
```

### Solution 3: Use Alternative Library

Install the ESPAsyncWebServer-esphome fork which has better ESP32 compatibility:

```
arduino-cli lib install --git-url https://github.com/esphome/ESPAsyncWebServer.git
```

### Solution 4: Library Manager Alternative

In Arduino IDE Library Manager, search for and install:
- "AsyncTCP" by Hristo Gochkov
- "ESPAsyncWebServer-esphome" by ESPHome Team

## Files in This Project

- `BasicStreaming.ino` - Original example with compatibility fix applied
- `BasicStreaming_Fixed.ino` - Dedicated fixed version with extra documentation
- `ESP32_Compatibility_Fix.md` - This documentation file

## Testing

Both files should now compile successfully on ESP32 with Arduino Core 3.x.x series.

If you still encounter issues, please:

1. Check your Arduino ESP32 core version
2. Ensure you have the correct library versions installed
3. Try the alternative library suggestions above

## Technical Details

The error occurs because the `state()` method in `ESPAsyncWebServer.h` is declared as `const` but calls `_server.status()` which is not a const method. This violates const-correctness rules in modern C++ compilers.

The pragma directive solution tells the compiler to be permissive about this specific violation while keeping strict checking for the rest of your code.