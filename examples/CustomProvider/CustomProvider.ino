/**
 * @file CustomProvider.ino
 * @brief Advanced example showing custom content providers and complex streaming scenarios
 * @version 1.0.0
 * @date 2025-09-20
 * 
 * This example demonstrates:
 * - Custom content providers
 * - Multi-part content streaming
 * - Data compression (placeholder)
 * - Real-time data generation
 * - Memory buffer streaming
 * - Progress monitoring and error handling
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WebServerControl.h>
#include <ContentProviders.h>
#include <FilesystemProviders.h>
#include <LittleFS.h>

// WiFi credentials
const char* ssid = "your-wifi-ssid";
const char* password = "your-wifi-password";

// Server and streaming control
AsyncWebServer server(80);
WebServerControl streamControl(&server);

// Custom sensor data provider
class SensorDataProvider : public ContentProvider {
private:
    size_t _totalSamples;
    size_t _samplesPerChunk;
    String _mimeType;
    bool _isReady;
    
public:
    SensorDataProvider(size_t totalSamples, size_t samplesPerChunk = 100)
        : _totalSamples(totalSamples), _samplesPerChunk(samplesPerChunk),
          _mimeType("application/json"), _isReady(true) {}
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !buffer) return 0;
        
        // Calculate which sample we're at
        size_t currentSample = offset / getSampleSize();
        
        if (currentSample >= _totalSamples) {
            return 0; // End of data
        }
        
        // Generate JSON chunk with sensor readings
        String chunk = "";
        size_t samplesInChunk = min(_samplesPerChunk, _totalSamples - currentSample);
        
        if (currentSample == 0) {
            chunk += "{\"sensors\":[";
        }
        
        for (size_t i = 0; i < samplesInChunk; i++) {
            if (currentSample + i > 0) chunk += ",";
            
            // Simulate sensor readings
            float temperature = 20.0 + 10.0 * sin((currentSample + i) * 0.1);
            float humidity = 50.0 + 20.0 * cos((currentSample + i) * 0.15);
            unsigned long timestamp = millis() + (currentSample + i) * 1000;
            
            chunk += "{";
            chunk += "\"id\":" + String(currentSample + i) + ",";
            chunk += "\"timestamp\":" + String(timestamp) + ",";
            chunk += "\"temperature\":" + String(temperature, 2) + ",";
            chunk += "\"humidity\":" + String(humidity, 2);
            chunk += "}";
        }
        
        if (currentSample + samplesInChunk >= _totalSamples) {
            chunk += "]}"; // Close JSON
        }
        
        size_t chunkLen = min(chunk.length(), maxSize);
        memcpy(buffer, chunk.c_str(), chunkLen);
        return chunkLen;
    }
    
    size_t getTotalSize() const override {
        // Estimate total size (actual size may vary)
        return _totalSamples * getSampleSize() + 50; // +50 for JSON overhead
    }
    
    String getMimeType() const override { return _mimeType; }
    void reset() override { /* Stateless generator */ }
    bool isReady() const override { return _isReady; }
    
private:
    size_t getSampleSize() const {
        return 80; // Estimated bytes per JSON sample
    }
};

// Live data stream provider
class LiveDataProvider : public ContentProvider {
private:
    size_t _duration;
    size_t _interval;
    String _mimeType;
    bool _isReady;
    unsigned long _startTime;
    
public:
    LiveDataProvider(size_t durationSeconds, size_t intervalMs = 1000)
        : _duration(durationSeconds), _interval(intervalMs),
          _mimeType("text/plain"), _isReady(true), _startTime(millis()) {}
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !buffer) return 0;
        
        unsigned long elapsed = millis() - _startTime;
        if (elapsed > _duration * 1000) {
            return 0; // Duration exceeded
        }
        
        // Generate timestamp and sensor data line
        String line = String(millis()) + ",";
        line += String(analogRead(A0)) + ","; // Simulate analog reading
        line += String(ESP.getFreeHeap()) + "\n";
        
        size_t lineLen = min(line.length(), maxSize);
        memcpy(buffer, line.c_str(), lineLen);
        
        // Wait for next interval
        delay(_interval);
        
        return lineLen;
    }
    
    size_t getTotalSize() const override {
        // Estimate based on duration and interval
        size_t totalSamples = (_duration * 1000) / _interval;
        return totalSamples * 30; // ~30 bytes per line
    }
    
    String getMimeType() const override { return _mimeType; }
    void reset() override { _startTime = millis(); }
    bool isReady() const override { return _isReady; }
};

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("WebServerControl Custom Provider Example");
    Serial.println("=======================================");
    
    // Initialize LittleFS for multi-part content
    if (!LittleFS.begin()) {
        Serial.println("Warning: LittleFS not available for multi-part examples");
    }
    
    // Connect to WiFi
    connectToWiFi();
    
    // Setup custom streaming routes
    setupCustomProviders();
    setupMultiPartContent();
    setupLiveStreaming();
    setupWebInterface();
    
    // Start server
    server.begin();
    Serial.println("✓ Server started");
    
    printAvailableEndpoints();
}

