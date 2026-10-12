#pragma once
// Host stand-in for ESP-IDF's heap_caps API, for the desktop simulator.
// The simulator is a normal process: malloc is the allocator and memory is
// not scarce, so the free-block queries report a roomy heap. Returning 0
// (as the unit-test stub does) would make every "is there room?" probe in
// the firmware refuse, and the simulator would show out-of-memory paths.
#include <cstddef>
#include <cstdint>
#include <cstdlib>

constexpr uint32_t MALLOC_CAP_DEFAULT = 1;
constexpr uint32_t MALLOC_CAP_SPIRAM = 2;
constexpr uint32_t MALLOC_CAP_INTERNAL = 4;
constexpr uint32_t MALLOC_CAP_8BIT = 8;

constexpr size_t kSimHeapBytes = 64u * 1024u * 1024u;

inline void* heap_caps_malloc(size_t size, uint32_t) { return std::malloc(size); }
inline void* heap_caps_calloc(size_t n, size_t size, uint32_t) { return std::calloc(n, size); }
inline void* heap_caps_realloc(void* ptr, size_t size, uint32_t) { return std::realloc(ptr, size); }
inline void heap_caps_free(void* ptr) { std::free(ptr); }
inline size_t heap_caps_get_free_size(uint32_t) { return kSimHeapBytes; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return kSimHeapBytes; }
inline size_t heap_caps_get_total_size(uint32_t) { return kSimHeapBytes; }
inline size_t heap_caps_get_minimum_free_size(uint32_t) { return kSimHeapBytes; }
