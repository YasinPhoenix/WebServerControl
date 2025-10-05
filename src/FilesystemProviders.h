/**
 * @file FilesystemProviders.h
 * @brief Specialized filesystem content providers for WebServerControl
 * @version 1.0.0
 * @date 2025-09-20
 */

#ifndef FILESYSTEM_PROVIDERS_H
#define FILESYSTEM_PROVIDERS_H

#include "WebServerControl.h"

#include <LittleFS.h>
#include <SD.h>

/**
 * @brief Enhanced file content provider with buffering and error handling
 */
class BufferedFileProvider : public ContentProvider {
private:
    fs::FS* _fs;
    String _filePath;
    String _mimeType;
    File _file;
    size_t _totalSize;
    size_t _bufferSize;
    uint8_t* _buffer;
    size_t _bufferOffset;
    size_t _bufferDataSize;
    bool _isReady;
    bool _eof;
    
    bool fillBuffer(size_t targetOffset) {
        if (!_file || !_buffer) {
            return false;
        }
        
        // If target offset is within current buffer, no need to refill
        if (targetOffset >= _bufferOffset && 
            targetOffset < _bufferOffset + _bufferDataSize) {
            return true;
        }
        
        // Seek to target position
        if (!_file.seek(targetOffset)) {
            return false;
        }
        
        // Read new buffer
        _bufferOffset = targetOffset;
        _bufferDataSize = _file.read(_buffer, _bufferSize);
        _eof = (_bufferDataSize == 0) || (_bufferOffset + _bufferDataSize >= _totalSize);
        
        return _bufferDataSize > 0;
    }

public:
    /**
     * @brief Constructor with custom buffer size
     * @param filesystem Filesystem to use
     * @param filePath Path to file
     * @param bufferSize Size of internal buffer (default 4KB)
     */
    BufferedFileProvider(fs::FS& filesystem, const String& filePath, 
                        size_t bufferSize = 4096)
        : _fs(&filesystem), _filePath(filePath), _totalSize(0), 
          _bufferSize(bufferSize), _buffer(nullptr), _bufferOffset(0),
          _bufferDataSize(0), _isReady(false), _eof(false) {
        
        if (!_fs->exists(_filePath)) {
            return;
        }
        
        _file = _fs->open(_filePath, "r");
        if (!_file) {
            return;
        }
        
        _totalSize = _file.size();
        _mimeType = WebServerControl::getMimeTypeFromExtension(_filePath);
        
        // Allocate buffer
        _buffer = new(std::nothrow) uint8_t[_bufferSize];
        if (!_buffer) {
            _file.close();
            return;
        }
        
        _isReady = true;
    }
    
    ~BufferedFileProvider() {
        if (_file) {
            _file.close();
        }
        if (_buffer) {
            delete[] _buffer;
        }
    }
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !buffer || offset >= _totalSize) {
            return 0;
        }
        
        // Fill internal buffer if needed
        if (!fillBuffer(offset)) {
            return 0;
        }
        
        // Calculate how much we can read from current buffer
        size_t bufferIndex = offset - _bufferOffset;
        size_t availableInBuffer = _bufferDataSize - bufferIndex;
        size_t toRead = min(maxSize, availableInBuffer);
        
        memcpy(buffer, _buffer + bufferIndex, toRead);
        return toRead;
    }
    
    size_t getTotalSize() const override { return _totalSize; }
    String getMimeType() const override { return _mimeType; }
    
    void reset() override {
        if (_file) {
            _file.seek(0);
            _bufferOffset = 0;
            _bufferDataSize = 0;
            _eof = false;
        }
    }
    
    bool isReady() const override { return _isReady; }
};

/**
 * @brief LittleFS-specific content provider
 */
class LittleFSProvider : public ContentProvider {
private:
    String _filePath;
    String _mimeType;
    File _file;
    size_t _totalSize;
    bool _isReady;

public:
    explicit LittleFSProvider(const String& filePath)
        : _filePath(filePath), _totalSize(0), _isReady(false) {
        
        if (!LittleFS.exists(_filePath)) {
            return;
        }
        
        _file = LittleFS.open(_filePath, "r");
        if (!_file) {
            return;
        }
        
        _totalSize = _file.size();
        _mimeType = WebServerControl::getMimeTypeFromExtension(_filePath);
        _isReady = true;
    }
    