void loop() {
    // Monitor memory usage
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        printMemoryStats();
        lastCheck = millis();
    }
    
    delay(1000);
}

void connectToWiFi() {
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println();
    Serial.println("✓ WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void setupCustomProviders() {
    // Sensor data streaming
    server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
        int samples = 1000; // Default
        if (request->hasParam("samples")) {
            samples = request->getParam("samples")->value().toInt();
            samples = constrain(samples, 10, 10000); // Limit range
        }
        
        auto provider = std::make_unique<SensorDataProvider>(samples);
        streamCustomProvider(request, std::move(provider), "sensor_data.json");
    });
    
    // Memory buffer streaming example
    server.on("/api/buffer", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Create a test buffer (be careful with size!)
        size_t bufferSize = 8192; // 8KB test buffer
        uint8_t* testData = new uint8_t[bufferSize];
        
        // Fill with test pattern
        for (size_t i = 0; i < bufferSize; i++) {
            testData[i] = i % 256;
        }
        
        auto provider = std::make_unique<MemoryContentProvider>(
            testData, bufferSize, "application/octet-stream", true); // Take ownership
        
        streamCustomProvider(request, std::move(provider), "test_buffer.bin");
    });
    
    // Generated content with custom pattern
    server.on("/api/pattern", HTTP_GET, [](AsyncWebServerRequest *request) {
        size_t size = 32768; // 32KB
        String pattern = "wave"; // Default pattern
        
        if (request->hasParam("size")) {
            size = request->getParam("size")->value().toInt();
            size = constrain(size, 1024, 1048576); // 1KB to 1MB
        }
        
        if (request->hasParam("pattern")) {
            pattern = request->getParam("pattern")->value();
        }
        
        auto generator = [pattern](uint8_t* buffer, size_t maxSize, size_t offset) -> size_t {
            for (size_t i = 0; i < maxSize; i++) {
                uint8_t value = 0;
                double pos = (double)(offset + i);
                
                if (pattern == "wave") {
                    value = (uint8_t)(128 + 127 * sin(pos * 0.01));
                } else if (pattern == "square") {
                    value = ((int)(pos / 100) % 2) ? 255 : 0;
                } else if (pattern == "sawtooth") {
                    value = (uint8_t)((int)pos % 256);
                } else { // random
                    value = random(256);
                }
                
                buffer[i] = value;
            }
            return maxSize;
        };
        
        auto provider = std::make_unique<GeneratorContentProvider>(
            generator, size, "application/octet-stream");
        
        streamCustomProvider(request, std::move(provider), "pattern_" + pattern + ".bin");
    });
    
    Serial.println("✓ Custom provider routes registered");
}

void setupMultiPartContent() {
    // Multi-part HTML document
    server.on("/multipart", HTTP_GET, [](AsyncWebServerRequest *request) {
        auto multiProvider = std::make_unique<MultiPartContentProvider>("text/html");
        
        // Header
        String header = R"====(<!DOCTYPE html>
            <html><head><title>Multi-Part Document</title></head><body>
            <h1>Multi-Part Streaming Example</h1>
            <p>This document is assembled from multiple sources:</p>
            <div style="background: #f0f0f0; padding: 10px; margin: 10px 0;">
        )====";
        multiProvider->addPart(std::make_unique<MemoryContentProvider>(
            (uint8_t*)header.c_str(), header.length(), "text/html"));
        
        // File content (if available)
        if (LittleFS.exists("/content.txt")) {
            multiProvider->addPart(std::make_unique<LittleFSProvider>("/content.txt"));
        } else {
            String fallback = "<p>File content would go here (upload content.txt to LittleFS)</p>";
            multiProvider->addPart(std::make_unique<MemoryContentProvider>(
                (uint8_t*)fallback.c_str(), fallback.length(), "text/html"));
        }
        
        // Generated content
        auto generator = [](uint8_t* buffer, size_t maxSize, size_t offset) -> size_t {
            if (offset >= 500) return 0; // Limit to 500 chars
            
            String content = "<p>Generated content line " + String(offset / 25 + 1) + "</p>\n";
            size_t remaining = 500 - offset;
            size_t toWrite = min(content.length(), min(maxSize, remaining));
            
            memcpy(buffer, content.c_str(), toWrite);
            return toWrite;
        };
        
        multiProvider->addPart(std::make_unique<GeneratorContentProvider>(
            generator, 500, "text/html"));
        
        // Footer
        String footer = R"====(
        </div>
        <p>Total parts combined: 3</p>
        <p>Generated at: )====" + String(millis()) + R"====( ms</p>
        </body></html>)====";
        multiProvider->addPart(std::make_unique<MemoryContentProvider>(
            (uint8_t*)footer.c_str(), footer.length(), "text/html"));
        
        streamCustomProvider(request, std::move(multiProvider), "multipart.html");
    });
    
    Serial.println("✓ Multi-part content routes registered");
}

