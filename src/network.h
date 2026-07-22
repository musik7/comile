#pragma once
#include "core.h"

enum class ResourceType {
    Document, Stylesheet, Image, Media, Font, Script, TextTrack, 
    XHR, Fetch, EventSource, WebSocket, Manifest, SignedExchange, 
    Ping, CSPViolationReport, Preflight, Other
};

enum class RequestState { Pending, Finished, Failed, Blocked };

struct HTTPHeader {
    StringView name;
    StringView value;
    HTTPHeader* next;
};

struct ResourceTiming {
    int64_t request_time_us; 
    int64_t proxy_start_us;
    int64_t proxy_end_us;
    int64_t dns_start_us;
    int64_t dns_end_us;
    int64_t connect_start_us;
    int64_t connect_end_us;
    int64_t ssl_start_us;
    int64_t ssl_end_us;
    int64_t worker_start_us;
    int64_t worker_ready_us;
    int64_t send_start_us;
    int64_t send_end_us;
    int64_t receive_headers_end_us;
};

struct DataChunk {
    size_t length;
    uint64_t timestamp_us;
    DataChunk* next;
};

struct NetworkRequest {
    uint32_t request_id;
    StringView url;
    StringView method;
    StringView protocol;
    StringView mime_type;
    
    ResourceType resource_type;
    RequestState state;
    
    uint64_t start_time_us;
    uint64_t end_time_us;
    
    uint32_t status_code;
    StringView status_text;
    
    size_t transfer_size;
    size_t encoded_body_size;
    size_t decoded_body_size;
    
    HTTPHeader* first_request_header;
    HTTPHeader* first_response_header;
    DataChunk* first_chunk;
    DataChunk* last_chunk;
    size_t total_chunks;
    
    ResourceTiming timing;
    StringView error_text;
    
    NetworkRequest* next_in_map;
};

struct WebSocketFrame {
    uint32_t frame_id;
    uint32_t request_id;
    uint8_t opcode; // 0x1 = text, 0x2 = binary, 0x8 = close, 0x9 = ping, 0xA = pong
    bool is_outgoing;
    StringView payload;
    size_t payload_size;
    uint64_t timestamp_us;
    WebSocketFrame* next;
};

struct SSEEvent {
    uint32_t event_id_num;
    uint32_t request_id;
    StringView event_type;
    StringView data;
    StringView sse_id;
    uint64_t timestamp_us;
    SSEEvent* next;
};

class NetworkEngine {
private:
    ArenaAllocator* arena;
    NetworkRequest** request_map;
    size_t map_capacity;
    
    NetworkRequest* requests;
    size_t capacity;
    size_t count;

    // WebSocket & SSE streams
    WebSocketFrame* first_ws_frame;
    WebSocketFrame* last_ws_frame;
    size_t ws_frame_count;
    uint32_t next_ws_frame_id;

    SSEEvent* first_sse_event;
    SSEEvent* last_sse_event;
    size_t sse_event_count;
    uint32_t next_sse_event_id;

public:
    NetworkEngine(ArenaAllocator* alloc, size_t max_req = 5000)
        : arena(alloc), capacity(max_req), count(0), map_capacity(8192),
          first_ws_frame(nullptr), last_ws_frame(nullptr), ws_frame_count(0), next_ws_frame_id(1),
          first_sse_event(nullptr), last_sse_event(nullptr), sse_event_count(0), next_sse_event_id(1) {
        requests = (NetworkRequest*)arena->allocate(sizeof(NetworkRequest) * capacity);
        request_map = (NetworkRequest**)arena->allocate(sizeof(NetworkRequest*) * map_capacity);
        
        for (size_t i = 0; i < map_capacity; i++) {
            request_map[i] = nullptr;
        }
    }

    NetworkRequest* get_request(uint32_t req_id) {
        if (!request_map) return nullptr;
        size_t idx = req_id % map_capacity;
        NetworkRequest* req = request_map[idx];
        while (req) {
            if (req->request_id == req_id) return req;
            req = req->next_in_map;
        }
        return nullptr;
    }

