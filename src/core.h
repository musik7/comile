#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef EXPORT
#define EXPORT __attribute__((visibility("default")))
#endif

struct StringView {
    const char* data;
    size_t length;
    
    bool equals(const StringView& other) const {
        if (length != other.length) return false;
        for (size_t i = 0; i < length; ++i) {
            if (data[i] != other.data[i]) return false;
        }
        return true;
    }

    bool equals(const char* str) const {
        size_t i = 0;
        while (i < length && str[i] != '\0') {
            if (data[i] != str[i]) return false;
            i++;
        }
        return i == length && str[i] == '\0';
    }
};

inline void* operator new(size_t, void* p) throw() { return p; }
inline void* operator new[](size_t, void* p) throw() { return p; }

class ArenaAllocator {
private:
    char* buffer;
    size_t capacity;
    size_t offset;

public:
    ArenaAllocator(char* mem, size_t cap) : buffer(mem), capacity(cap), offset(0) {}

    void* allocate(size_t size, size_t alignment = 8) {
        size_t padding = (alignment - (offset % alignment)) % alignment;
        size_t aligned_size = size + padding;
        
        if (offset + aligned_size > capacity) return nullptr;
        
        void* ptr = buffer + offset + padding;
        offset += aligned_size;
        return ptr;
    }

    void reset() { offset = 0; }
    size_t get_used() const { return offset; }
    size_t get_capacity() const { return capacity; }
};