void setupLiveStreaming() {
    // Live data stream
    server.on("/live", HTTP_GET, [](AsyncWebServerRequest *request) {
        int duration = 30; // Default 30 seconds
        if (request->hasParam("duration")) {
            duration = request->getParam("duration")->value().toInt();
            duration = constrain(duration, 5, 300); // 5 seconds to 5 minutes
        }
        
        auto provider = std::make_unique<LiveDataProvider>(duration, 500); // 500ms interval
        
        // Set response headers for streaming
        AsyncWebServerResponse* response = request->beginChunkedResponse("text/plain",
            [provider = std::move(provider)](uint8_t* buffer, size_t maxLen, size_t index) mutable -> size_t {
                if (!provider) return 0;
                return provider->readChunk(buffer, maxLen, index);
            });
        
        response->addHeader("Cache-Control", "no-cache");
        response->addHeader("Connection", "keep-alive");
        
        request->send(response);
    });
    
    Serial.println("✓ Live streaming routes registered");
}

void setupWebInterface() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = generateMainInterface();
        request->send(200, "text/html", html);
    });
    
    // Status API
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint32_t freeHeap, maxAlloc;
        WebServerControl::getMemoryStats(freeHeap, maxAlloc);
        
        String json = "{";
        json += "\"freeHeap\":" + String(freeHeap) + ",";
        json += "\"maxAlloc\":" + String(maxAlloc) + ",";
        json += "\"uptime\":" + String(millis()) + ",";
        json += "\"version\":\"" + WebServerControl::getVersion() + "\"";
        json += "}";
        
        request->send(200, "application/json", json);
    });
    
    Serial.println("✓ Web interface routes registered");
}

void streamCustomProvider(AsyncWebServerRequest* request, 
                         std::unique_ptr<ContentProvider> provider, 
                         const String& filename) {
    if (!provider || !provider->isReady()) {
        request->send(500, "text/plain", "Provider not ready");
        return;
    }
    
    size_t totalSize = provider->getTotalSize();
    String mimeType = provider->getMimeType();
    
    Serial.printf("Streaming %s (%u bytes, %s)\n", 
                  filename.c_str(), totalSize, mimeType.c_str());
    
    AsyncWebServerResponse* response = request->beginChunkedResponse(mimeType,
        [provider = std::move(provider), filename](uint8_t* buffer, size_t maxLen, size_t index) mutable -> size_t {
            if (!provider) return 0;
            
            size_t bytesRead = provider->readChunk(buffer, maxLen, index);
            
            // Log progress periodically
            static size_t lastLogged = 0;
            if (index > lastLogged + 10000) { // Every 10KB
                Serial.printf("Streamed %u bytes of %s\n", index + bytesRead, filename.c_str());
                lastLogged = index;
            }
            
            return bytesRead;
        });
    
    response->addHeader("Content-Length", String(totalSize));
    response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    
    request->send(response);
}

