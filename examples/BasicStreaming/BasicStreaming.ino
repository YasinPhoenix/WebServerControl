/**
 * @file BasicStreaming.ino
 * @brief Basic example demonstrating file streaming with WebServerControl
 * @version 1.0.0
 * @date 2025-09-20
 * 
 * This example shows how to use WebServerControl to stream files from
 * LittleFS without loading them into memory. Perfect for serving images,
 * videos, or any large files on ESP8266.
 */

#include <WebServerControl.h>
#include <ESP8266WiFi.h>

// WiFi credentials
const char* ssid = "your-wifi-ssid";
const char* password = "your-wifi-password";

// Create server and streaming control
AsyncWebServer server(80);
WebServerControl streamControl(&server);

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("WebServerControl Basic Streaming Example");
    Serial.println("========================================");
    
    // Initialize LittleFS
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS!");
        return;
    }
    Serial.println("✓ LittleFS mounted successfully");
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println();
    Serial.println("✓ WiFi connected successfully");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Setup streaming routes
    setupStreamingRoutes();
    
    // Setup basic web interface
    setupWebInterface();
    
    // Start server
    server.begin();
    Serial.println("✓ Server started");
    
    // Print memory stats
    printMemoryStats();
    
    Serial.println("\nReady! Try these URLs:");
    Serial.printf("http://%s/ - Main page\n", WiFi.localIP().toString().c_str());
    Serial.printf("http://%s/image - Stream a test image\n", WiFi.localIP().toString().c_str());
    Serial.printf("http://%s/data - Stream generated data\n", WiFi.localIP().toString().c_str());
}

void loop() {
    // Print memory stats every 30 seconds
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        printMemoryStats();
        lastCheck = millis();
    }
    
    delay(1000);
}

void setupStreamingRoutes() {
    // Example 1: Stream a file from LittleFS
    // Note: You need to upload files to LittleFS first
    WSCError result = streamControl.streamFile("/image", "/test_image.jpg");
    if (result == WSCError::SUCCESS) {
        Serial.println("✓ File streaming route '/image' registered");
    } else {
        Serial.printf("⚠ File streaming route failed: %s\n", WebServerControl::errorToString(result));
        Serial.println("  Make sure to upload 'test_image.jpg' to LittleFS");
    }
    
    // Example 2: Stream generated content
    result = streamControl.streamCallback("/data", HTTP_GET, 
        generateDataCallback, 
        50000, // 50KB of generated data
        "application/octet-stream",
        4096, // 4KB buffer
        progressCallback // Progress monitoring
    );
    
    if (result == WSCError::SUCCESS) {
        Serial.println("✓ Generated content route '/data' registered");
    } else {
        Serial.printf("✗ Generated content route failed: %s\n", WebServerControl::errorToString(result));
    }
    
    // Example 3: Stream a text file
    result = streamControl.streamFile("/log", "/system.log", HTTP_GET, &LittleFS);
    if (result == WSCError::SUCCESS) {
        Serial.println("✓ Log file streaming route '/log' registered");
    } else {
        Serial.println("⚠ Log file streaming route failed (file may not exist)");
    }
}

void setupWebInterface() {
    // Main page with links to streaming examples
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = R"====(
            <!DOCTYPE html>
            <html>
            <head>
                <title>WebServerControl Demo</title>
                <style>
                    body { font-family: Arial, sans-serif; margin: 40px; }
                    .container { max-width: 800px; margin: 0 auto; }
                    .example { background: #f5f5f5; padding: 20px; margin: 20px 0; border-radius: 5px; }
                    .button { 
                        display: inline-block; 
                        padding: 10px 20px; 
                        background: #007bff; 
                        color: white; 
                        text-decoration: none; 
                        border-radius: 4px; 
                        margin: 5px;
                    }
                    .button:hover { background: #0056b3; }
                    .stats { background: #e9ecef; padding: 15px; border-radius: 5px; }
                </style>
            </head>
            <body>
                <div class="container">
                    <h1>WebServerControl Library Demo</h1>
                    <p>This example demonstrates chunked streaming without memory overflow.</p>

                    <div class="stats">
                        <h3>System Information</h3>
                        <p><strong>Free Heap:</strong> <span id="heap">Loading...</span></p>
                    </div>

                    <div class="example">
                        <h3>File Streaming</h3>
                        <p>Stream files directly from filesystem without loading into memory.</p>
                        <a href="/image" class="button">Download Test Image</a>
                        <a href="/log" class="button">View Log File</a>
                    </div>

                    <div class="example">
                        <h3>Generated Content</h3>
                        <p>Generate and stream content on-demand (50KB of test data).</p>
                        <a href="/data" class="button">Download Generated Data</a>
                    </div>

                    <div class="example">
                        <h3>Memory Management</h3>
                        <p>All streaming operations use chunked transfer to prevent heap overflow.</p>
                        <button onclick="updateStats()" class="button">Refresh Memory Stats</button>
                    </div>
                </div>

                <script>
                    function updateStats() {
                        fetch('/stats')
                            .then(response => response.json())
                            .then(data => {
                                document.getElementById('heap').textContent = 
                                    data.freeHeap + ' bytes (max alloc: ' + data.maxAlloc + ' bytes)';
                            });
                    }

                    // Update stats on page load
                    updateStats();

                    // Auto-refresh every 10 seconds
                    setInterval(updateStats, 10000);
                </script>
            </body>
            </html>
        )====";
        
        request->send(200, "text/html", html);
    });
    
    // Memory stats API endpoint
    server.on("/stats", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint32_t freeHeap, maxAlloc;
        WebServerControl::getMemoryStats(freeHeap, maxAlloc);
        
        String json = "{";
        json += "\"freeHeap\":" + String(freeHeap) + ",";
        json += "\"maxAlloc\":" + String(maxAlloc);
        json += "}";
        
        request->send(200, "application/json", json);
    });
}

// Callback function to generate content chunks
size_t generateDataCallback(uint8_t* buffer, size_t maxSize, size_t offset, void* userData) {
    // Calculate how much data to generate for this chunk
    const size_t totalSize = 50000; // Total size we want to generate
    
    if (offset >= totalSize) {
        return 0; // End of content
    }
    
    size_t remaining = totalSize - offset;
    size_t chunkSize = min(maxSize, remaining);
    
    // Generate some test data - in this case, a pattern based on offset
    for (size_t i = 0; i < chunkSize; i++) {
        // Create a simple pattern: sine wave + offset
        uint8_t value = (uint8_t)(128 + 127 * sin((offset + i) * 0.01));
        buffer[i] = value;
    }
    
    return chunkSize;
}

// Progress callback to monitor streaming progress
void progressCallback(size_t transferred, size_t total, void* userData) {
    float progress = (float)transferred / total * 100.0f;
    
    // Only print every 10% to avoid spam
    static int lastPercent = -1;
    int currentPercent = (int)(progress / 10) * 10;
    
    if (currentPercent != lastPercent && currentPercent % 10 == 0) {
        Serial.printf("Streaming progress: %d%% (%u/%u bytes)\n", 
                     currentPercent, transferred, total);
        lastPercent = currentPercent;
    }
}

void printMemoryStats() {
    uint32_t freeHeap, maxAlloc;
    WebServerControl::getMemoryStats(freeHeap, maxAlloc);
    
    Serial.println("\n--- Memory Statistics ---");
    Serial.printf("Free Heap: %u bytes\n", freeHeap);
    Serial.printf("Max Allocatable Block: %u bytes\n", maxAlloc);
    Serial.println("------------------------\n");
}