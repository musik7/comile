#pragma once
#include "core.h"

enum class TracePhase : char { 
    Begin = 'B', 
    End = 'E', 
    Instant = 'I', 
    Complete = 'X', 
    AsyncBegin = 'b', 
    AsyncInstant = 'n', 
    AsyncEnd = 'e',
    Metadata = 'M',
    FlowStart = 's',
    FlowStep = 't',
    FlowEnd = 'f',
    Sample = 'P'
};

struct TraceArg {
    StringView key;
    StringView value;
    TraceArg* next;
};

struct TraceEvent {
    StringView category;
    StringView name;
    TracePhase phase;
    uint64_t timestamp_us;
    uint64_t thread_timestamp_us;
    uint64_t duration_us;         
    uint64_t thread_duration_us;  
    uint32_t process_id;          
    uint32_t thread_id;           
    uint64_t id;                  
    TraceArg* first_arg;
};

struct ProfilerThreadMeta {
    uint32_t pid;
    uint32_t tid;
    StringView thread_name;
    int32_t sort_index;
    ProfilerThreadMeta* next;
};

struct FrameMetric {
    uint64_t timestamp_us;
    double duration_ms;
    double layout_shift;
    bool is_dropped;
    bool is_long_task;
};

struct FrameMetricsSummary {
    double current_fps;
    double avg_fps;
    uint32_t total_frames;
    uint32_t dropped_frames;
    uint32_t long_tasks_count;
    double cumulative_layout_shift;
};

class ProfilerEngine {
private:
    ArenaAllocator* arena;
    TraceEvent* events;
    size_t capacity;
    size_t count;
    
    ProfilerThreadMeta* first_thread_meta;
    uint32_t current_pid;

    // Frame Performance Metrics
    FrameMetric* frame_buffer;
    size_t max_frames;
    size_t frame_count;
    size_t dropped_frames_count;
    size_t long_tasks_count;
    double cumulative_layout_shift;

public:
    ProfilerEngine(ArenaAllocator* alloc, size_t max_events = 250000, size_t max_frames_cap = 10000) 
        : arena(alloc), capacity(max_events), count(0), first_thread_meta(nullptr), current_pid(1),
          max_frames(max_frames_cap), frame_count(0), dropped_frames_count(0), 
          long_tasks_count(0), cumulative_layout_shift(0.0) {
        events = (TraceEvent*)arena->allocate(sizeof(TraceEvent) * capacity);
        frame_buffer = (FrameMetric*)arena->allocate(sizeof(FrameMetric) * max_frames);
    }

    void record_frame(uint64_t timestamp_us, double duration_ms, double layout_shift_score) {
        if (!frame_buffer || frame_count >= max_frames) return;
        FrameMetric* fm = &frame_buffer[frame_count++];
        fm->timestamp_us = timestamp_us;
        fm->duration_ms = duration_ms;
        fm->layout_shift = layout_shift_score;
        fm->is_dropped = (duration_ms > 16.67); // > 16.67ms means dropped frame at 60Hz
        fm->is_long_task = (duration_ms > 50.0);  // > 50ms is a long task

        if (fm->is_dropped) dropped_frames_count++;
        if (fm->is_long_task) long_tasks_count++;
        cumulative_layout_shift += layout_shift_score;
    }

    FrameMetricsSummary get_frame_summary() const {
        FrameMetricsSummary summary;
        summary.total_frames = (uint32_t)frame_count;
        summary.dropped_frames = (uint32_t)dropped_frames_count;
        summary.long_tasks_count = (uint32_t)long_tasks_count;
        summary.cumulative_layout_shift = cumulative_layout_shift;

        if (frame_count == 0) {
            summary.current_fps = 0.0;
            summary.avg_fps = 0.0;
            return summary;
        }

        double total_dur_ms = 0.0;
        size_t sample_size = frame_count > 60 ? 60 : frame_count;
        size_t start_idx = frame_count - sample_size;
        double recent_dur_ms = 0.0;

        for (size_t i = 0; i < frame_count; ++i) {
            total_dur_ms += frame_buffer[i].duration_ms;
            if (i >= start_idx) {
                recent_dur_ms += frame_buffer[i].duration_ms;
            }
        }

        double avg_frame_dur = total_dur_ms / (double)frame_count;
        summary.avg_fps = (avg_frame_dur > 0.0) ? (1000.0 / avg_frame_dur) : 0.0;

        double recent_avg = recent_dur_ms / (double)sample_size;
        summary.current_fps = (recent_avg > 0.0) ? (1000.0 / recent_avg) : 0.0;

        return summary;
    }

    void set_process_id(uint32_t pid) { current_pid = pid; }

    void set_thread_name(uint32_t tid, StringView name, int32_t sort_index) {
        ProfilerThreadMeta* meta = (ProfilerThreadMeta*)arena->allocate(sizeof(ProfilerThreadMeta));
        if (!meta) return;
        meta->pid = current_pid;
        meta->tid = tid;
        meta->thread_name = name;
        meta->sort_index = sort_index;
        meta->next = first_thread_meta;
        first_thread_meta = meta;
    }

