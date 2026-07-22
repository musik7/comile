#pragma once
#include "core.h"

enum class LogLevelFilter : uint8_t {
    ALL = 0,
    LOG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4
};

struct VirtualWindowResult {
    size_t total_items;
    size_t filtered_items;
    float total_height_px;
    size_t start_index;
    size_t end_index;
    float top_padding_px;
    float bottom_padding_px;
};

struct LogItemMeta {
    uint32_t id;
    LogLevelFilter level;
    uint64_t timestamp_us;
    StringView message;
    StringView source_url;
    uint32_t line_number;
};

class VirtualListEngine {
private:
    ArenaAllocator* arena;

    // Log storage
    LogItemMeta* items;
    size_t capacity;
    size_t count;

    // Filtered index buffer for ultra-fast rendering
    uint32_t* filtered_indices;
    size_t filtered_count;

public:
    VirtualListEngine(ArenaAllocator* alloc, size_t max_items = 100000)
        : arena(alloc), capacity(max_items), count(0), filtered_count(0) {
        items = (LogItemMeta*)arena->allocate(sizeof(LogItemMeta) * capacity);
        filtered_indices = (uint32_t*)arena->allocate(sizeof(uint32_t) * capacity);
    }

    void reset() {
        count = 0;
        filtered_count = 0;
    }

    void add_log(uint32_t id, LogLevelFilter level, StringView msg, StringView source, uint32_t line, uint64_t timestamp_us) {
        if (count >= capacity) return;
        LogItemMeta& item = items[count];
        item.id = id;
        item.level = level;
        item.message = msg;
        item.source_url = source;
        item.line_number = line;
        item.timestamp_us = timestamp_us;

        filtered_indices[filtered_count++] = (uint32_t)count;
        count++;
    }

    void apply_filter(LogLevelFilter level_filter, StringView search_term = {"", 0}) {
        filtered_count = 0;
        for (size_t i = 0; i < count; ++i) {
            bool matches_level = (level_filter == LogLevelFilter::ALL) || (items[i].level == level_filter);
            if (!matches_level) continue;

            if (search_term.length > 0 && search_term.data) {
                // Simple sub-string check in message
                bool matches_search = false;
                StringView msg = items[i].message;
                if (msg.length >= search_term.length) {
                    for (size_t k = 0; k <= msg.length - search_term.length; ++k) {
                        bool match = true;
                        for (size_t m = 0; m < search_term.length; ++m) {
                            if (msg.data[k + m] != search_term.data[m]) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            matches_search = true;
                            break;
                        }
                    }
                }
                if (!matches_search) continue;
            }

            filtered_indices[filtered_count++] = (uint32_t)i;
        }
    }

    VirtualWindowResult compute_window(float scroll_top, float viewport_height, float item_height = 28.0f, size_t overscan = 5) {
        VirtualWindowResult res;
        res.total_items = count;
        res.filtered_items = filtered_count;
        res.total_height_px = (float)filtered_count * item_height;

        if (filtered_count == 0) {
            res.start_index = 0;
            res.end_index = 0;
            res.top_padding_px = 0;
            res.bottom_padding_px = 0;
            return res;
        }

        int raw_start = (int)(scroll_top / item_height) - (int)overscan;
        size_t start_idx = raw_start < 0 ? 0 : (size_t)raw_start;

        size_t visible_count = (size_t)(viewport_height / item_height) + 1;
        size_t end_idx = start_idx + visible_count + (overscan * 2);
        if (end_idx > filtered_count) end_idx = filtered_count;

        res.start_index = start_idx;
        res.end_index = end_idx;
        res.top_padding_px = (float)start_idx * item_height;
        res.bottom_padding_px = (float)(filtered_count - end_idx) * item_height;

        return res;
    }

    size_t get_filtered_count() const { return filtered_count; }
    const uint32_t* get_filtered_indices() const { return filtered_indices; }
    const LogItemMeta* get_item(size_t raw_index) const {
        if (raw_index < count) return &items[raw_index];
        return nullptr;
    }
};
