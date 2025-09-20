# WebServerControl Library

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![ESP8266](https://img.shields.io/badge/ESP8266-Compatible-green.svg)](https://arduino-esp8266.readthedocs.io/)
[![ESP32](https://img.shields.io/badge/ESP32-Compatible-green.svg)](https://docs.espressif.com/projects/arduino-esp32/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A library that enhances **ESPAsyncWebServer** with efficient chunked streaming capabilities for serving large content without heap overflow. Perfect for ESP8266 and ESP32 projects that need to serve files, images, or generated content larger than available RAM.

## 🚀 Features

- **Memory-Safe Streaming**: Serve content larger than available heap without crashes
- **Multiple Content Sources**: Files, generated content, memory buffers, and custom providers
- **Filesystem Support**: SPIFFS, LittleFS, SD card with automatic detection
- **Production Ready**: Comprehensive error handling, timeout management, and recovery mechanisms
- **ESP8266/ESP32 Compatible**: Optimized for both platforms
- **Easy Integration**: Simple API that enhances existing ESPAsyncWebServer code
- **Buffer Management**: Configurable buffer sizes with automatic validation
- **Progress Monitoring**: Optional callbacks for tracking streaming progress

## 📦 Installation

### Arduino Library Manager
1. Open Arduino IDE
2. Go to **Sketch** → **Include Library** → **Manage Libraries**
3. Search for "WebServerControl"
4. Click **Install**

### Manual Installation
1. Download the latest release
2. Extract to your Arduino libraries folder
3. Restart Arduino IDE

### Dependencies
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
- Platform-specific filesystem libraries (included with ESP8266/ESP32 cores)

## 🔧 Quick Start

```cpp
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebServerControl.h>
#include <LittleFS.h>

AsyncWebServer server(80);
WebServerControl streamControl(&server);

void setup() {
    Serial.begin(115200);
    
    // Initialize filesystem
    LittleFS.begin();
    
    // Initialize WiFi
    WiFi.begin("your-ssid", "your-password");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    
    // Stream a large file without loading it into memory
    streamControl.streamFile("/download", "/large_image.jpg");
    
    // Generate content on-demand
    streamControl.streamCallback("/data", HTTP_GET, 
        [](uint8_t* buffer, size_t maxSize, size_t offset, void* userData) -> size_t {
            // Generate data chunk - example: fill with pattern
            size_t generated = min(maxSize, (size_t)1000 - offset);
            for (size_t i = 0; i < generated; i++) {
                buffer[i] = (offset + i) % 256;
            }
            return generated;
        }, 
        1000, // Total size
        "application/octet-stream"
    );
    
    server.begin();
    Serial.println("Server started");
}

void loop() {
    // Your code here
}
```

## 📖 API Documentation

### WebServerControl Class

#### Constructor
```cpp
WebServerControl(AsyncWebServer* server, 
                size_t defaultBufferSize = 4096,
                unsigned long timeoutMs = 30000);
```

#### Core Methods

##### Stream File from Filesystem
```cpp
WSCError streamFile(const String& uri, 
                   const String& filePath, 
                   WebRequestMethodComposite method = HTTP_GET,
                   fs::FS& fs = LittleFS, 
                   size_t bufferSize = 0, 
                   ProgressCallback progressCallback = nullptr, 
                   void* userData = nullptr);
```

##### Stream Generated Content
```cpp
WSCError streamCallback(const String& uri, 
                       WebRequestMethodComposite method, 
                       ContentCallback callback, 
                       size_t totalSize, 
                       const String& mimeType = "application/octet-stream",
                       size_t bufferSize = 0, 
                       ProgressCallback progressCallback = nullptr, 
                       void* userData = nullptr);
```

##### Stream Custom Provider
```cpp
WSCError streamProvider(const String& uri, 
                       WebRequestMethodComposite method,
                       std::unique_ptr<ContentProvider> provider, 
                       size_t bufferSize = 0,
                       ProgressCallback progressCallback = nullptr, 
                       void* userData = nullptr);
```

### Content Providers

#### File Providers
- `FileContentProvider`: Basic file streaming
- `BufferedFileProvider`: Enhanced with internal buffering
- `SPIFFSProvider`: SPIFFS-optimized provider
- `LittleFSProvider`: LittleFS-optimized provider
- `SDProvider`: SD card with error recovery

#### Memory Providers
- `MemoryContentProvider`: Stream from RAM buffer
- `GeneratorContentProvider`: Generate content on-demand
- `MultiPartContentProvider`: Combine multiple sources

#### Factory
```cpp
auto provider = FilesystemProviderFactory::create("/path/to/file", 
                    FilesystemProviderFactory::FilesystemType::AUTO_DETECT);
```

## 💡 Examples

### 1. Basic File Streaming
```cpp
#include <WebServerControl.h>

WebServerControl streamControl(&server);

void setup() {
    // Stream large images without memory issues
    streamControl.streamFile("/image", "/photos/large_photo.jpg");
    streamControl.streamFile("/video", "/videos/demo.mp4");
}
```

### 2. Generate Large Data Sets
```cpp
// Stream a large CSV file generated on-demand
streamControl.streamCallback("/report.csv", HTTP_GET,
    [](uint8_t* buffer, size_t maxSize, size_t offset, void* userData) -> size_t {
        // Generate CSV data chunk by chunk
        String chunk = generateCSVChunk(offset, maxSize);
        size_t len = min(chunk.length(), maxSize);
        memcpy(buffer, chunk.c_str(), len);
        return len;
    },
    1000000, // 1MB total
    "text/csv"
);
```

### 3. Multi-Source Content
```cpp
auto multiProvider = std::make_unique<MultiPartContentProvider>("text/html");

// Add header from memory
String header = "<html><head><title>Report</title></head><body>";
multiProvider->addPart(std::make_unique<MemoryContentProvider>(
    (uint8_t*)header.c_str(), header.length(), "text/html"));

// Add main content from file
multiProvider->addPart(std::make_unique<LittleFSProvider>("/report_body.html"));

// Add footer from memory
String footer = "</body></html>";
multiProvider->addPart(std::make_unique<MemoryContentProvider>(
    (uint8_t*)footer.c_str(), footer.length(), "text/html"));

streamControl.streamProvider("/report", HTTP_GET, std::move(multiProvider));
```

### 4. Progress Monitoring
```cpp
streamControl.streamFile("/download", "/large_file.bin", HTTP_GET, LittleFS, 0,
    [](size_t transferred, size_t total, void* userData) {
        float progress = (float)transferred / total * 100.0f;
        Serial.printf("Download progress: %.1f%%\n", progress);
    }
);
```

## ⚠️ Important Notes

### Memory Management
- **Never pass large strings directly** - this defeats the purpose of chunked streaming
- Use callbacks or file providers to generate content in small chunks
- Monitor heap usage with `WebServerControl::getMemoryStats()`

### Buffer Sizes
- Default: 4KB (good balance of memory vs performance)
- Minimum: 512 bytes
- Maximum: 8KB
- Larger buffers = better performance but more RAM usage

### Error Handling
```cpp
WSCError result = streamControl.streamFile("/test", "/file.txt");
if (result != WSCError::SUCCESS) {
    Serial.println("Error: " + WebServerControl::errorToString(result));
}
```

## 🔧 Configuration

### Buffer Size Optimization
```cpp
// For ESP8266 with limited RAM
streamControl.setDefaultBufferSize(2048);

// For ESP32 with more RAM
streamControl.setDefaultBufferSize(8192);
```

### Timeout Settings
```cpp
// Set 60-second timeout for large file transfers
streamControl.setTimeout(60000);
```

## 🐛 Troubleshooting

### Common Issues

1. **Heap Overflow**: Reduce buffer size or check for memory leaks
2. **File Not Found**: Verify filesystem is mounted and file exists
3. **Slow Streaming**: Increase buffer size if RAM allows
4. **Connection Drops**: Check timeout settings and network stability

### Debug Information
```cpp
uint32_t freeHeap, maxAlloc;
WebServerControl::getMemoryStats(freeHeap, maxAlloc);
Serial.printf("Free heap: %u, Max alloc: %u\n", freeHeap, maxAlloc);
```

## 📋 Requirements

- **ESP8266** Arduino Core 2.7.0 or later
- **ESP32** Arduino Core 1.0.4 or later
- **ESPAsyncWebServer** library
- **LittleFS** (ESP8266) or **SPIFFS** (ESP32) for file operations

## 🤝 Contributing

Contributions are welcome! Please read our contributing guidelines and submit pull requests for any improvements.

## 📄 License

This library is released under the MIT License. See LICENSE file for details.

## 🙋‍♂️ Support

- Create an issue for bug reports or feature requests
- Check the examples folder for more usage patterns
- Read the source code documentation for advanced usage

## 🔗 Related Projects

- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) - The underlying web server library
- [Arduino ESP8266/ESP32 Cores](https://github.com/esp8266/Arduino) - Platform support

---

**WebServerControl** - Making large content streaming simple and memory-safe for ESP8266/ESP32 projects.