    TraceEvent* record_event(StringView cat, StringView name, TracePhase phase, uint64_t time, uint32_t tid, uint64_t id = 0, uint64_t dur = 0) {
        if (!events || count >= capacity) return nullptr;
        TraceEvent* evt = &events[count++];
        
        evt->category = cat;
        evt->name = name;
        evt->phase = phase;
        evt->timestamp_us = time;
        evt->thread_timestamp_us = 0;
        evt->duration_us = dur;
        evt->thread_duration_us = 0;
        evt->process_id = current_pid;
        evt->thread_id = tid;
        evt->id = id;
        evt->first_arg = nullptr;
        
        return evt;
    }
    
    void add_event_arg(TraceEvent* evt, StringView key, StringView value) {
        if (!evt) return;
        TraceArg* arg = (TraceArg*)arena->allocate(sizeof(TraceArg));
        if (!arg) return;
        arg->key = key;
        arg->value = value;
        arg->next = evt->first_arg;
        evt->first_arg = arg;
    }

    size_t export_trace_json(char* buffer, size_t buffer_size) {
        if (!buffer || buffer_size == 0) return 0;
        
        size_t len = 0;
        auto append = [&](const char* str) {
            while (*str && len < buffer_size - 1) buffer[len++] = *str++;
        };
        auto append_c = [&](char c) {
            if (len < buffer_size - 1) buffer[len++] = c;
        };
        auto append_sv = [&](StringView sv) {
            for (size_t i = 0; i < sv.length && len < buffer_size - 1; i++) {
                char c = sv.data[i];
                if (c == '"' || c == '\\') { append_c('\\'); append_c(c); }
                else if (c == '\n') { append_c('\\'); append_c('n'); }
                else if (c == '\r') { append_c('\\'); append_c('r'); }
                else if (c == '\t') { append_c('\\'); append_c('t'); }
                else append_c(c);
            }
        };
        auto append_num = [&](uint64_t num) {
            char buf[32]; int idx = 31; buf[idx] = '\0';
            if (num == 0) buf[--idx] = '0';
            else {
                while (num > 0 && idx > 0) {
                    buf[--idx] = '0' + (num % 10);
                    num /= 10;
                }
            }
            append(&buf[idx]);
        };
        auto append_hex = [&](uint64_t num) {
            char buf[32]; int idx = 31; buf[idx] = '\0';
            if (num == 0) buf[--idx] = '0';
            else {
                while (num > 0 && idx > 0) {
                    int rem = num & 0xF;
                    buf[--idx] = rem < 10 ? ('0' + rem) : ('a' + rem - 10);
                    num >>= 4;
                }
            }
            append(&buf[idx]);
        };

        append("{\"traceEvents\":[");
        bool first = true;
        
        // Metadata events
        ProfilerThreadMeta* meta = first_thread_meta;
        while (meta) {
            if (!first) append(",");
            first = false;
            append("{\"ph\":\"M\",\"pid\":"); append_num(meta->pid);
            append(",\"tid\":"); append_num(meta->tid);
            append(",\"name\":\"thread_name\",\"args\":{\"name\":\"");
            append_sv(meta->thread_name);
            append("\"}}");

            append(",{\"ph\":\"M\",\"pid\":"); append_num(meta->pid);
            append(",\"tid\":"); append_num(meta->tid);
            append(",\"name\":\"thread_sort_index\",\"args\":{\"sort_index\":");
            
            // Handle signed int for sort_index
            if (meta->sort_index < 0) {
                append_c('-');
                append_num(-meta->sort_index);
            } else {
                append_num(meta->sort_index);
            }
            
            append("}}");
            meta = meta->next;
        }

        // Trace events
        for (size_t i = 0; i < count; i++) {
            TraceEvent* evt = &events[i];
            if (!first) append(",");
            first = false;
            
            append("{\"pid\":"); append_num(evt->process_id);
            append(",\"tid\":"); append_num(evt->thread_id);
            append(",\"ts\":"); append_num(evt->timestamp_us);
            append(",\"ph\":\""); append_c((char)evt->phase); append_c('"');
            
            if (evt->category.length > 0) {
                append(",\"cat\":\""); append_sv(evt->category); append_c('"');
            }
            if (evt->name.length > 0) {
                append(",\"name\":\""); append_sv(evt->name); append_c('"');
            }
            if (evt->phase == TracePhase::Complete) {
                append(",\"dur\":"); append_num(evt->duration_us);
            }
            if (evt->id > 0) {
                append(",\"id\":\"0x"); append_hex(evt->id); append_c('"');
            }
            
            if (evt->first_arg) {
                append(",\"args\":{");
                TraceArg* arg = evt->first_arg;
                bool first_arg = true;
                while (arg) {
                    if (!first_arg) append(",");
                    first_arg = false;
                    append_c('"'); append_sv(arg->key); append("\":\"");
                    append_sv(arg->value); append_c('"');
                    arg = arg->next;
                }
                append_c('}');
            }
            
            append_c('}');
        }
        
        append("]}");
        buffer[len] = '\0';
        return len;
    }
};
