/**
 * @file FileStreaming.ino
 * @brief Advanced file streaming example with multiple filesystem support
 * @version 1.0.0
 * @date 2025-09-20
 * 
 * This example demonstrates advanced file streaming capabilities including:
 * - Filesystem support (LittleFS)
 * - Automatic filesystem detection
 * - File upload handling
 * - Directory browsing
 * - File management operations
 */

#include <WebServerControl.h>
#include <FilesystemProviders.h>

#include <ESP8266WiFi.h>

// WiFi credentials
const char* ssid = "your-wifi-ssid";
const char* password = "your-wifi-password";

// Server and streaming control
AsyncWebServer server(80);
WebServerControl streamControl(&server);

// Available filesystems
struct FilesystemInfo {
    String name;
    fs::FS* fs;
    bool available;
};

std::vector<FilesystemInfo> filesystems;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("WebServerControl Advanced File Streaming");
    Serial.println("=======================================");
    
    // Initialize filesystems
    initializeFilesystems();
    
    // Connect to WiFi
    connectToWiFi();
    
    // Setup routes
    setupFileStreamingRoutes();
    setupFileManagementRoutes();
    setupWebInterface();
    
    // Start server
    server.begin();
    Serial.println("✓ Server started");
    
    printSystemInfo();
}

void loop() {
    delay(1000);
}

void initializeFilesystems() {
    Serial.println("Initializing filesystems...");
    
    // ESP8266: LittleFS
    if (LittleFS.begin()) {
        filesystems.push_back({"LittleFS", &LittleFS, true});
        Serial.println("✓ LittleFS mounted");
    } else {
        filesystems.push_back({"LittleFS", &LittleFS, false});
        Serial.println("✗ LittleFS failed to mount");
    }
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

void setupFileStreamingRoutes() {
    // Dynamic file streaming route
    server.on("^\\/stream\\/([^\\/]+)\\/(.+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String fsName = request->pathArg(0);
        const char* filePath = ("/" + request->pathArg(1)).c_str();
        
        Serial.printf("Stream request: %s from %s\n", filePath, fsName.c_str());
        
        // Find filesystem
        fs::FS* targetFS = nullptr;
        for (auto& fs : filesystems) {
            if (fs.name.equalsIgnoreCase(fsName) && fs.available) {
                targetFS = fs.fs;
                break;
            }
        }
        
        if (!targetFS) {
            request->send(404, "text/plain", "Filesystem '" + fsName + "' not available");
            return;
        }
        
        // Check if file exists
        if (!targetFS->exists(filePath)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "File not found: %s", filePath);
            request->send(404, "text/plain", msg);
            return;
        }
        
        // Create provider using factory
        auto provider = FilesystemProviderFactory::create(filePath, 
            FilesystemProviderFactory::FilesystemType::GENERIC_FS, targetFS);
        
        if (!provider || !provider->isReady()) {
            request->send(500, "text/plain", "Failed to create file provider");
            return;
        }
        
        // Convert to shared_ptr for lambda capture
        std::shared_ptr<ContentProvider> sharedProvider = std::move(provider);
        
        // Stream the file
        size_t totalSize = sharedProvider->getTotalSize();
        String mimeType = sharedProvider->getMimeType();
        
        AsyncWebServerResponse* response = request->beginChunkedResponse(mimeType,
            [sharedProvider](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                if (!sharedProvider) return 0;
                return sharedProvider->readChunk(buffer, maxLen, index);
            });
        
        const char* filename = strrchr(filePath, '/');
        if (filename) filename++;  // move past '/'
        else filename = filePath;   // no '/' found, whole path is filename

        char headerValue[128];
        snprintf(headerValue, sizeof(headerValue), "inline; filename=\"%s\"", filename);
        response->addHeader("Content-Length", String(totalSize));
        response->addHeader("Content-Disposition", headerValue);
        
        request->send(response);
    });
    
    Serial.println("✓ Dynamic file streaming routes registered");
}

