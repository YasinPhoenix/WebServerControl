/**
 * @file WebServerControl.h
 * @brief Professional chunked streaming library for ESPAsyncWebServer
 * @version 1.0.0
 * @date 2025-09-20
 * 
 * A production-ready library that enhances ESPAsyncWebServer with efficient
 * chunked streaming capabilities for serving large content without heap overflow.
 * 
 * Features:
 * - Memory-safe chunked streaming
 * - Multiple content source providers
 * - ESP8266/ESP32 compatibility
 * - File system integration (SPIFFS, LittleFS, SD)
 * - Custom content generators
 * - Production error handling
 * 
 * @author Your Name
 * @license MIT
 */

#ifndef WEBSERVERCONTROL_H
#define WEBSERVERCONTROL_H

#include <Arduino.h>

#if defined(ESP8266)
    #include <ESPAsyncWebServer.h>
    #include <FS.h>
    #include <LittleFS.h>
#elif defined(ESP32)
    #include <ESPAsyncWebServer.h>
    #include <FS.h>
    #include <SPIFFS.h>
    #include <SD.h>
#else
    #error "WebServerControl only supports ESP8266 and ESP32"
#endif

#include <functional>
#include <memory>

// Forward declarations
class ContentProvider;
class FileContentProvider;
class CallbackContentProvider;

/**
 * @brief Configuration constants for the library
 */
namespace WebServerControlConfig {
    static const size_t DEFAULT_BUFFER_SIZE = 4096;    // 4KB chunks
    static const size_t MAX_BUFFER_SIZE = 8192;        // 8KB maximum
    static const size_t MIN_BUFFER_SIZE = 512;         // 512B minimum
    static const unsigned long DEFAULT_TIMEOUT_MS = 30000; // 30 seconds
}

/**
 * @brief Error codes for WebServerControl operations
 */
enum class WSCError {
    SUCCESS = 0,
    INVALID_PARAMETER,
    BUFFER_TOO_LARGE,
    BUFFER_TOO_SMALL,
    PROVIDER_ERROR,
    FILE_NOT_FOUND,
    MEMORY_ALLOCATION_FAILED,
    ASYNC_SERVER_ERROR,
    TIMEOUT,
    UNKNOWN_ERROR
};

/**
 * @brief Callback function type for generating content chunks
 * @param buffer Pointer to the buffer to fill
 * @param maxSize Maximum size that can be written to buffer
 * @param offset Current offset in the total content
 * @param userData Optional user data pointer
 * @return Number of bytes actually written to buffer (0 indicates end of content)
 */
typedef std::function<size_t(uint8_t* buffer, size_t maxSize, size_t offset, void* userData)> ContentCallback;

/**
 * @brief Progress callback for monitoring streaming progress
 * @param bytesTransferred Number of bytes transferred so far
 * @param totalBytes Total bytes to transfer
 * @param userData Optional user data pointer
 */
typedef std::function<void(size_t bytesTransferred, size_t totalBytes, void* userData)> ProgressCallback;

/**
 * @brief Abstract base class for content providers
 */
class ContentProvider {
public:
    virtual ~ContentProvider() = default;
    
    /**
     * @brief Read the next chunk of content
     * @param buffer Buffer to write content to
     * @param maxSize Maximum size to read
     * @param offset Current offset in the content
     * @return Number of bytes read (0 indicates end of content)
     */
    virtual size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) = 0;
    
    /**
     * @brief Get the total size of the content
     * @return Total content size in bytes
     */
    virtual size_t getTotalSize() const = 0;
    
    /**
     * @brief Get the MIME type of the content
     * @return MIME type string
     */
    virtual String getMimeType() const = 0;
    
    /**
     * @brief Reset the provider to the beginning
     */
    virtual void reset() = 0;
    
    /**
     * @brief Check if the provider is ready to provide content
     * @return true if ready, false otherwise
     */
    virtual bool isReady() const = 0;
};

/**
 * @brief Streaming context for managing active streams
 */
struct StreamingContext {
    std::unique_ptr<ContentProvider> provider;
    size_t bufferSize;
    size_t totalSize;
    size_t bytesTransferred;
    ProgressCallback progressCallback;
    void* userData;
    unsigned long startTime;
    bool isActive;
    
    StreamingContext() : bufferSize(WebServerControlConfig::DEFAULT_BUFFER_SIZE), 
                        totalSize(0), bytesTransferred(0), userData(nullptr),
                        startTime(0), isActive(false) {}
};

/**
 * @brief Main WebServerControl class for chunked streaming
 */
class WebServerControl {
private:
    AsyncWebServer* _server;
    size_t _defaultBufferSize;
    unsigned long _timeoutMs;
    bool _initialized;
    
