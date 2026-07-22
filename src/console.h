#pragma once
#include "core.h"

enum class ConsoleMessageType : uint8_t {
    LOG = 0,
    DIR = 1,
    TABLE = 2,
    MEMORY = 3,
    GROUP = 4,
    GROUP_END = 5,
    TIME = 6,
    TIME_END = 7,
    ERROR = 8,
    WARN = 9,
    INFO = 10
};

enum class LogLevel : uint8_t { Verbose = 0, Info = 1, Warning = 2, Error = 3, WASM = 4 };

struct ConsoleMessage {
    uint32_t id;
    ConsoleMessageType type;
    LogLevel level;
    uint32_t group_depth;
    StringView message;
    StringView source_url; // filename e.g. "brain.cpp" or "main.ts"
    uint32_t line;
    uint32_t column;
    uint64_t timestamp; // epoch time in microseconds
    StringView stack_trace; // C++ / JS call stack trace
    StringView json_payload; // structured JSON data for dir, table, memory
};

struct TimerEntry {
    char label[64];
    uint64_t start_time_us;
};

class ConsoleEngine {
private:
    ArenaAllocator* arena;
    ConsoleMessage* messages;
    size_t capacity;
    size_t head;
    size_t count;
    uint64_t total_logged;

    uint32_t current_group_depth;

    // Timer slots for console.time / timeEnd
    TimerEntry timers[32];
    size_t timer_count;

    bool string_contains(StringView source, StringView query) const {
        if (query.length == 0) return true;
        if (source.length < query.length) return false;
        
        for (size_t i = 0; i <= source.length - query.length; ++i) {
            bool match = true;
            for (size_t j = 0; j < query.length; ++j) {
                char c1 = source.data[i + j];
                char c2 = query.data[j];
                if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
                if (c1 != c2) {
                    match = false;
                    break;
                }
            }
            if (match) return true;
        }
        return false;
    }

public:
    ConsoleEngine(ArenaAllocator* alloc, size_t max_msgs = 50000) 
        : arena(alloc), capacity(max_msgs), head(0), count(0), total_logged(0),
          current_group_depth(0), timer_count(0) {
        messages = (ConsoleMessage*)arena->allocate(sizeof(ConsoleMessage) * capacity);
    }

    void reset() {
        head = 0;
        count = 0;
        total_logged = 0;
        current_group_depth = 0;
        timer_count = 0;
    }

    void clear() {
        head = 0;
        count = 0;
    }

    // Generic Logger in C++
    void add_message(ConsoleMessageType type, LogLevel level, StringView msg, StringView url, uint32_t line, uint32_t col, uint64_t time, StringView stack = {"", 0}, StringView json = {"", 0}) {
        if (!messages) return;
        
        size_t idx = (head + count) % capacity;
        if (count == capacity) {
            head = (head + 1) % capacity;
        } else {
            count++;
        }
        
        uint32_t msg_id = (uint32_t)(total_logged + 1);
        messages[idx] = {
            msg_id,
            type,
            level,
            current_group_depth,
            msg,
            url,
            line,
            col,
            time,
            stack,
            json
        };
        total_logged++;
    }

    // console.log / info / warn / error helper with line, col, source_url, stack_trace
    void log(StringView msg, StringView url, uint32_t line, uint32_t col, uint64_t time) {
        add_message(ConsoleMessageType::LOG, LogLevel::Info, msg, url, line, col, time);
    }

    void warn(StringView msg, StringView url, uint32_t line, uint32_t col, uint64_t time) {
        add_message(ConsoleMessageType::WARN, LogLevel::Warning, msg, url, line, col, time);
    }

    void error(StringView msg, StringView url, uint32_t line, uint32_t col, uint64_t time, StringView stack) {
        add_message(ConsoleMessageType::ERROR, LogLevel::Error, msg, url, line, col, time, stack);
    }

