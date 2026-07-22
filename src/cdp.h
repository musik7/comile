#pragma once
#include "core.h"
#include "elements.h"
#include "network.h"
#include "console.h"
#include "profiler.h"
#include "memory.h"
#include "storage.h"
#include "security.h"
#include "service_worker.h"

struct CDPCommand {
    uint32_t id;
    StringView method;
    StringView params;
};

class CDPParser {
public:
    static bool parse(StringView json, CDPCommand& cmd) {
        cmd.id = 0;
        cmd.method = {nullptr, 0};
        cmd.params = {nullptr, 0};

        if (json.length == 0 || json.data[0] != '{') return false;

        size_t i = 1;
        while (i < json.length) {
            // Skip whitespace & delimiters
            while (i < json.length && (json.data[i] == ' ' || json.data[i] == '\t' || json.data[i] == '\r' || json.data[i] == '\n' || json.data[i] == ',')) i++;
            if (i >= json.length || json.data[i] == '}') break;

            if (json.data[i] == '"') {
                i++;
                size_t key_start = i;
                while (i < json.length && json.data[i] != '"') i++;
                size_t key_len = i - key_start;
                if (i < json.length) i++; // skip closing quote

                StringView key = { json.data + key_start, key_len };

                // Skip colon
                while (i < json.length && (json.data[i] == ' ' || json.data[i] == ':')) i++;

                // Read value
                if (key.equals("id")) {
                    uint32_t num = 0;
                    while (i < json.length && json.data[i] >= '0' && json.data[i] <= '9') {
                        num = num * 10 + (json.data[i] - '0');
                        i++;
                    }
                    cmd.id = num;
                } else if (key.equals("method")) {
                    if (i < json.length && json.data[i] == '"') {
                        i++;
                        size_t val_start = i;
                        while (i < json.length && json.data[i] != '"') i++;
                        cmd.method = { json.data + val_start, i - val_start };
                        if (i < json.length) i++;
                    }
                } else if (key.equals("params")) {
                    size_t params_start = i;
                    if (i < json.length && json.data[i] == '{') {
                        int depth = 1;
                        i++;
                        while (i < json.length && depth > 0) {
                            if (json.data[i] == '{') depth++;
                            else if (json.data[i] == '}') depth--;
                            i++;
                        }
                        cmd.params = { json.data + params_start, i - params_start };
                    }
                } else {
                    // Skip value
                    if (i < json.length && json.data[i] == '"') {
                        i++;
                        while (i < json.length && json.data[i] != '"') i++;
                        if (i < json.length) i++;
                    } else {
                        while (i < json.length && json.data[i] != ',' && json.data[i] != '}') i++;
                    }
                }
            } else {
                i++;
            }
        }

        return cmd.id > 0 && cmd.method.length > 0;
    }
};

class CDPDispatcher {
private:
    ElementsEngine* elements;
    NetworkEngine* network;
    ConsoleEngine* console;
    ProfilerEngine* profiler;
    MemoryEngine* memory;

public:
    CDPDispatcher(ElementsEngine* el, NetworkEngine* net, ConsoleEngine* con, ProfilerEngine* prof, MemoryEngine* mem)
        : elements(el), network(net), console(con), profiler(prof), memory(mem) {}

    size_t dispatch(StringView raw_json_command, char* out_buf, size_t max_buf_len) {
        if (!out_buf || max_buf_len == 0) return 0;

        CDPCommand cmd;
        if (!CDPParser::parse(raw_json_command, cmd)) {
            // Error response
            const char* err = "{\"id\":0,\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}";
            size_t l = 0;
            while (err[l] && l < max_buf_len - 1) { out_buf[l] = err[l]; l++; }
            out_buf[l] = '\0';
            return l;
        }

        size_t len = 0;
        auto append = [&](const char* str) {
            while (*str && len < max_buf_len - 1) out_buf[len++] = *str++;
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

        append("{\"id\":");
        append_num(cmd.id);
        append(",\"result\":");

        if (cmd.method.equals("Page.enable") || cmd.method.equals("Network.enable") || 
            cmd.method.equals("Console.enable") || cmd.method.equals("DOM.enable") ||
            cmd.method.equals("Profiler.enable")) {
            append("{}");
        } else if (cmd.method.equals("Console.clearMessages")) {
            if (console) console->clear();
            append("{}");
        } else if (cmd.method.equals("DOM.getDocument")) {
            append("{\"root\":{\"nodeId\":1,\"backendNodeId\":1,\"nodeType\":9,\"nodeName\":\"#document\"}}");
        } else if (cmd.method.equals("Memory.getHeapSnapshot")) {
            append("{\"nodeCount\":");
            append_num(memory ? memory->get_node_count() : 0);
            append(",\"edgeCount\":");
            append_num(memory ? memory->get_edge_count() : 0);
            append("}");
        } else if (cmd.method.equals("Network.getResponseBody")) {
            append("{\"body\":\"\",\"base64Encoded\":false}");
        } else {
            // Fallback success for custom commands
            append("{\"status\":\"ok\"}");
        }

        append("}");
        out_buf[len] = '\0';
        return len;
    }
};