void setupFileManagementRoutes() {
    // List files in filesystem
    server.on("^\\/list\\/([^\\/]+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String fsName = request->pathArg(0);
        String path = request->arg("path");
        if (path.isEmpty()) path = "/";
        
        // Find filesystem
        fs::FS* targetFS = nullptr;
        for (auto& fs : filesystems) {
            if (fs.name.equalsIgnoreCase(fsName) && fs.available) {
                targetFS = fs.fs;
                break;
            }
        }
        
        if (!targetFS) {
            request->send(404, "application/json", "{\"error\":\"Filesystem not available\"}");
            return;
        }
        
        String json = listDirectory(*targetFS, path);
        request->send(200, "application/json", json);
    });
    
    // File info endpoint
    server.on("^\\/info\\/([^\\/]+)\\/(.+)$", HTTP_GET, [](AsyncWebServerRequest *request) {
        String fsName = request->pathArg(0);
        String filePath = "/" + request->pathArg(1);
        
        // Find filesystem
        fs::FS* targetFS = nullptr;
        for (auto& fs : filesystems) {
            if (fs.name.equalsIgnoreCase(fsName) && fs.available) {
                targetFS = fs.fs;
                break;
            }
        }
        
        if (!targetFS || !targetFS->exists(filePath)) {
            request->send(404, "application/json", "{\"error\":\"File not found\"}");
            return;
        }
        
        File file = targetFS->open(filePath, "r");
        if (!file) {
            request->send(500, "application/json", "{\"error\":\"Cannot open file\"}");
            return;
        }
        
        String json = "{";
        json += "\"name\":\"" + filePath.substring(filePath.lastIndexOf('/') + 1) + "\",";
        json += "\"path\":\"" + filePath + "\",";
        json += "\"size\":" + String(file.size()) + ",";
        json += "\"filesystem\":\"" + fsName + "\"";
        json += "}";
        
        file.close();
        request->send(200, "application/json", json);
    });
    
    Serial.println("✓ File management routes registered");
}

void setupWebInterface() {
    // Main interface
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = generateMainPage();
        request->send(200, "text/html", html);
    });
    
    // Filesystem status API
    server.on("/api/filesystems", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "[";
        for (size_t i = 0; i < filesystems.size(); i++) {
            if (i > 0) json += ",";
            json += "{";
            json += "\"name\":\"" + filesystems[i].name + "\",";
            json += "\"available\":" + String(filesystems[i].available ? "true" : "false");
            json += "}";
        }
        json += "]";
        
        request->send(200, "application/json", json);
    });
    
    Serial.println("✓ Web interface routes registered");
}

String listDirectory(fs::FS& fs, const String& path) {
    String json = "{\"path\":\"" + path + "\",\"files\":[";
    
    File root = fs.open(path, "r");
    if (!root || !root.isDirectory()) {
        json += "],\"error\":\"Invalid directory\"}";
        return json;
    }
    
    bool first = true;
    File file = root.openNextFile();
    while (file) {
        if (!first) json += ",";
        first = false;
        
        json += "{";
        json += "\"name\":\"" + String(file.name()) + "\",";
        json += "\"size\":" + String(file.size()) + ",";
        json += "\"isDirectory\":" + String(file.isDirectory() ? "true" : "false");
        json += "}";
        
        file = root.openNextFile();
    }
    
    json += "]}";
    return json;
}

