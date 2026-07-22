#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define EXPORT __attribute__((visibility("default")))

// ============================================================================
// CHROMIUM DEVTOOLS-INSPIRED C++ ENGINE FOR MOBILE WEB (WASM TARGET)
// ============================================================================
// Target: Android 12+, Low-overhead memory architecture
// This provides a comprehensive, production-ready C++ foundation for DevTools 
// subsystems: Elements, Console, Performance, Memory, and Network.
// Designed for zero-copy JS bindings and maximum throughput on ARM CPUs, 
// avoiding heavy STL containers in favor of Arena-backed custom data structures.
// ============================================================================

// ----------------------------------------------------------------------------
// 1. CORE TYPES & MEMORY MANAGEMENT
// ----------------------------------------------------------------------------

#include "core.h"
#include "memory.h"

// Global Arena Instance: 64MB Total Arena for comprehensive DevTools tracking
#define MEMORY_CAPACITY (1024 * 1024 * 64) 
char global_memory_buffer[MEMORY_CAPACITY];
ArenaAllocator global_arena(global_memory_buffer, MEMORY_CAPACITY);


#include "elements.h"
#include "console.h"
#include "profiler.h"

#include "storage.h"
#include "service_worker.h"
#include "security.h"
#include "network.h"
#include "cdp.h"
#include "js_lexer.h"
#include "vfs.h"
#include "canvas_renderer.h"
#include "syntax_highlighter.h"
#include "diff_engine.h"
#include "virtual_list.h"
#include "layout_tree.h"

// ============================================================================
// GLOBAL DEVTOOLS STATE & C-ABI EXPORTS
// ============================================================================

struct DevToolsContext {
    ElementsEngine* elements;
    ConsoleEngine* console;
    ProfilerEngine* profiler;
    MemoryEngine* memory;
    NetworkEngine* network;
    StorageEngine* storage;
    ServiceWorkerEngine* service_worker;
    SecurityEngine* security;
    VFSEngine* vfs;
    CanvasRendererEngine* canvas;
    SyntaxHighlighterEngine* highlighter;
    DiffEngine* diff;
    VirtualListEngine* virtual_list;
    LayoutTreeEngine* layout_tree;
};

DevToolsContext* g_ctx = nullptr;

