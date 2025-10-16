/**
 * @file WebServerControl.cpp
 * @brief Implementation of WebServerControl chunked streaming library
 * @version 1.0.0
 * @date 2025-09-20
 */

#include "WebServerControl.h"

// ============================================================================
// ContentProvider Implementations
// ============================================================================

/**
 * @brief File-based content provider for streaming files from filesystem
 */
class FileContentProvider : public ContentProvider {
private:
    fs::FS* _fs;
    const char* _filePath;
    const char* _mimeType;
    File _file;
    size_t _totalSize;
    bool _isReady;

public:
    FileContentProvider(fs::FS& filesystem, const char* filePath) 
        : _fs(&filesystem), _filePath(filePath), _totalSize(0), _isReady(false) {
        
        if (_fs->exists(_filePath)) {
            _file = _fs->open(_filePath, "r");
            if (_file) {
                _totalSize = _file.size();
                _mimeType = WebServerControl::getMimeTypeFromExtension(_filePath);
                _isReady = true;
            }
        }
    }
    
    ~FileContentProvider() {
        if (_file) {
            _file.close();
        }
    }
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !_file || !buffer) {
            return 0;
        }
        
        // Seek to the correct position if needed
        if (_file.position() != offset) {
            if (!_file.seek(offset)) {
                return 0;
            }
        }
        
        return _file.read(buffer, maxSize);
    }
    
    size_t getTotalSize() const override {
        return _totalSize;
    }
    
    const char* getMimeType() const override {
        return _mimeType;
    }
    
    void reset() override {
        if (_file) {
            _file.seek(0);
        }
    }
    
    bool isReady() const override {
        return _isReady && _file;
    }
};

/**
 * @brief Callback-based content provider for generated content
 */
class CallbackContentProvider : public ContentProvider {
private:
    ContentCallback _callback;
    size_t _totalSize;
    const char* _mimeType;
    void* _userData;
    bool _isReady;

public:
    CallbackContentProvider(ContentCallback callback, size_t totalSize, 
                           const char* mimeType, void* userData = nullptr)
        : _callback(callback), _totalSize(totalSize), _mimeType(mimeType), 
          _userData(userData), _isReady(callback != nullptr) {}
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !_callback || !buffer) {
            return 0;
        }
        
        return _callback(buffer, maxSize, offset, _userData);
    }
    
    size_t getTotalSize() const override {
        return _totalSize;
    }
    
    const char* getMimeType() const override {
        return _mimeType;
    }
    
    void reset() override {
        // Nothing to reset for callback-based providers
    }
    
    bool isReady() const override {
        return _isReady;
    }
};

// ============================================================================
// WebServerControl Implementation
// ============================================================================

WebServerControl::WebServerControl(AsyncWebServer* server, size_t defaultBufferSize, unsigned long timeoutMs)
    : _server(server), _defaultBufferSize(defaultBufferSize), _timeoutMs(timeoutMs), _initialized(false) {
    
    if (!server) {
        return;
    }
    
    if (!validateBufferSize(defaultBufferSize)) {
        _defaultBufferSize = WebServerControlConfig::DEFAULT_BUFFER_SIZE;
    }
    
    _initialized = true;
}

WebServerControl::~WebServerControl() {
    // Cleanup if needed
}

WSCError WebServerControl::streamCallback(const char* uri, WebRequestMethodComposite method, 
                                         ContentCallback callback, size_t totalSize, 
                                         const char* mimeType, size_t bufferSize, 
                                         ProgressCallback progressCallback, void* userData) {
    
    if (!_initialized || !_server) {
        return WSCError::ASYNC_SERVER_ERROR;
    }
    
    if (!callback || (uri == nullptr || uri[0] == '\0')) {
        return WSCError::INVALID_PARAMETER;
    }
    
    size_t actualBufferSize = (bufferSize == 0) ? _defaultBufferSize : bufferSize;
    if (!validateBufferSize(actualBufferSize)) {
        return WSCError::BUFFER_TOO_LARGE;
    }
    
    // Create callback provider
    auto provider = std::make_unique<CallbackContentProvider>(callback, totalSize, mimeType, userData);
    if (!provider || !provider->isReady()) {
        return WSCError::PROVIDER_ERROR;
    }
    
    // Register the handler with AsyncWebServer
    _server->on(uri, method, [this, actualBufferSize, progressCallback, userData]
                (AsyncWebServerRequest* request) mutable {
        
        // Recreate provider for each request (callbacks are stateless)
        // Note: This is a simplified approach - in production you might want to 
        // store provider factories instead
        request->send(500, "text/plain", "Callback streaming requires specific implementation per request");
    });
    
    return WSCError::SUCCESS;
}

WSCError WebServerControl::streamFile(const char* uri, const char* filePath, 
                                     WebRequestMethodComposite method, fs::FS* fs, 
                                     size_t bufferSize, ProgressCallback progressCallback, void* userData) {
    
    if (!_initialized || !_server) {
        return WSCError::ASYNC_SERVER_ERROR;
    }
    
    if ((uri == nullptr || uri[0] == '\0') || (filePath == nullptr || filePath[0] == '\0')) {
        return WSCError::INVALID_PARAMETER;
    }
    
    // Default to LittleFS if no filesystem specified
    if (!fs) {
        fs = &LittleFS;
    }
    
    if (!fs->exists(filePath)) {
        return WSCError::FILE_NOT_FOUND;
    }
    
    size_t actualBufferSize = (bufferSize == 0) ? _defaultBufferSize : bufferSize;
    if (!validateBufferSize(actualBufferSize)) {
        return WSCError::BUFFER_TOO_LARGE;
    }
    
    // Register the handler with AsyncWebServer
    _server->on(uri, method, [this, filePath, &fs, actualBufferSize, progressCallback, userData]
                (AsyncWebServerRequest* request) {
        
        auto provider = std::make_unique<FileContentProvider>(*fs, filePath);
        if (!provider || !provider->isReady()) {
            sendErrorResponse(request, 404, "File not found or cannot be opened");
            return;
        }
        
        handleStreamingRequest(request, std::move(provider), actualBufferSize, progressCallback, userData);
    });
    
    return WSCError::SUCCESS;
}