    // Internal methods
    void handleStreamingRequest(AsyncWebServerRequest* request, std::unique_ptr<ContentProvider> provider, 
                               size_t bufferSize = 0, ProgressCallback progressCallback = nullptr, void* userData = nullptr);
    static String getMimeTypeFromExtension(const String& filename);
    static bool validateBufferSize(size_t bufferSize);
    void sendErrorResponse(AsyncWebServerRequest* request, int code, const String& message);

public:
    /**
     * @brief Construct WebServerControl with an AsyncWebServer instance
     * @param server Pointer to initialized AsyncWebServer
     * @param defaultBufferSize Default buffer size for streaming operations
     * @param timeoutMs Timeout for streaming operations in milliseconds
     */
    explicit WebServerControl(AsyncWebServer* server, 
                             size_t defaultBufferSize = WebServerControlConfig::DEFAULT_BUFFER_SIZE,
                             unsigned long timeoutMs = WebServerControlConfig::DEFAULT_TIMEOUT_MS);
    
    /**
     * @brief Destructor
     */
    ~WebServerControl();
    
    // Core streaming methods
    
    /**
     * @brief Stream content using a callback function
     * @param uri URI path to handle
     * @param method HTTP method (HTTP_GET, HTTP_POST, etc.)
     * @param callback Function to generate content chunks
     * @param totalSize Total size of content to be streamed
     * @param mimeType MIME type of the content
     * @param bufferSize Buffer size for streaming (0 = use default)
     * @param progressCallback Optional progress monitoring callback
     * @param userData Optional user data for callbacks
     * @return WSCError::SUCCESS on success, error code otherwise
     */
    WSCError streamCallback(const String& uri, WebRequestMethodComposite method, 
                           ContentCallback callback, size_t totalSize, 
                           const String& mimeType = "application/octet-stream",
                           size_t bufferSize = 0, ProgressCallback progressCallback = nullptr, void* userData = nullptr);
    
    /**
     * @brief Stream a file from filesystem
     * @param uri URI path to handle
     * @param filePath Path to the file in filesystem
     * @param method HTTP method
     * @param fs Filesystem to use (SPIFFS, LittleFS, SD, etc.)
     * @param bufferSize Buffer size for streaming (0 = use default)
     * @param progressCallback Optional progress monitoring callback
     * @param userData Optional user data for callbacks
     * @return WSCError::SUCCESS on success, error code otherwise
     */
    WSCError streamFile(const String& uri, const String& filePath, 
                       WebRequestMethodComposite method = HTTP_GET,
                       fs::FS& fs = LittleFS, size_t bufferSize = 0, 
                       ProgressCallback progressCallback = nullptr, void* userData = nullptr);
    
    /**
     * @brief Stream content using a custom provider
     * @param uri URI path to handle
     * @param method HTTP method
     * @param provider Unique pointer to content provider
     * @param bufferSize Buffer size for streaming (0 = use default)
     * @param progressCallback Optional progress monitoring callback
     * @param userData Optional user data for callbacks
     * @return WSCError::SUCCESS on success, error code otherwise
     */
    WSCError streamProvider(const String& uri, WebRequestMethodComposite method,
                           std::unique_ptr<ContentProvider> provider, size_t bufferSize = 0,
                           ProgressCallback progressCallback = nullptr, void* userData = nullptr);
    
    // Configuration methods
    
    /**
     * @brief Set default buffer size for streaming operations
     * @param bufferSize New default buffer size
     * @return WSCError::SUCCESS on success, error code otherwise
     */
    WSCError setDefaultBufferSize(size_t bufferSize);
    
    /**
     * @brief Get current default buffer size
     * @return Current default buffer size
     */
    size_t getDefaultBufferSize() const { return _defaultBufferSize; }
    
    /**
     * @brief Set timeout for streaming operations
     * @param timeoutMs Timeout in milliseconds
     */
    void setTimeout(unsigned long timeoutMs) { _timeoutMs = timeoutMs; }
    
    /**
     * @brief Get current timeout setting
     * @return Current timeout in milliseconds
     */
    unsigned long getTimeout() const { return _timeoutMs; }
    
    /**
     * @brief Check if the library is properly initialized
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const { return _initialized; }
    
    // Utility methods
    
    /**
     * @brief Convert error code to human-readable string
     * @param error Error code to convert
     * @return String description of the error
     */
    static String errorToString(WSCError error);
    
    /**
     * @brief Get library version
     * @return Version string
     */
    static String getVersion() { return "1.0.0"; }
    
    /**
     * @brief Get memory usage statistics
     * @param freeHeap Will be set to current free heap
     * @param maxAllocHeap Will be set to maximum allocatable heap block
     */
    static void getMemoryStats(uint32_t& freeHeap, uint32_t& maxAllocHeap);
};

#endif // WEBSERVERCONTROL_H