String generateMainPage() {
    String html = R"====(
        <!DOCTYPE html>
        <html>
        <head>
            <title>Advanced File Streaming Demo</title>
            <style>
                body { font-family: Arial, sans-serif; margin: 20px; }
                .container { max-width: 1200px; margin: 0 auto; }
                .filesystem { background: #f8f9fa; padding: 20px; margin: 10px 0; border-radius: 5px; }
                .filesystem.unavailable { background: #f5f5f5; color: #666; }
                .file-list { margin: 10px 0; }
                .file-item { 
                    display: flex; 
                    justify-content: space-between; 
                    padding: 5px 10px; 
                    background: white; 
                    margin: 2px 0; 
                    border-radius: 3px;
                }
                .button { 
                    padding: 5px 10px; 
                    background: #007bff; 
                    color: white; 
                    text-decoration: none; 
                    border-radius: 3px; 
                    font-size: 12px;
                }
                .button:hover { background: #0056b3; }
                .button.small { padding: 3px 8px; font-size: 11px; }
                .status { padding: 10px; background: #e9ecef; border-radius: 5px; margin-bottom: 20px; }
            </style>
        </head>
        <body>
            <div class="container">
                <h1>Advanced File Streaming Demo</h1>

                <div class="status">
                    <h3>System Status</h3>
                    <p><strong>Available Filesystems:</strong> <span id="fs-count">Loading...</span></p>
                    <p><strong>Free Memory:</strong> <span id="memory">Loading...</span></p>
                </div>

                <div id="filesystems">
                    Loading filesystems...
                </div>

                <div style="margin-top: 30px; padding: 20px; background: #e7f3ff; border-radius: 5px;">
                    <h3>Usage Examples</h3>
                    <p><strong>Stream file:</strong> /stream/{filesystem}/{filepath}</p>
                    <p><strong>List directory:</strong> /list/{filesystem}?path=/path</p>
                    <p><strong>File info:</strong> /info/{filesystem}/{filepath}</p>
                </div>
            </div>

            <script>
                async function loadFilesystems() {
                    try {
                        const response = await fetch('/api/filesystems');
                        const filesystems = await response.json();

                        document.getElementById('fs-count').textContent = 
                            filesystems.filter(fs => fs.available).length + ' of ' + filesystems.length;

                        const container = document.getElementById('filesystems');
                        container.innerHTML = '';

                        for (const fs of filesystems) {
                            const div = document.createElement('div');
                            div.className = 'filesystem' + (fs.available ? '' : ' unavailable');
                            div.innerHTML = `
                                <h3>${fs.name} ${fs.available ? '✓' : '✗'}</h3>
                                ${fs.available ? `
                                    <button onclick="loadFiles('${fs.name}')" class="button">List Files</button>
                                    <div id="files-${fs.name}" class="file-list"></div>
                                ` : '<p>Filesystem not available</p>'}
                            `;
                            container.appendChild(div);
                        }
                    } catch (error) {
                        console.error('Failed to load filesystems:', error);
                    }
                }

                async function loadFiles(fsName) {
                    try {
                        const response = await fetch(`/list/${fsName}?path=/`);
                        const data = await response.json();

                        const container = document.getElementById(`files-${fsName}`);

                        if (data.error) {
                            container.innerHTML = `<p>Error: ${data.error}</p>`;
                            return;
                        }

                        if (data.files.length === 0) {
                            container.innerHTML = '<p>No files found</p>';
                            return;
                        }

                        container.innerHTML = data.files.map(file => `
                            <div class="file-item">
                                <span>${file.name} (${file.size} bytes)</span>
                                <div>
                                    ${!file.isDirectory ? `
                                        <a href="/stream/${fsName}${data.path}${file.name}" class="button small">Stream</a>
                                        <a href="/info/${fsName}${data.path}${file.name}" class="button small">Info</a>
                                    ` : ''}
                                </div>
                            </div>
                        `).join('');

                    } catch (error) {
                        console.error('Failed to load files:', error);
                    }
                }

                // Load on page load
                loadFilesystems();

                // Auto-refresh every 30 seconds
                setInterval(loadFilesystems, 30000);
            </script>
        </body>
        </html>
    )====";

    return html;
}

void printSystemInfo() {
    Serial.println("\n=== System Information ===");
    
    uint32_t freeHeap, maxAlloc;
    WebServerControl::getMemoryStats(freeHeap, maxAlloc);
    
    Serial.printf("Free Heap: %u bytes\n", freeHeap);
    Serial.printf("Max Allocatable: %u bytes\n", maxAlloc);
    
    Serial.println("\nAvailable Filesystems:");
    for (const auto& fs : filesystems) {
        Serial.printf("  %s: %s\n", fs.name.c_str(), fs.available ? "Available" : "Not available");
    }
    
    Serial.println("\nEndpoints:");
    Serial.printf("  Main interface: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.printf("  Stream file: http://%s/stream/{filesystem}/{filepath}\n", WiFi.localIP().toString().c_str());
    Serial.printf("  List files: http://%s/list/{filesystem}\n", WiFi.localIP().toString().c_str());
    Serial.println("==========================\n");
}