    // console.dir()
    void dir(StringView title, StringView json_data, StringView url, uint32_t line, uint32_t col, uint64_t time) {
        add_message(ConsoleMessageType::DIR, LogLevel::Info, title, url, line, col, time, {"", 0}, json_data);
    }

    // console.table()
    void table(StringView title, StringView json_data, StringView url, uint32_t line, uint32_t col, uint64_t time) {
        add_message(ConsoleMessageType::TABLE, LogLevel::Info, title, url, line, col, time, {"", 0}, json_data);
    }

    // console.memory()
    void memory(StringView title, StringView memory_json, StringView url, uint32_t line, uint32_t col, uint64_t time) {
        add_message(ConsoleMessageType::MEMORY, LogLevel::Info, title, url, line, col, time, {"", 0}, memory_json);
    }

    // console.group() & console.groupEnd()
    void group(StringView title, StringView url, uint32_t line, uint32_t col, uint64_t time) {
        add_message(ConsoleMessageType::GROUP, LogLevel::Info, title, url, line, col, time);
        current_group_depth++;
    }

    void group_end() {
        if (current_group_depth > 0) {
            current_group_depth--;
        }
    }

    // console.time() & console.timeEnd()
    void time_start(StringView label, uint64_t time_us) {
        if (timer_count >= 32) return;
        // Check existing
        for (size_t i = 0; i < timer_count; ++i) {
            bool match = true;
            size_t k = 0;
            while (k < label.length && timers[i].label[k]) {
                if (timers[i].label[k] != label.data[k]) { match = false; break; }
                k++;
            }
            if (match && k == label.length && timers[i].label[k] == '\0') {
                timers[i].start_time_us = time_us;
                return;
            }
        }
        // Add new
        size_t len = label.length < 63 ? label.length : 63;
        for (size_t k = 0; k < len; ++k) timers[timer_count].label[k] = label.data[k];
        timers[timer_count].label[len] = '\0';
        timers[timer_count].start_time_us = time_us;
        timer_count++;
    }

    float time_end(StringView label, uint64_t time_us) {
        for (size_t i = 0; i < timer_count; ++i) {
            bool match = true;
            size_t k = 0;
            while (k < label.length && timers[i].label[k]) {
                if (timers[i].label[k] != label.data[k]) { match = false; break; }
                k++;
            }
            if (match && k == label.length && timers[i].label[k] == '\0') {
                uint64_t diff = time_us > timers[i].start_time_us ? (time_us - timers[i].start_time_us) : 0;
                float duration_ms = (float)diff / 1000.0f;
                // Shift array
                for (size_t j = i; j < timer_count - 1; ++j) {
                    timers[j] = timers[j + 1];
                }
                timer_count--;
                return duration_ms;
            }
        }
        return -1.0f;
    }

    size_t get_total_logged() const { return total_logged; }
    size_t get_active_count() const { return count; }
    const ConsoleMessage* get_message_at(size_t index) const {
        if (index >= count) return nullptr;
        size_t idx = (head + index) % capacity;
        return &messages[idx];
    }

    size_t filter_and_paginate(LogLevel min_level, StringView filter_text, size_t offset, size_t limit, ConsoleMessage* out_array) {
        if (!messages || !out_array || limit == 0) return 0;

        size_t match_count = 0;
        size_t matched_written = 0;

        for (size_t i = 0; i < count; ++i) {
            size_t idx = (head + i) % capacity;
            ConsoleMessage* msg = &messages[idx];

            if ((int)msg->level < (int)min_level) continue;

            if (filter_text.length > 0) {
                bool in_msg = string_contains(msg->message, filter_text);
                bool in_url = string_contains(msg->source_url, filter_text);
                if (!in_msg && !in_url) continue;
            }

            if (match_count >= offset && matched_written < limit) {
                out_array[matched_written++] = *msg;
            }
            match_count++;
        }

        return matched_written;
    }
};