    ~LittleFSProvider() {
        if (_file) {
            _file.close();
        }
    }
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !_file || !buffer) {
            return 0;
        }
        
        if (_file.position() != offset) {
            if (!_file.seek(offset)) {
                return 0;
            }
        }
        
        return _file.read(buffer, maxSize);
    }
    
    size_t getTotalSize() const override { return _totalSize; }
    String getMimeType() const override { return _mimeType; }
    
    void reset() override {
        if (_file) {
            _file.seek(0);
        }
    }
    
    bool isReady() const override { return _isReady; }
};

/**
 * @brief SD card content provider with error recovery
 */
class SDProvider : public ContentProvider {
private:
    String _filePath;
    String _mimeType;
    File _file;
    size_t _totalSize;
    bool _isReady;
    uint8_t _retryCount;
    static const uint8_t MAX_RETRIES = 3;
    
    bool reopenFile() {
        if (_file) {
            _file.close();
        }
        
        _file = SD.open(_filePath, FILE_READ);
        return _file;
    }

public:
    explicit SDProvider(const String& filePath)
        : _filePath(filePath), _totalSize(0), _isReady(false), _retryCount(0) {
        
        if (!SD.exists(_filePath)) {
            return;
        }
        
        _file = SD.open(_filePath, FILE_READ);
        if (!_file) {
            return;
        }
        
        _totalSize = _file.size();
        _mimeType = WebServerControl::getMimeTypeFromExtension(_filePath);
        _isReady = true;
    }
    
    ~SDProvider() {
        if (_file) {
            _file.close();
        }
    }
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !buffer) {
            return 0;
        }
        
        // Check if file is still valid
        if (!_file && _retryCount < MAX_RETRIES) {
            if (reopenFile()) {
                _retryCount++;
            } else {
                return 0;
            }
        }
        
        if (!_file) {
            return 0;
        }
        
        if (_file.position() != offset) {
            if (!_file.seek(offset)) {
                // Try to reopen file on seek failure
                if (_retryCount < MAX_RETRIES && reopenFile()) {
                    _retryCount++;
                    if (!_file.seek(offset)) {
                        return 0;
                    }
                } else {
                    return 0;
                }
            }
        }
        
        size_t bytesRead = _file.read(buffer, maxSize);
        if (bytesRead > 0) {
            _retryCount = 0; // Reset retry count on successful read
        }
        
        return bytesRead;
    }
    
    size_t getTotalSize() const override { return _totalSize; }
    String getMimeType() const override { return _mimeType; }
    
    void reset() override {
        if (_file) {
            _file.seek(0);
        }
        _retryCount = 0;
    }
    
    bool isReady() const override { return _isReady; }
};

/**
 * @brief Factory class for creating filesystem providers
 */
class FilesystemProviderFactory {
public:
    enum class FilesystemType {
        AUTO_DETECT,
        LITTLEFS,
        SD_CARD,
        GENERIC_FS
    };
    
    /**
     * @brief Create a filesystem provider
     * @param filePath Path to the file
     * @param fsType Type of filesystem (AUTO_DETECT will try to determine automatically)
     * @param customFS Custom filesystem pointer (for GENERIC_FS type)
     * @return Unique pointer to content provider, or nullptr on failure
     */
    static std::unique_ptr<ContentProvider> create(const String& filePath, 
                                                  FilesystemType fsType = FilesystemType::AUTO_DETECT,
                                                  fs::FS* customFS = nullptr) {
        
        switch (fsType) {
            case FilesystemType::LITTLEFS:
                return std::make_unique<LittleFSProvider>(filePath);
                
            case FilesystemType::SD_CARD:
                return std::make_unique<SDProvider>(filePath);
                
            case FilesystemType::GENERIC_FS:
                if (customFS) {
                    return std::make_unique<BufferedFileProvider>(*customFS, filePath);
                }
                break;
                
            case FilesystemType::AUTO_DETECT:
            default:
                // Try different filesystems in order of preference
                if (LittleFS.exists(filePath)) {
                    return std::make_unique<LittleFSProvider>(filePath);
                }
                if (SD.exists(filePath)) {
                    return std::make_unique<SDProvider>(filePath);
                }
                break;
        }
        
        return nullptr;
    }
};

#endif // FILESYSTEM_PROVIDERS_H