    NetworkRequest* begin_request(uint32_t req_id, StringView url, StringView method, ResourceType type, uint64_t time) {
        if (!requests || count >= capacity) return nullptr;
        NetworkRequest* req = &requests[count++];
        
        req->request_id = req_id;
        req->url = url;
        req->method = method;
        req->resource_type = type;
        req->state = RequestState::Pending;
        req->start_time_us = time;
        req->end_time_us = 0;
        req->status_code = 0;
        req->transfer_size = 0;
        req->encoded_body_size = 0;
        req->decoded_body_size = 0;
        req->first_request_header = nullptr;
        req->first_response_header = nullptr;
        req->first_chunk = nullptr;
        req->last_chunk = nullptr;
        req->total_chunks = 0;
        req->protocol = {nullptr, 0};
        req->mime_type = {nullptr, 0};
        req->status_text = {nullptr, 0};
        req->error_text = {nullptr, 0};
        
        char* t_ptr = (char*)&req->timing;
        for (size_t i = 0; i < sizeof(ResourceTiming); ++i) t_ptr[i] = 0;
        
        size_t idx = req_id % map_capacity;
        req->next_in_map = request_map[idx];
        request_map[idx] = req;
        
        return req;
    }

    void add_request_header(uint32_t req_id, StringView name, StringView value) {
        NetworkRequest* req = get_request(req_id);
        if (!req) return;
        
        HTTPHeader* hdr = (HTTPHeader*)arena->allocate(sizeof(HTTPHeader));
        if (!hdr) return;
        
        hdr->name = name;
        hdr->value = value;
        hdr->next = req->first_request_header;
        req->first_request_header = hdr;
    }

    void add_response_header(uint32_t req_id, StringView name, StringView value) {
        NetworkRequest* req = get_request(req_id);
        if (!req) return;
        
        HTTPHeader* hdr = (HTTPHeader*)arena->allocate(sizeof(HTTPHeader));
        if (!hdr) return;
        
        hdr->name = name;
        hdr->value = value;
        hdr->next = req->first_response_header;
        req->first_response_header = hdr;
    }

    void finish_request(uint32_t req_id, uint32_t status_code, size_t transfer, size_t decoded, uint64_t end_time) {
        NetworkRequest* req = get_request(req_id);
        if (req) {
            req->status_code = status_code;
            req->transfer_size = transfer;
            req->encoded_body_size = transfer;
            req->decoded_body_size = decoded;
            req->end_time_us = end_time;
            req->state = RequestState::Finished;
        }
    }
    
    void fail_request(uint32_t req_id, StringView error, uint64_t end_time) {
        NetworkRequest* req = get_request(req_id);
        if (req) {
            req->error_text = error;
            req->end_time_us = end_time;
            req->state = RequestState::Failed;
        }
    }

    void add_data_chunk(uint32_t req_id, size_t length, uint64_t timestamp_us) {
        NetworkRequest* req = get_request(req_id);
        if (!req) return;
        
        DataChunk* chunk = (DataChunk*)arena->allocate(sizeof(DataChunk));
        if (!chunk) return;
        
        chunk->length = length;
        chunk->timestamp_us = timestamp_us;
        chunk->next = nullptr;
        
        if (!req->first_chunk) {
            req->first_chunk = chunk;
        } else {
            req->last_chunk->next = chunk;
        }
        req->last_chunk = chunk;
        req->total_chunks++;
        req->transfer_size += length; // keep running total
    }