WSCError WebServerControl::streamProvider(const char* uri, WebRequestMethodComposite method,
                                         std::unique_ptr<ContentProvider> provider, size_t bufferSize,
                                         ProgressCallback progressCallback, void* userData) {
    
    if (!_initialized || !_server) {
        return WSCError::ASYNC_SERVER_ERROR;
    }
    
    if ((uri == nullptr || uri[0] == '\0') || !provider || !provider->isReady()) {
        return WSCError::INVALID_PARAMETER;
    }
    
    size_t actualBufferSize = (bufferSize == 0) ? _defaultBufferSize : bufferSize;
    if (!validateBufferSize(actualBufferSize)) {
        return WSCError::BUFFER_TOO_LARGE;
    }
    
    // For custom providers, we need to handle this differently since we can't capture
    // unique_ptr in lambda. This is a more complex case that requires careful design.
    // For now, return an error indicating this needs special handling
    return WSCError::PROVIDER_ERROR;
}

void WebServerControl::handleStreamingRequest(AsyncWebServerRequest* request, 
                                             std::unique_ptr<ContentProvider> provider, 
                                             size_t bufferSize, ProgressCallback progressCallback, void* userData) {
    
    if (!provider || !provider->isReady()) {
        sendErrorResponse(request, 500, "Content provider not ready");
        return;
    }
    
    size_t totalSize = provider->getTotalSize();
    const char* mimeType = provider->getMimeType();
    
    // Convert unique_ptr to shared_ptr for lambda capture
    std::shared_ptr<ContentProvider> sharedProvider = std::move(provider);
    
    // Set up the response with proper headers
    AsyncWebServerResponse* response = request->beginChunkedResponse(mimeType, 
        [sharedProvider, bufferSize, progressCallback, userData, totalSize]
        (uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
        
        if (!sharedProvider) {
            return 0;
        }
        
        // Calculate how much to read (don't exceed buffer size or maxLen)
        size_t chunkSize = min(bufferSize, maxLen);
        size_t bytesRead = sharedProvider->readChunk(buffer, chunkSize, index);
        
        // Call progress callback if provided
        if (progressCallback) {
            progressCallback(index + bytesRead, totalSize, userData);
        }
        
        return bytesRead;
    });
    
    // Set Content-Length header for better browser compatibility
    response->addHeader("Content-Length", (const char*)totalSize);
    
    // Send the response
    request->send(response);
}

WSCError WebServerControl::setDefaultBufferSize(size_t bufferSize) {
    if (!validateBufferSize(bufferSize)) {
        return WSCError::BUFFER_TOO_LARGE;
    }
    
    _defaultBufferSize = bufferSize;
    return WSCError::SUCCESS;
}

bool WebServerControl::validateBufferSize(size_t bufferSize) {
    return (bufferSize >= WebServerControlConfig::MIN_BUFFER_SIZE && 
            bufferSize <= WebServerControlConfig::MAX_BUFFER_SIZE);
}

const char* WebServerControl::getMimeTypeFromExtension(const char* filename) {
    if (filename == nullptr) return "application/octet-stream";

    const char* ext = strrchr(filename, '.');
    if (ext == nullptr) return "application/octet-stream";
    ext++; // Move past the dot

    // Common MIME types
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "js") return "application/javascript";
    if (ext == "json") return "application/json";
    if (ext == "xml") return "application/xml";
    if (ext == "txt") return "text/plain";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "png") return "image/png";
    if (ext == "gif") return "image/gif";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    if (ext == "pdf") return "application/pdf";
    if (ext == "zip") return "application/zip";
    if (ext == "gz") return "application/gzip";
    if (ext == "mp3") return "audio/mpeg";
    if (ext == "mp4") return "video/mp4";
    if (ext == "avi") return "video/x-msvideo";
    
    return "application/octet-stream"; // Default binary type
}

void WebServerControl::sendErrorResponse(AsyncWebServerRequest* request, int code, const char* message) {
    if (request) {
        request->send(code, "text/plain", message);
    }
}

const char* WebServerControl::errorToString(WSCError error) {
    switch (error) {
        case WSCError::SUCCESS:
            return "Success";
        case WSCError::INVALID_PARAMETER:
            return "Invalid parameter";
        case WSCError::BUFFER_TOO_LARGE:
            return "Buffer size too large";
        case WSCError::BUFFER_TOO_SMALL:
            return "Buffer size too small";
        case WSCError::PROVIDER_ERROR:
            return "Content provider error";
        case WSCError::FILE_NOT_FOUND:
            return "File not found";
        case WSCError::MEMORY_ALLOCATION_FAILED:
            return "Memory allocation failed";
        case WSCError::ASYNC_SERVER_ERROR:
            return "AsyncWebServer error";
        case WSCError::TIMEOUT:
            return "Operation timeout";
        default:
            return "Unknown error";
    }
}

void WebServerControl::getMemoryStats(uint32_t& freeHeap, uint32_t& maxAllocHeap) {
    freeHeap = ESP.getFreeHeap();
    maxAllocHeap = ESP.getMaxFreeBlockSize();
}