/**
 * @file ContentProviders.h
 * @brief Additional content provider implementations for WebServerControl
 * @version 1.0.0
 * @date 2025-09-20
 */

#ifndef CONTENT_PROVIDERS_H
#define CONTENT_PROVIDERS_H

#include "WebServerControl.h"

/**
 * @brief Memory buffer content provider for serving data from RAM
 * WARNING: Use with caution - large buffers can cause heap overflow
 */
class MemoryContentProvider : public ContentProvider {
private:
    const uint8_t* _data;
    size_t _totalSize;
    String _mimeType;
    bool _isReady;
    bool _ownsData;

public:
    /**
     * @brief Constructor for external data (not owned by provider)
     * @param data Pointer to data buffer
     * @param size Size of data
     * @param mimeType MIME type of content
     */
    MemoryContentProvider(const uint8_t* data, size_t size, const String& mimeType)
        : _data(data), _totalSize(size), _mimeType(mimeType), 
          _isReady(data != nullptr && size > 0), _ownsData(false) {}
    
    /**
     * @brief Constructor that takes ownership of data
     * @param data Pointer to data buffer (will be deleted by destructor)
     * @param size Size of data
     * @param mimeType MIME type of content
     * @param takeOwnership If true, provider will delete data in destructor
     */
    MemoryContentProvider(uint8_t* data, size_t size, const String& mimeType, bool takeOwnership)
        : _data(data), _totalSize(size), _mimeType(mimeType), 
          _isReady(data != nullptr && size > 0), _ownsData(takeOwnership) {}
    
    ~MemoryContentProvider() {
        if (_ownsData && _data) {
            delete[] _data;
        }
    }
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !_data || !buffer || offset >= _totalSize) {
            return 0;
        }
        
        size_t remaining = _totalSize - offset;
        size_t toRead = min(maxSize, remaining);
        
        memcpy(buffer, _data + offset, toRead);
        return toRead;
    }
    
    size_t getTotalSize() const override { return _totalSize; }
    String getMimeType() const override { return _mimeType; }
    void reset() override { /* Nothing to reset */ }
    bool isReady() const override { return _isReady; }
};

/**
 * @brief Progressive data generator for creating content on-demand
 * Useful for generating large datasets without storing them in memory
 */
class GeneratorContentProvider : public ContentProvider {
private:
    std::function<size_t(uint8_t*, size_t, size_t)> _generator;
    size_t _totalSize;
    String _mimeType;
    bool _isReady;

public:
    /**
     * @brief Constructor
     * @param generator Function that generates data chunks
     * @param totalSize Total size of content to generate
     * @param mimeType MIME type of content
     */
    GeneratorContentProvider(std::function<size_t(uint8_t*, size_t, size_t)> generator,
                           size_t totalSize, const String& mimeType)
        : _generator(generator), _totalSize(totalSize), _mimeType(mimeType),
          _isReady(generator != nullptr) {}
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !_generator || !buffer || offset >= _totalSize) {
            return 0;
        }
        
        return _generator(buffer, maxSize, offset);
    }
    
    size_t getTotalSize() const override { return _totalSize; }
    String getMimeType() const override { return _mimeType; }
    void reset() override { /* Generators are typically stateless */ }
    bool isReady() const override { return _isReady; }
};

/**
 * @brief Multi-part content provider for combining multiple sources
 */
class MultiPartContentProvider : public ContentProvider {
private:
    struct ContentPart {
        std::unique_ptr<ContentProvider> provider;
        size_t startOffset;
        size_t size;
    };
    
    std::vector<ContentPart> _parts;
    size_t _totalSize;
    String _mimeType;
    bool _isReady;

public:
    /**
     * @brief Constructor
     * @param mimeType MIME type for the combined content
     */
    explicit MultiPartContentProvider(const String& mimeType = "application/octet-stream")
        : _totalSize(0), _mimeType(mimeType), _isReady(true) {}
    
    /**
     * @brief Add a content part
     * @param provider Content provider for this part
     * @return true if added successfully
     */
    bool addPart(std::unique_ptr<ContentProvider> provider) {
        if (!provider || !provider->isReady()) {
            return false;
        }
        
        ContentPart part;
        part.provider = std::move(provider);
        part.startOffset = _totalSize;
        part.size = part.provider->getTotalSize();
        
        _totalSize += part.size;
        _parts.push_back(std::move(part));
        
        return true;
    }
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !buffer || offset >= _totalSize) {
            return 0;
        }
        
        // Find which part contains this offset
        for (auto& part : _parts) {
            if (offset >= part.startOffset && offset < part.startOffset + part.size) {
                size_t partOffset = offset - part.startOffset;
                size_t partRemaining = part.size - partOffset;
                size_t toRead = min(maxSize, partRemaining);
                
                return part.provider->readChunk(buffer, toRead, partOffset);
            }
        }
        
        return 0;
    }
    
    size_t getTotalSize() const override { return _totalSize; }
    String getMimeType() const override { return _mimeType; }
    
    void reset() override {
        for (auto& part : _parts) {
            part.provider->reset();
        }
    }
    
    bool isReady() const override { return _isReady; }
};

/**
 * @brief Compressed content provider wrapper
 * Note: This is a placeholder for future compression support
 */
class CompressedContentProvider : public ContentProvider {
private:
    std::unique_ptr<ContentProvider> _sourceProvider;
    String _mimeType;
    bool _isReady;
    
public:
    /**
     * @brief Constructor
     * @param sourceProvider Provider for uncompressed content
     * @param compressionType Type of compression ("gzip", "deflate", etc.)
     */
    CompressedContentProvider(std::unique_ptr<ContentProvider> sourceProvider, 
                            const String& compressionType = "gzip")
        : _sourceProvider(std::move(sourceProvider)), _isReady(false) {
        
        if (_sourceProvider && _sourceProvider->isReady()) {
            _mimeType = _sourceProvider->getMimeType();
            // TODO: Implement actual compression
            // For now, just pass through
            _isReady = true;
        }
    }
    
    size_t readChunk(uint8_t* buffer, size_t maxSize, size_t offset) override {
        if (!_isReady || !_sourceProvider) {
            return 0;
        }
        
        // TODO: Implement compression/decompression
        // For now, just pass through to source
        return _sourceProvider->readChunk(buffer, maxSize, offset);
    }
    
    size_t getTotalSize() const override {
        return _sourceProvider ? _sourceProvider->getTotalSize() : 0;
    }
    
    String getMimeType() const override { return _mimeType; }
    void reset() override { if (_sourceProvider) _sourceProvider->reset(); }
    bool isReady() const override { return _isReady; }
};

#endif // CONTENT_PROVIDERS_H