    // A simple HTTP header parser that handles \r\n delimited headers
    // e.g., "Content-Type: text/html\r\nContent-Length: 123\r\n\r\n"
    void parse_raw_response_headers(uint32_t req_id, StringView raw_headers) {
        size_t i = 0;
        
        // Skip potential HTTP status line if present (e.g., "HTTP/1.1 200 OK")
        if (i < raw_headers.length && raw_headers.data[i] == 'H' && 
            i+4 < raw_headers.length && raw_headers.data[i+1] == 'T' && 
            raw_headers.data[i+2] == 'T' && raw_headers.data[i+3] == 'P') {
            
            while (i < raw_headers.length && raw_headers.data[i] != '\n') {
                i++;
            }
            if (i < raw_headers.length && raw_headers.data[i] == '\n') i++;
        }

        while (i < raw_headers.length) {
            // Find end of line
            size_t line_start = i;
            size_t line_end = i;
            while (i < raw_headers.length && raw_headers.data[i] != '\n') {
                line_end = i;
                i++;
            }
            if (i < raw_headers.length && raw_headers.data[i] == '\n') i++;
            
            // Adjust for \r
            if (line_end >= line_start && raw_headers.data[line_end] == '\r') {
                if (line_end == line_start) break; // empty line \r\n marks end of headers
                line_end--;
            } else if (line_end == line_start && (raw_headers.data[line_end] == '\n' || raw_headers.data[line_end] == '\r')) {
                break; // empty line marks end of headers
            }

            size_t line_len = line_end - line_start + 1;
            if (line_len == 0) break;
            
            // Look for colon
            size_t colon_idx = line_start;
            while (colon_idx <= line_end && raw_headers.data[colon_idx] != ':') {
                colon_idx++;
            }
            
            if (colon_idx <= line_end) {
                size_t name_len = colon_idx - line_start;
                
                size_t val_start = colon_idx + 1;
                while (val_start <= line_end && (raw_headers.data[val_start] == ' ' || raw_headers.data[val_start] == '\t')) {
                    val_start++;
                }
                
                size_t val_len = line_end - val_start + 1;
                if (val_start > line_end) val_len = 0;
                
                StringView name = { raw_headers.data + line_start, name_len };
                StringView value = { raw_headers.data + val_start, val_len };
                
                add_response_header(req_id, name, value);
            }
        }
    }

    size_t get_total_requests() const { return count; }

    size_t get_requests_paginated(size_t offset, size_t limit, NetworkRequest** out_array) {
        if (!out_array || limit == 0) return 0;
        size_t written = 0;
        for (size_t i = offset; i < count && written < limit; ++i) {
            out_array[written++] = &requests[i];
        }
        return written;
    }

    double calculate_throughput_bytes_per_sec(uint32_t req_id) {
        NetworkRequest* req = get_request(req_id);
        if (!req || req->start_time_us == 0) return 0.0;

        uint64_t end = req->end_time_us > 0 ? req->end_time_us : req->start_time_us;
        if (req->last_chunk && req->last_chunk->timestamp_us > end) {
            end = req->last_chunk->timestamp_us;
        }

        uint64_t duration_us = end - req->start_time_us;
        if (duration_us == 0) return 0.0;

        double seconds = (double)duration_us / 1000000.0;
        return (double)req->transfer_size / seconds;
    }

    void record_ws_frame(uint32_t req_id, uint8_t opcode, bool is_outgoing, StringView payload, uint64_t timestamp_us) {
        WebSocketFrame* frame = (WebSocketFrame*)arena->allocate(sizeof(WebSocketFrame));
        if (!frame) return;
        frame->frame_id = next_ws_frame_id++;
        frame->request_id = req_id;
        frame->opcode = opcode;
        frame->is_outgoing = is_outgoing;
        frame->payload = payload;
        frame->payload_size = payload.length;
        frame->timestamp_us = timestamp_us;
        frame->next = nullptr;

        if (!first_ws_frame) {
            first_ws_frame = frame;
        } else {
            last_ws_frame->next = frame;
        }
        last_ws_frame = frame;
        ws_frame_count++;
    }

    void record_sse_event(uint32_t req_id, StringView event_type, StringView data, StringView sse_id, uint64_t timestamp_us) {
        SSEEvent* evt = (SSEEvent*)arena->allocate(sizeof(SSEEvent));
        if (!evt) return;
        evt->event_id_num = next_sse_event_id++;
        evt->request_id = req_id;
        evt->event_type = event_type;
        evt->data = data;
        evt->sse_id = sse_id;
        evt->timestamp_us = timestamp_us;
        evt->next = nullptr;

        if (!first_sse_event) {
            first_sse_event = evt;
        } else {
            last_sse_event->next = evt;
        }
        last_sse_event = evt;
        sse_event_count++;
    }

    size_t get_ws_frame_count() const { return ws_frame_count; }
    size_t get_sse_event_count() const { return sse_event_count; }
};