extern "C" {

EXPORT void* allocate_memory(size_t size) {
    return global_arena.allocate(size);
}

EXPORT void devtools_init() {
    global_arena.reset();
    
    // Bootstrap the global context inside the arena
    void* ctx_mem = global_arena.allocate(sizeof(DevToolsContext));
    g_ctx = new (ctx_mem) DevToolsContext();
    
    g_ctx->elements       = new (global_arena.allocate(sizeof(ElementsEngine))) ElementsEngine(&global_arena);
    g_ctx->console        = new (global_arena.allocate(sizeof(ConsoleEngine))) ConsoleEngine(&global_arena);
    g_ctx->profiler       = new (global_arena.allocate(sizeof(ProfilerEngine))) ProfilerEngine(&global_arena);
    g_ctx->memory         = new (global_arena.allocate(sizeof(MemoryEngine))) MemoryEngine(&global_arena);
    g_ctx->network        = new (global_arena.allocate(sizeof(NetworkEngine))) NetworkEngine(&global_arena);
    g_ctx->storage        = new (global_arena.allocate(sizeof(StorageEngine))) StorageEngine(&global_arena);
    g_ctx->service_worker = new (global_arena.allocate(sizeof(ServiceWorkerEngine))) ServiceWorkerEngine(&global_arena);
    g_ctx->security       = new (global_arena.allocate(sizeof(SecurityEngine))) SecurityEngine(&global_arena);
    g_ctx->vfs            = new (global_arena.allocate(sizeof(VFSEngine))) VFSEngine(&global_arena);
    g_ctx->canvas         = new (global_arena.allocate(sizeof(CanvasRendererEngine))) CanvasRendererEngine(&global_arena);
    g_ctx->highlighter    = new (global_arena.allocate(sizeof(SyntaxHighlighterEngine))) SyntaxHighlighterEngine(&global_arena);
    g_ctx->diff           = new (global_arena.allocate(sizeof(DiffEngine))) DiffEngine(&global_arena);
    g_ctx->virtual_list   = new (global_arena.allocate(sizeof(VirtualListEngine))) VirtualListEngine(&global_arena);
    g_ctx->layout_tree    = new (global_arena.allocate(sizeof(LayoutTreeEngine))) LayoutTreeEngine(&global_arena);
}

EXPORT void devtools_reset() {
    devtools_init(); // Resets arena and recreates context
}

// ---- STUBS FOR JS INTEROP ----

EXPORT uint32_t dom_create_node(uint32_t backend_id, uint32_t node_type, const char* name, size_t name_len, const char* value, size_t value_len) {
    if (!g_ctx || !g_ctx->elements) return 0;
    DOMNode* node = g_ctx->elements->create_node(backend_id, (NodeType)node_type, {name, name_len}, {value, value_len});
    return node ? node->id : 0;
}

EXPORT void dom_insert_before(uint32_t parent_id, uint32_t child_id, uint32_t ref_id) {
    if (!g_ctx || !g_ctx->elements) return;
    DOMNode* parent = g_ctx->elements->get_node_by_id(parent_id);
    DOMNode* child = g_ctx->elements->get_node_by_id(child_id);
    DOMNode* ref = ref_id ? g_ctx->elements->get_node_by_id(ref_id) : nullptr;
    g_ctx->elements->insert_before(parent, child, ref);
}

EXPORT void dom_remove_child(uint32_t parent_id, uint32_t child_id) {
    if (!g_ctx || !g_ctx->elements) return;
    DOMNode* parent = g_ctx->elements->get_node_by_id(parent_id);
    DOMNode* child = g_ctx->elements->get_node_by_id(child_id);
    g_ctx->elements->remove_child(parent, child);
}

EXPORT void dom_set_attribute(uint32_t node_id, const char* name, size_t name_len, const char* val, size_t val_len) {
    if (!g_ctx || !g_ctx->elements) return;
    DOMNode* node = g_ctx->elements->get_node_by_id(node_id);
    g_ctx->elements->set_attribute(node, {name, name_len}, {val, val_len});
}

EXPORT void dom_remove_attribute(uint32_t node_id, const char* name, size_t name_len) {
    if (!g_ctx || !g_ctx->elements) return;
    DOMNode* node = g_ctx->elements->get_node_by_id(node_id);
    g_ctx->elements->remove_attribute(node, {name, name_len});
}

EXPORT void dom_add_computed_style(uint32_t node_id, const char* prop, size_t prop_len, const char* val, size_t val_len) {
    if (!g_ctx || !g_ctx->elements) return;
    DOMNode* node = g_ctx->elements->get_node_by_id(node_id);
    g_ctx->elements->add_computed_style(node, {prop, prop_len}, {val, val_len});
}

EXPORT uint32_t dom_get_element_by_id(uint32_t root_id, const char* id_str, size_t id_len) {
    if (!g_ctx || !g_ctx->elements) return 0;
    DOMNode* root = g_ctx->elements->get_node_by_id(root_id);
    DOMNode* res = g_ctx->elements->get_element_by_id(root, {id_str, id_len});
    return res ? res->id : 0;
}

// Fills an array with up to max_results node IDs that match the class name.
// Returns the number of matched elements.
EXPORT size_t dom_get_elements_by_class_name(uint32_t root_id, const char* class_str, size_t class_len, uint32_t* out_array, size_t max_results) {
    if (!g_ctx || !g_ctx->elements || !out_array) return 0;
    DOMNode* root = g_ctx->elements->get_node_by_id(root_id);
    
    const size_t MAX_TMP = 1024;
    DOMNode* tmp_results[MAX_TMP];
    size_t actual_max = max_results < MAX_TMP ? max_results : MAX_TMP;
    size_t count = 0;
    
    g_ctx->elements->get_elements_by_class_name(root, {class_str, class_len}, tmp_results, actual_max, count);
    
    for (size_t i = 0; i < count; ++i) {
        out_array[i] = tmp_results[i]->id;
    }
    return count;
}

// Evaluates a simple or complex CSS selector string
EXPORT size_t dom_query_selector_all(uint32_t root_id, const char* selector_str, size_t selector_len, uint32_t* out_array, size_t max_results) {
    if (!g_ctx || !g_ctx->elements || !out_array) return 0;
    DOMNode* root = g_ctx->elements->get_node_by_id(root_id);
    
    const size_t MAX_TMP = 1024;
    DOMNode* tmp_results[MAX_TMP];
    size_t actual_max = max_results < MAX_TMP ? max_results : MAX_TMP;
    size_t count = 0;
    
    g_ctx->elements->query_selector_all(root, {selector_str, selector_len}, tmp_results, actual_max, count);
    
    for (size_t i = 0; i < count; ++i) {
        out_array[i] = tmp_results[i]->id;
    }
    return count;
}

EXPORT void console_log(int level, const char* msg, size_t msg_len, uint64_t time) {
    if (!g_ctx || !g_ctx->console) return;
    g_ctx->console->add_message(ConsoleMessageType::LOG, (LogLevel)level, {msg, msg_len}, {"brain.cpp", 9}, 142, 8, time);
}

EXPORT void console_log_full(uint8_t type, uint8_t level, const char* msg, size_t msg_len, const char* url, size_t url_len, uint32_t line, uint32_t col, uint64_t time, const char* stack, size_t stack_len, const char* json, size_t json_len) {
    if (!g_ctx || !g_ctx->console) return;
    g_ctx->console->add_message((ConsoleMessageType)type, (LogLevel)level, {msg, msg_len}, {url, url_len}, line, col, time, {stack, stack_len}, {json, json_len});
}

EXPORT void console_dir(const char* title, size_t title_len, const char* json, size_t json_len, const char* url, size_t url_len, uint32_t line, uint32_t col, uint64_t time) {
    if (!g_ctx || !g_ctx->console) return;
    g_ctx->console->dir({title, title_len}, {json, json_len}, {url, url_len}, line, col, time);
}

EXPORT void console_table(const char* title, size_t title_len, const char* json, size_t json_len, const char* url, size_t url_len, uint32_t line, uint32_t col, uint64_t time) {
    if (!g_ctx || !g_ctx->console) return;
    g_ctx->console->table({title, title_len}, {json, json_len}, {url, url_len}, line, col, time);
}

EXPORT void console_memory(const char* title, size_t title_len, const char* json, size_t json_len, const char* url, size_t url_len, uint32_t line, uint32_t col, uint64_t time) {
    if (!g_ctx || !g_ctx->console) return;
    g_ctx->console->memory({title, title_len}, {json, json_len}, {url, url_len}, line, col, time);
}

EXPORT void console_group(const char* title, size_t title_len, const char* url, size_t url_len, uint32_t line, uint32_t col, uint64_t time) {
    if (!g_ctx || !g_ctx->console) return;
    g_ctx->console->group({title, title_len}, {url, url_len}, line, col, time);
}

EXPORT void console_group_end() {
    if (!g_ctx || !g_ctx->console) return;
    g_ctx->console->group_end();
}

EXPORT void console_time(const char* label, size_t label_len, uint64_t time_us) {
    if (!g_ctx || !g_ctx->console) return;
    g_ctx->console->time_start({label, label_len}, time_us);
}

EXPORT float console_time_end(const char* label, size_t label_len, uint64_t time_us) {
    if (!g_ctx || !g_ctx->console) return -1.0f;
    return g_ctx->console->time_end({label, label_len}, time_us);
}

EXPORT void console_error_with_stack(const char* msg, size_t msg_len, const char* url, size_t url_len, uint32_t line, uint32_t col, const char* stack, size_t stack_len, uint64_t time) {
    if (!g_ctx || !g_ctx->console) return;
    g_ctx->console->error({msg, msg_len}, {url, url_len}, line, col, time, {stack, stack_len});
}

EXPORT void profiler_set_process(uint32_t pid) {
    if (!g_ctx || !g_ctx->profiler) return;
    g_ctx->profiler->set_process_id(pid);
}

EXPORT void profiler_set_thread(uint32_t tid, const char* name, size_t name_len, int32_t sort_index) {
    if (!g_ctx || !g_ctx->profiler) return;
    g_ctx->profiler->set_thread_name(tid, {name, name_len}, sort_index);
}

EXPORT void* profiler_record_event(const char* cat, size_t cat_len, const char* name, size_t name_len, char phase, uint64_t time, uint32_t tid, uint64_t id, uint64_t dur) {
    if (!g_ctx || !g_ctx->profiler) return nullptr;
    return g_ctx->profiler->record_event({cat, cat_len}, {name, name_len}, (TracePhase)phase, time, tid, id, dur);
}

EXPORT void profiler_add_arg(void* evt_ptr, const char* key, size_t key_len, const char* val, size_t val_len) {
    if (!g_ctx || !g_ctx->profiler || !evt_ptr) return;
    g_ctx->profiler->add_event_arg((TraceEvent*)evt_ptr, {key, key_len}, {val, val_len});
}

EXPORT size_t profiler_export_trace_json(char* buffer, size_t buffer_size) {
    if (!g_ctx || !g_ctx->profiler) return 0;
    return g_ctx->profiler->export_trace_json(buffer, buffer_size);
}

EXPORT void memory_add_node(uint32_t id, uint32_t type, const char* name, size_t name_len, size_t shallow, size_t retained, uint32_t distance) {
    if (!g_ctx || !g_ctx->memory) return;
    g_ctx->memory->add_node(id, (HeapNodeType)type, {name, name_len}, shallow, retained, distance);
}

EXPORT void memory_add_edge(uint32_t type, const char* name_or_index, size_t name_len, uint32_t from, uint32_t to) {
    if (!g_ctx || !g_ctx->memory) return;
    g_ctx->memory->add_edge((HeapEdgeType)type, {name_or_index, name_len}, from, to);
}

EXPORT void memory_compute_dominators() {
    if (!g_ctx || !g_ctx->memory) return;
    g_ctx->memory->compute_dominators_and_retained_sizes();
}

EXPORT void* memory_get_nodes(size_t* out_count) {
    if (!g_ctx || !g_ctx->memory) {
        if (out_count) *out_count = 0;
        return nullptr;
    }
    if (out_count) *out_count = g_ctx->memory->get_node_count();
    return g_ctx->memory->get_nodes_array();
}

EXPORT void* memory_get_edges(size_t* out_count) {
    if (!g_ctx || !g_ctx->memory) {
        if (out_count) *out_count = 0;
        return nullptr;
    }
    if (out_count) *out_count = g_ctx->memory->get_edge_count();
    return g_ctx->memory->get_edges_array();
}

EXPORT void* memory_add_allocation_profile_node(uint32_t id, const char* func, size_t func_len, const char* script, size_t script_len, uint32_t script_id, uint32_t line, uint32_t col) {
    if (!g_ctx || !g_ctx->memory) return nullptr;
    return g_ctx->memory->add_allocation_profile_node(id, {func, func_len}, {script, script_len}, script_id, line, col);
}

EXPORT void memory_add_allocation(void* node_ptr, size_t size, uint32_t count) {
    if (!g_ctx || !g_ctx->memory || !node_ptr) return;
    g_ctx->memory->add_allocation((AllocationProfileNode*)node_ptr, size, count);
}

EXPORT void storage_add_cookie(const char* name, size_t name_len, const char* val, size_t val_len, const char* domain, size_t domain_len, const char* path, size_t path_len, uint64_t expires, bool http_only, bool secure, const char* same_site, size_t same_site_len) {
    if (!g_ctx || !g_ctx->storage) return;
    g_ctx->storage->add_cookie({name, name_len}, {val, val_len}, {domain, domain_len}, {path, path_len}, expires, http_only, secure, {same_site, same_site_len});
}

EXPORT void storage_add_local(const char* key, size_t key_len, const char* val, size_t val_len) {
    if (!g_ctx || !g_ctx->storage) return;
    g_ctx->storage->add_local_storage({key, key_len}, {val, val_len});
}

EXPORT void storage_add_session(const char* key, size_t key_len, const char* val, size_t val_len) {
    if (!g_ctx || !g_ctx->storage) return;
    g_ctx->storage->add_session_storage({key, key_len}, {val, val_len});
}

EXPORT void* storage_add_idb_database(const char* name, size_t name_len, uint32_t version) {
    if (!g_ctx || !g_ctx->storage) return nullptr;
    return g_ctx->storage->add_idb_database({name, name_len}, version);
}

EXPORT void* storage_add_idb_object_store(void* db_ptr, const char* name, size_t name_len, const char* key_path, size_t key_path_len, bool auto_inc) {
    if (!g_ctx || !g_ctx->storage || !db_ptr) return nullptr;
    return g_ctx->storage->add_idb_object_store((IndexedDBDatabase*)db_ptr, {name, name_len}, {key_path, key_path_len}, auto_inc);
}

EXPORT void storage_add_idb_index(void* store_ptr, const char* name, size_t name_len, const char* key_path, size_t key_path_len, bool unique, bool multi) {
    if (!g_ctx || !g_ctx->storage || !store_ptr) return;
    g_ctx->storage->add_idb_index((IndexedDBObjectStore*)store_ptr, {name, name_len}, {key_path, key_path_len}, unique, multi);
}

EXPORT void storage_add_cache_storage(const char* name, size_t name_len) {
    if (!g_ctx || !g_ctx->storage) return;
    g_ctx->storage->add_cache_storage({name, name_len});
}

EXPORT void storage_set_manifest(const char* url, size_t url_len, const char* name, size_t name_len, const char* start_url, size_t start_url_len, const char* display, size_t display_len, const char* bg_color, size_t bg_color_len, const char* theme_color, size_t theme_color_len) {
    if (!g_ctx || !g_ctx->storage) return;
    g_ctx->storage->set_manifest({url, url_len}, {name, name_len}, {start_url, start_url_len}, {display, display_len}, {bg_color, bg_color_len}, {theme_color, theme_color_len});
}

EXPORT void* sw_register(uint32_t reg_id, uint32_t ver_id, const char* script, size_t script_len, const char* scope, size_t scope_len) {
    if (!g_ctx || !g_ctx->service_worker) return nullptr;
    return g_ctx->service_worker->register_worker(reg_id, ver_id, {script, script_len}, {scope, scope_len});
}

EXPORT void sw_update_status(uint32_t ver_id, uint32_t status, uint32_t state) {
    if (!g_ctx || !g_ctx->service_worker) return;
    g_ctx->service_worker->update_status(ver_id, (ServiceWorkerStatus)status, (ServiceWorkerRegistrationState)state);
}

EXPORT void sw_record_event(uint32_t ver_id, const char* name, size_t name_len, uint64_t dispatch, uint64_t completion, bool prevented) {
    if (!g_ctx || !g_ctx->service_worker) return;
    g_ctx->service_worker->record_event(ver_id, {name, name_len}, dispatch, completion, prevented);
}

EXPORT void security_set_state(uint32_t state) {
    if (!g_ctx || !g_ctx->security) return;
    g_ctx->security->set_security_state((SecurityState)state);
}

EXPORT void security_set_certificate(const char* subject, size_t subject_len, const char* issuer, size_t issuer_len, uint64_t from, uint64_t to, const char* proto, size_t proto_len, const char* kex, size_t kex_len, const char* cipher, size_t cipher_len, const char* san, size_t san_len, bool valid, bool trusted) {
    if (!g_ctx || !g_ctx->security) return;
    g_ctx->security->set_certificate({subject, subject_len}, {issuer, issuer_len}, from, to, {proto, proto_len}, {kex, kex_len}, {cipher, cipher_len}, {san, san_len}, valid, trusted);
}

EXPORT void security_add_csp_violation(const char* uri, size_t uri_len, const char* directive, size_t directive_len, const char* policy, size_t policy_len, const char* src, size_t src_len, uint32_t line) {
    if (!g_ctx || !g_ctx->security) return;
    g_ctx->security->add_csp_violation({uri, uri_len}, {directive, directive_len}, {policy, policy_len}, {src, src_len}, line);
}

EXPORT void security_set_lighthouse_scores(uint8_t perf, uint8_t a11y, uint8_t bp, uint8_t seo, uint8_t pwa) {
    if (!g_ctx || !g_ctx->security) return;
    g_ctx->security->set_lighthouse_scores(perf, a11y, bp, seo, pwa);
}

EXPORT void network_begin_request(uint32_t req_id, const char* url, size_t url_len, const char* method, size_t method_len, uint32_t resource_type, uint64_t time) {
    if (!g_ctx || !g_ctx->network) return;
    g_ctx->network->begin_request(req_id, {url, url_len}, {method, method_len}, (ResourceType)resource_type, time);
}

EXPORT void network_add_request_header(uint32_t req_id, const char* name, size_t name_len, const char* val, size_t val_len) {
    if (!g_ctx || !g_ctx->network) return;
    g_ctx->network->add_request_header(req_id, {name, name_len}, {val, val_len});
}

EXPORT void network_add_response_header(uint32_t req_id, const char* name, size_t name_len, const char* val, size_t val_len) {
    if (!g_ctx || !g_ctx->network) return;
    g_ctx->network->add_response_header(req_id, {name, name_len}, {val, val_len});
}

EXPORT void network_finish_request(uint32_t req_id, uint32_t status_code, size_t transfer, size_t decoded, uint64_t end_time) {
    if (!g_ctx || !g_ctx->network) return;
    g_ctx->network->finish_request(req_id, status_code, transfer, decoded, end_time);
}

EXPORT void network_add_data_chunk(uint32_t req_id, size_t length, uint64_t timestamp_us) {
    if (!g_ctx || !g_ctx->network) return;
    g_ctx->network->add_data_chunk(req_id, length, timestamp_us);
}

EXPORT void network_parse_raw_response_headers(uint32_t req_id, const char* raw_headers, size_t headers_len) {
    if (!g_ctx || !g_ctx->network) return;
    g_ctx->network->parse_raw_response_headers(req_id, {raw_headers, headers_len});
}

EXPORT void dom_diff_trees(uint32_t old_root_id, uint32_t new_root_id) {
    if (!g_ctx || !g_ctx->elements) return;
    DOMNode* old_root = g_ctx->elements->get_node_by_id(old_root_id);
    DOMNode* new_root = g_ctx->elements->get_node_by_id(new_root_id);
    g_ctx->elements->diff_trees(old_root, new_root);
}

EXPORT size_t console_get_paginated_messages(uint32_t min_level, const char* filter_str, size_t filter_len, size_t offset, size_t limit, ConsoleMessage* out_msgs) {
    if (!g_ctx || !g_ctx->console) return 0;
    return g_ctx->console->filter_and_paginate((LogLevel)min_level, {filter_str, filter_len}, offset, limit, out_msgs);
}

EXPORT size_t console_get_total_logged() {
    if (!g_ctx || !g_ctx->console) return 0;
    return g_ctx->console->get_total_logged();
}

EXPORT size_t network_get_requests_paginated(size_t offset, size_t limit, NetworkRequest** out_reqs) {
    if (!g_ctx || !g_ctx->network) return 0;
    return g_ctx->network->get_requests_paginated(offset, limit, out_reqs);
}

EXPORT double network_calculate_throughput(uint32_t req_id) {
    if (!g_ctx || !g_ctx->network) return 0.0;
    return g_ctx->network->calculate_throughput_bytes_per_sec(req_id);
}

EXPORT size_t cdp_dispatch_command(const char* json_cmd, size_t json_len, char* out_resp, size_t max_resp_len) {
    if (!g_ctx) return 0;
    CDPDispatcher dispatcher(g_ctx->elements, g_ctx->network, g_ctx->console, g_ctx->profiler, g_ctx->memory);
    return dispatcher.dispatch({json_cmd, json_len}, out_resp, max_resp_len);
}

EXPORT size_t js_tokenize(const char* js_code, size_t code_len, JSToken* out_tokens, size_t max_tokens, JSASTSummary* out_summary) {
    return JSLexer::tokenize({js_code, code_len}, out_tokens, max_tokens, out_summary);
}

EXPORT bool vfs_write_file(const char* path, size_t path_len, const char* content, size_t content_len) {
    if (!g_ctx || !g_ctx->vfs) return false;
    return g_ctx->vfs->write_file({path, path_len}, {content, content_len});
}

EXPORT bool vfs_file_exists(const char* path, size_t path_len) {
    if (!g_ctx || !g_ctx->vfs) return false;
    return g_ctx->vfs->file_exists({path, path_len});
}

// STORAGE CRUD EXPORTS
EXPORT void storage_set_local_storage(const char* k, size_t k_len, const char* v, size_t v_len) {
    if (g_ctx && g_ctx->storage) g_ctx->storage->set_local_storage({k, k_len}, {v, v_len});
}

EXPORT bool storage_delete_local_storage(const char* k, size_t k_len) {
    if (g_ctx && g_ctx->storage) return g_ctx->storage->delete_local_storage({k, k_len});
    return false;
}

EXPORT void storage_clear_local_storage() {
    if (g_ctx && g_ctx->storage) g_ctx->storage->clear_local_storage();
}

EXPORT void storage_set_session_storage(const char* k, size_t k_len, const char* v, size_t v_len) {
    if (g_ctx && g_ctx->storage) g_ctx->storage->set_session_storage({k, k_len}, {v, v_len});
}

EXPORT bool storage_delete_session_storage(const char* k, size_t k_len) {
    if (g_ctx && g_ctx->storage) return g_ctx->storage->delete_session_storage({k, k_len});
    return false;
}

EXPORT void storage_clear_session_storage() {
    if (g_ctx && g_ctx->storage) g_ctx->storage->clear_session_storage();
}

EXPORT bool storage_delete_cookie(const char* name, size_t n_len, const char* domain, size_t d_len) {
    if (g_ctx && g_ctx->storage) return g_ctx->storage->delete_cookie({name, n_len}, {domain, d_len});
    return false;
}

EXPORT void storage_clear_cookies() {
    if (g_ctx && g_ctx->storage) g_ctx->storage->clear_cookies();
}

EXPORT bool storage_delete_idb_database(const char* name, size_t n_len) {
    if (g_ctx && g_ctx->storage) return g_ctx->storage->delete_idb_database({name, n_len});
    return false;
}

EXPORT bool storage_delete_cache(const char* name, size_t n_len) {
    if (g_ctx && g_ctx->storage) return g_ctx->storage->delete_cache_storage({name, n_len});
    return false;
}

// STORAGE QUOTA & EVICTION
EXPORT void storage_get_quota_summary(StorageQuotaSummary* out_summary) {
    if (g_ctx && g_ctx->storage && out_summary) {
        *out_summary = g_ctx->storage->get_quota_summary();
    }
}

EXPORT size_t storage_evict_cache_if_over_quota() {
    if (g_ctx && g_ctx->storage) return g_ctx->storage->evict_cache_if_over_quota();
    return 0;
}

// PROFILER FRAME & CLS METRICS
EXPORT void profiler_record_frame(uint64_t timestamp_us, double duration_ms, double layout_shift_score) {
    if (g_ctx && g_ctx->profiler) {
        g_ctx->profiler->record_frame(timestamp_us, duration_ms, layout_shift_score);
    }
}

EXPORT void profiler_get_frame_summary(FrameMetricsSummary* out_summary) {
    if (g_ctx && g_ctx->profiler && out_summary) {
        *out_summary = g_ctx->profiler->get_frame_summary();
    }
}

// WEBSOCKET & SSE STREAM INSPECTOR
EXPORT void network_record_ws_frame(uint32_t req_id, uint8_t opcode, bool is_outgoing, const char* payload, size_t payload_len, uint64_t timestamp_us) {
    if (g_ctx && g_ctx->network) {
        g_ctx->network->record_ws_frame(req_id, opcode, is_outgoing, {payload, payload_len}, timestamp_us);
    }
}

EXPORT void network_record_sse_event(uint32_t req_id, const char* evt_type, size_t t_len, const char* data, size_t d_len, const char* sse_id, size_t id_len, uint64_t timestamp_us) {
    if (g_ctx && g_ctx->network) {
        g_ctx->network->record_sse_event(req_id, {evt_type, t_len}, {data, d_len}, {sse_id, id_len}, timestamp_us);
    }
}

// CANVAS RENDERER EXPORTS
EXPORT void canvas_reset_cmds() {
    if (g_ctx && g_ctx->canvas) g_ctx->canvas->reset_cmd_queue();
}

EXPORT size_t canvas_get_cmd_count() {
    if (g_ctx && g_ctx->canvas) return g_ctx->canvas->get_cmd_count();
    return 0;
}

EXPORT const CanvasDrawCmd* canvas_get_cmd_queue() {
    if (g_ctx && g_ctx->canvas) return g_ctx->canvas->get_cmd_queue();
    return nullptr;
}

EXPORT void canvas_render_fps_sparkline(float x, float y, float w, float h, const double* fps_samples, size_t sample_count, double target_fps) {
    if (g_ctx && g_ctx->canvas) {
        g_ctx->canvas->render_fps_sparkline(x, y, w, h, fps_samples, sample_count, target_fps);
    }
}

EXPORT void canvas_render_dom_overlay(float cx, float cy, float cw, float ch, float pt, float pr, float pb, float pl, float bt, float br, float bb, float bl, float mt, float mr, float mb, float ml) {
    if (g_ctx && g_ctx->canvas) {
        InspectOverlayBox box = { cx, cy, cw, ch, pt, pr, pb, pl, bt, br, bb, bl, mt, mr, mb, ml, {"", 0} };
        g_ctx->canvas->render_dom_inspect_overlay(box);
    }
}

EXPORT void canvas_render_network_waterfall(float x, float y, float total_w, float h, float dns_pct, float conn_pct, float ttfb_pct, float dl_pct) {
    if (g_ctx && g_ctx->canvas) {
        g_ctx->canvas->render_network_waterfall_bar(x, y, total_w, h, dns_pct, conn_pct, ttfb_pct, dl_pct);
    }
}

// SYNTAX HIGHLIGHTER EXPORTS
EXPORT void highlighter_tokenize_code(const char* code, size_t code_len) {
    if (g_ctx && g_ctx->highlighter) {
        g_ctx->highlighter->tokenize_code({code, code_len});
    }
}

EXPORT size_t highlighter_get_token_count() {
    if (g_ctx && g_ctx->highlighter) return g_ctx->highlighter->get_token_count();
    return 0;
}

EXPORT const SyntaxToken* highlighter_get_tokens() {
    if (g_ctx && g_ctx->highlighter) return g_ctx->highlighter->get_tokens();
    return nullptr;
}

// DIFF ENGINE EXPORTS
EXPORT void diff_compute(const char* old_code, size_t old_len, const char* new_code, size_t new_len, DiffSummary* out_summary) {
    if (g_ctx && g_ctx->diff) {
        DiffSummary s = g_ctx->diff->compute_diff({old_code, old_len}, {new_code, new_len});
        if (out_summary) *out_summary = s;
    }
}

EXPORT size_t diff_get_line_count() {
    if (g_ctx && g_ctx->diff) return g_ctx->diff->get_line_count();
    return 0;
}

EXPORT const DiffLine* diff_get_lines() {
    if (g_ctx && g_ctx->diff) return g_ctx->diff->get_diff_lines();
    return nullptr;
}

// VIRTUAL LIST ENGINE EXPORTS
EXPORT void virtual_list_add_log(uint32_t id, uint8_t level, const char* msg, size_t msg_len, const char* src, size_t src_len, uint32_t line, uint64_t timestamp_us) {
    if (g_ctx && g_ctx->virtual_list) {
        g_ctx->virtual_list->add_log(id, (LogLevelFilter)level, {msg, msg_len}, {src, src_len}, line, timestamp_us);
    }
}

EXPORT void virtual_list_apply_filter(uint8_t level_filter, const char* search, size_t search_len) {
    if (g_ctx && g_ctx->virtual_list) {
        g_ctx->virtual_list->apply_filter((LogLevelFilter)level_filter, {search, search_len});
    }
}

EXPORT void virtual_list_compute_window(float scroll_top, float viewport_h, float item_h, size_t overscan, VirtualWindowResult* out_res) {
    if (g_ctx && g_ctx->virtual_list && out_res) {
        *out_res = g_ctx->virtual_list->compute_window(scroll_top, viewport_h, item_h, overscan);
    }
}

// LAYOUT TREE ENGINE EXPORTS
EXPORT void layout_tree_reset() {
    if (g_ctx && g_ctx->layout_tree) g_ctx->layout_tree->reset();
}

EXPORT void layout_tree_add_node(uint32_t node_id, uint32_t parent_id, const char* tag, size_t tag_len, const char* elem_id, size_t id_len, const char* classes, size_t cls_len,
                                float x, float y, float w, float h,
                                float pt, float pr, float pb, float pl,
                                float bt, float br, float bb, float bl,
                                float mt, float mr, float mb, float ml,
                                int32_t z_idx, uint8_t display, uint8_t position) {
    if (g_ctx && g_ctx->layout_tree) {
        g_ctx->layout_tree->add_node(node_id, parent_id, {tag, tag_len}, {elem_id, id_len}, {classes, cls_len},
                                      x, y, w, h, pt, pr, pb, pl, bt, br, bb, bl, mt, mr, mb, ml, z_idx, display, position);
    }
}

EXPORT void layout_tree_get_summary(LayoutTreeMetricsSummary* out_summary) {
    if (g_ctx && g_ctx->layout_tree && out_summary) {
        *out_summary = g_ctx->layout_tree->get_summary();
    }
}

} // extern "C"