String generateMainInterface() {
    return R"====(
        <!DOCTYPE html>
        <html>
        <head>
            <title>Custom Provider Demo</title>
            <style>
                body { font-family: Arial, sans-serif; margin: 20px; max-width: 1200px; margin: 0 auto; padding: 20px; }
                .section { background: #f8f9fa; padding: 20px; margin: 20px 0; border-radius: 5px; }
                .example { margin: 15px 0; padding: 15px; background: white; border-radius: 3px; }
                .button { 
                    display: inline-block; padding: 8px 15px; background: #007bff; color: white; 
                    text-decoration: none; border-radius: 4px; margin: 5px; font-size: 14px;
                }
                .button:hover { background: #0056b3; }
                input, select { padding: 5px; margin: 5px; }
                .status { background: #e9ecef; padding: 15px; border-radius: 5px; margin-bottom: 20px; }
                .live-data { background: #000; color: #0f0; padding: 10px; font-family: monospace; height: 200px; overflow-y: scroll; }
            </style>
        </head>
        <body>
            <h1>WebServerControl Custom Provider Examples</h1>
            
            <div class="status">
                <h3>System Status</h3>
                <p><strong>Free Memory:</strong> <span id="memory">Loading...</span></p>
                <p><strong>Uptime:</strong> <span id="uptime">Loading...</span></p>
                <p><strong>Library Version:</strong> <span id="version">Loading...</span></p>
            </div>
            
            <div class="section">
                <h2>Sensor Data Streaming</h2>
                <div class="example">
                    <p>Generate JSON sensor data in chunks:</p>
                    <input type="number" id="sensorSamples" value="1000" min="10" max="10000">
                    <label>samples</label>
                    <a href="#" onclick="downloadSensors()" class="button">Download Sensor Data</a>
                </div>
            </div>
            
            <div class="section">
                <h2>Pattern Generation</h2>
                <div class="example">
                    <p>Generate binary patterns:</p>
                    <select id="patternType">
                        <option value="wave">Sine Wave</option>
                        <option value="square">Square Wave</option>
                        <option value="sawtooth">Sawtooth</option>
                        <option value="random">Random</option>
                    </select>
                    <input type="number" id="patternSize" value="32768" min="1024" max="1048576">
                    <label>bytes</label>
                    <a href="#" onclick="downloadPattern()" class="button">Generate Pattern</a>
                </div>
            </div>
            
            <div class="section">
                <h2>Multi-Part Content</h2>
                <div class="example">
                    <p>Combine multiple content sources into one document:</p>
                    <a href="/multipart" class="button">View Multi-Part Document</a>
                </div>
            </div>
            
            <div class="section">
                <h2>Memory Buffer</h2>
                <div class="example">
                    <p>Stream from memory buffer (8KB test pattern):</p>
                    <a href="/api/buffer" class="button">Download Buffer</a>
                </div>
            </div>
            
            <div class="section">
                <h2>Live Data Stream</h2>
                <div class="example">
                    <p>Real-time data streaming:</p>
                    <input type="number" id="liveDuration" value="30" min="5" max="300">
                    <label>seconds</label>
                    <button onclick="startLiveStream()" class="button">Start Live Stream</button>
                    <div id="liveData" class="live-data" style="display: none;"></div>
                </div>
            </div>
            
            <script>
                function downloadSensors() {
                    const samples = document.getElementById('sensorSamples').value;
                    window.open(`/api/sensors?samples=${samples}`);
                }
                
                function downloadPattern() {
                    const type = document.getElementById('patternType').value;
                    const size = document.getElementById('patternSize').value;
                    window.open(`/api/pattern?pattern=${type}&size=${size}`);
                }
                
                async function startLiveStream() {
                    const duration = document.getElementById('liveDuration').value;
                    const dataDiv = document.getElementById('liveData');
                    
                    dataDiv.style.display = 'block';
                    dataDiv.innerHTML = 'Starting live stream...\n';
                    
                    try {
                        const response = await fetch(`/live?duration=${duration}`);
                        const reader = response.body.getReader();
                        const decoder = new TextDecoder();
                        
                        while (true) {
                            const { done, value } = await reader.read();
                            if (done) break;
                            
                            const text = decoder.decode(value);
                            dataDiv.innerHTML += text;
                            dataDiv.scrollTop = dataDiv.scrollHeight;
                        }
                        
                        dataDiv.innerHTML += '\nStream completed.\n';
                    } catch (error) {
                        dataDiv.innerHTML += `\nError: ${error.message}\n`;
                    }
                }
                
                async function updateStatus() {
                    try {
                        const response = await fetch('/api/status');
                        const data = await response.json();
                        
                        document.getElementById('memory').textContent = 
                            `${data.freeHeap} bytes (max: ${data.maxAlloc})`;
                        document.getElementById('uptime').textContent = 
                            `${Math.floor(data.uptime / 1000)} seconds`;
                        document.getElementById('version').textContent = data.version;
                    } catch (error) {
                        console.error('Failed to update status:', error);
                    }
                }
                
                // Update status on load and every 10 seconds
                updateStatus();
                setInterval(updateStatus, 10000);
            </script>
        </body>
        </html>
    )====";
}

void printAvailableEndpoints() {
    Serial.println("\n=== Available Endpoints ===");
    Serial.printf("Main interface: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.printf("Sensor data: http://%s/api/sensors?samples=1000\n", WiFi.localIP().toString().c_str());
    Serial.printf("Pattern gen: http://%s/api/pattern?pattern=wave&size=32768\n", WiFi.localIP().toString().c_str());
    Serial.printf("Multi-part: http://%s/multipart\n", WiFi.localIP().toString().c_str());
    Serial.printf("Memory buffer: http://%s/api/buffer\n", WiFi.localIP().toString().c_str());
    Serial.printf("Live stream: http://%s/live?duration=30\n", WiFi.localIP().toString().c_str());
    Serial.println("===========================\n");
}

void printMemoryStats() {
    uint32_t freeHeap, maxAlloc;
    WebServerControl::getMemoryStats(freeHeap, maxAlloc);
    
    Serial.printf("[Memory] Free: %u bytes, Max alloc: %u bytes\n", freeHeap, maxAlloc);
}