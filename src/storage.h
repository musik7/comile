#pragma once
#include "core.h"

struct Cookie {
    StringView name;
    StringView value;
    StringView domain;
    StringView path;
    uint64_t expires;
    size_t size;
    bool http_only;
    bool secure;
    StringView same_site;
    Cookie* next;
};

struct KeyValuePair {
    StringView key;
    StringView value;
    size_t size;
    KeyValuePair* next;
};

struct IndexedDBRecord {
    StringView key;
    StringView value;
    IndexedDBRecord* next;
};

struct IndexedDBIndex {
    StringView name;
    StringView key_path;
    bool unique;
    bool multi_entry;
    IndexedDBIndex* next;
};

struct IndexedDBObjectStore {
    StringView name;
    StringView key_path;
    bool auto_increment;
    size_t record_count;
    IndexedDBRecord* first_record;
    IndexedDBIndex* first_index;
    IndexedDBObjectStore* next;
};

struct IndexedDBDatabase {
    uint32_t id;
    StringView name;
    uint32_t version;
    IndexedDBObjectStore* first_store;
    size_t store_count;
    IndexedDBDatabase* next;
};

struct CacheEntry {
    StringView url;
    uint32_t status;
    StringView body;
    size_t size;
    CacheEntry* next;
};

struct CacheStorageCache {
    StringView name;
    size_t entry_count;
    size_t total_size;
    CacheEntry* first_entry;
    CacheStorageCache* next;
};

struct WebManifest {
    StringView url;
    StringView name;
    StringView start_url;
    StringView display;
    StringView background_color;
    StringView theme_color;
};

struct StorageQuotaSummary {
    uint64_t total_quota_bytes;
    uint64_t total_used_bytes;
    uint64_t local_storage_bytes;
    uint64_t session_storage_bytes;
    uint64_t cookie_bytes;
    uint64_t indexeddb_bytes;
    uint64_t cache_bytes;
    uint64_t remaining_bytes;
    bool quota_exceeded;
};

class StorageEngine {
private:
    ArenaAllocator* arena;
    
    Cookie* first_cookie;
    size_t cookie_count;

    KeyValuePair* first_local_storage;
    size_t ls_count;

    KeyValuePair* first_session_storage;
    size_t ss_count;

    IndexedDBDatabase* first_idb;
    size_t idb_count;
    uint32_t next_idb_id;

    CacheStorageCache* first_cache;
    size_t cache_count;
    
    WebManifest current_manifest;
    uint64_t max_quota_bytes;

public:
    StorageEngine(ArenaAllocator* alloc)
        : arena(alloc), 
          first_cookie(nullptr), cookie_count(0),
          first_local_storage(nullptr), ls_count(0),
          first_session_storage(nullptr), ss_count(0),
          first_idb(nullptr), idb_count(0), next_idb_id(1),
          first_cache(nullptr), cache_count(0),
          max_quota_bytes(50 * 1024 * 1024) { // Default 50MB
    }

    void set_max_quota(uint64_t quota_bytes) {
        max_quota_bytes = quota_bytes;
    }

    StorageQuotaSummary get_quota_summary() const {
        StorageQuotaSummary summary;
        summary.total_quota_bytes = max_quota_bytes;
        summary.local_storage_bytes = 0;
        summary.session_storage_bytes = 0;
        summary.cookie_bytes = 0;
        summary.indexeddb_bytes = 0;
        summary.cache_bytes = 0;

        // 1. LocalStorage
        KeyValuePair* ls = first_local_storage;
        while (ls) {
            summary.local_storage_bytes += ls->size;
            ls = ls->next;
        }

        // 2. SessionStorage
        KeyValuePair* ss = first_session_storage;
        while (ss) {
            summary.session_storage_bytes += ss->size;
            ss = ss->next;
        }

        // 3. Cookies
        Cookie* c = first_cookie;
        while (c) {
            summary.cookie_bytes += c->size;
            c = c->next;
        }

        // 4. IndexedDB
        IndexedDBDatabase* db = first_idb;
        while (db) {
            IndexedDBObjectStore* store = db->first_store;
            while (store) {
                IndexedDBRecord* rec = store->first_record;
                while (rec) {
                    summary.indexeddb_bytes += (rec->key.length + rec->value.length);
                    rec = rec->next;
                }
                store = store->next;
            }
            db = db->next;
        }

        // 5. CacheStorage
        CacheStorageCache* cache = first_cache;
        while (cache) {
            summary.cache_bytes += cache->total_size;
            cache = cache->next;
        }

        summary.total_used_bytes = summary.local_storage_bytes + summary.session_storage_bytes + 
                                   summary.cookie_bytes + summary.indexeddb_bytes + summary.cache_bytes;
        
        summary.remaining_bytes = (summary.total_used_bytes >= max_quota_bytes) ? 0 : (max_quota_bytes - summary.total_used_bytes);
        summary.quota_exceeded = (summary.total_used_bytes >= max_quota_bytes);
        return summary;
    }

    // LRU Eviction Policy for Cache Storage entries when over quota
    size_t evict_cache_if_over_quota() {
        StorageQuotaSummary summary = get_quota_summary();
        if (!summary.quota_exceeded) return 0;

        size_t freed_bytes = 0;
        CacheStorageCache* cache = first_cache;
        while (cache && get_quota_summary().quota_exceeded) {
            if (cache->first_entry) {
                CacheEntry* to_evict = cache->first_entry;
                size_t sz = to_evict->size;
                cache->first_entry = to_evict->next;
                if (cache->entry_count > 0) cache->entry_count--;
                if (cache->total_size >= sz) cache->total_size -= sz;
                freed_bytes += sz;
            } else {
                cache = cache->next;
            }
        }
        return freed_bytes;
    }

    // =========================================================================
    // COOKIES CRUD
    // =========================================================================
    void add_cookie(StringView name, StringView value, StringView domain, StringView path, uint64_t expires, bool http_only, bool secure, StringView same_site) {
        // Check if cookie exists to update
        Cookie* curr = first_cookie;
        while (curr) {
            if (curr->name.equals(name) && curr->domain.equals(domain)) {
                curr->value = value;
                curr->expires = expires;
                curr->size = name.length + value.length;
                curr->http_only = http_only;
                curr->secure = secure;
                curr->same_site = same_site;
                return;
            }
            curr = curr->next;
        }

        Cookie* cookie = (Cookie*)arena->allocate(sizeof(Cookie));
        if (!cookie) return;
        cookie->name = name;
        cookie->value = value;
        cookie->domain = domain;
        cookie->path = path;
        cookie->expires = expires;
        cookie->size = name.length + value.length;
        cookie->http_only = http_only;
        cookie->secure = secure;
        cookie->same_site = same_site;
        cookie->next = first_cookie;
        first_cookie = cookie;
        cookie_count++;
    }

    StringView get_cookie(StringView name) {
        Cookie* curr = first_cookie;
        while (curr) {
            if (curr->name.equals(name)) return curr->value;
            curr = curr->next;
        }
        return {nullptr, 0};
    }

    bool delete_cookie(StringView name, StringView domain) {
        Cookie* curr = first_cookie;
        Cookie* prev = nullptr;
        while (curr) {
            if (curr->name.equals(name) && (domain.length == 0 || curr->domain.equals(domain))) {
                if (prev) prev->next = curr->next;
                else first_cookie = curr->next;
                if (cookie_count > 0) cookie_count--;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    void clear_cookies() {
        first_cookie = nullptr;
        cookie_count = 0;
    }

    // =========================================================================
    // LOCAL STORAGE CRUD
    // =========================================================================
    void set_local_storage(StringView key, StringView value) {
        KeyValuePair* curr = first_local_storage;
        while (curr) {
            if (curr->key.equals(key)) {
                curr->value = value;
                curr->size = key.length + value.length;
                return;
            }
            curr = curr->next;
        }

        KeyValuePair* pair = (KeyValuePair*)arena->allocate(sizeof(KeyValuePair));
        if (!pair) return;
        pair->key = key;
        pair->value = value;
        pair->size = key.length + value.length;
        pair->next = first_local_storage;
        first_local_storage = pair;
        ls_count++;
    }

    void add_local_storage(StringView key, StringView value) {
        set_local_storage(key, value);
    }

    StringView get_local_storage(StringView key) {
        KeyValuePair* curr = first_local_storage;
        while (curr) {
            if (curr->key.equals(key)) return curr->value;
            curr = curr->next;
        }
        return {nullptr, 0};
    }

    bool delete_local_storage(StringView key) {
        KeyValuePair* curr = first_local_storage;
        KeyValuePair* prev = nullptr;
        while (curr) {
            if (curr->key.equals(key)) {
                if (prev) prev->next = curr->next;
                else first_local_storage = curr->next;
                if (ls_count > 0) ls_count--;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    void clear_local_storage() {
        first_local_storage = nullptr;
        ls_count = 0;
    }

    // =========================================================================
    // SESSION STORAGE CRUD
    // =========================================================================
    void set_session_storage(StringView key, StringView value) {
        KeyValuePair* curr = first_session_storage;
        while (curr) {
            if (curr->key.equals(key)) {
                curr->value = value;
                curr->size = key.length + value.length;
                return;
            }
            curr = curr->next;
        }

        KeyValuePair* pair = (KeyValuePair*)arena->allocate(sizeof(KeyValuePair));
        if (!pair) return;
        pair->key = key;
        pair->value = value;
        pair->size = key.length + value.length;
        pair->next = first_session_storage;
        first_session_storage = pair;
        ss_count++;
    }

    void add_session_storage(StringView key, StringView value) {
        set_session_storage(key, value);
    }

    StringView get_session_storage(StringView key) {
        KeyValuePair* curr = first_session_storage;
        while (curr) {
            if (curr->key.equals(key)) return curr->value;
            curr = curr->next;
        }
        return {nullptr, 0};
    }

    bool delete_session_storage(StringView key) {
        KeyValuePair* curr = first_session_storage;
        KeyValuePair* prev = nullptr;
        while (curr) {
            if (curr->key.equals(key)) {
                if (prev) prev->next = curr->next;
                else first_session_storage = curr->next;
                if (ss_count > 0) ss_count--;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    void clear_session_storage() {
        first_session_storage = nullptr;
        ss_count = 0;
    }
    
    // =========================================================================
    // INDEXEDDB CRUD
    // =========================================================================
    IndexedDBDatabase* add_idb_database(StringView name, uint32_t version) {
        IndexedDBDatabase* db = (IndexedDBDatabase*)arena->allocate(sizeof(IndexedDBDatabase));
        if (!db) return nullptr;
        db->id = next_idb_id++;
        db->name = name;
        db->version = version;
        db->first_store = nullptr;
        db->store_count = 0;
        db->next = first_idb;
        first_idb = db;
        idb_count++;
        return db;
    }

    IndexedDBObjectStore* add_idb_object_store(IndexedDBDatabase* db, StringView name, StringView key_path, bool auto_inc) {
        if (!db) return nullptr;
        IndexedDBObjectStore* store = (IndexedDBObjectStore*)arena->allocate(sizeof(IndexedDBObjectStore));
        if (!store) return nullptr;
        store->name = name;
        store->key_path = key_path;
        store->auto_increment = auto_inc;
        store->record_count = 0;
        store->first_record = nullptr;
        store->first_index = nullptr;
        store->next = db->first_store;
        db->first_store = store;
        db->store_count++;
        return store;
    }

    bool put_idb_record(IndexedDBObjectStore* store, StringView key, StringView value) {
        if (!store) return false;
        IndexedDBRecord* curr = store->first_record;
        while (curr) {
            if (curr->key.equals(key)) {
                curr->value = value;
                return true;
            }
            curr = curr->next;
        }
        IndexedDBRecord* rec = (IndexedDBRecord*)arena->allocate(sizeof(IndexedDBRecord));
        if (!rec) return false;
        rec->key = key;
        rec->value = value;
        rec->next = store->first_record;
        store->first_record = rec;
        store->record_count++;
        return true;
    }

    bool delete_idb_record(IndexedDBObjectStore* store, StringView key) {
        if (!store) return false;
        IndexedDBRecord* curr = store->first_record;
        IndexedDBRecord* prev = nullptr;
        while (curr) {
            if (curr->key.equals(key)) {
                if (prev) prev->next = curr->next;
                else store->first_record = curr->next;
                if (store->record_count > 0) store->record_count--;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    bool delete_idb_database(StringView name) {
        IndexedDBDatabase* curr = first_idb;
        IndexedDBDatabase* prev = nullptr;
        while (curr) {
            if (curr->name.equals(name)) {
                if (prev) prev->next = curr->next;
                else first_idb = curr->next;
                if (idb_count > 0) idb_count--;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }
    
    void add_idb_index(IndexedDBObjectStore* store, StringView name, StringView key_path, bool unique, bool multi) {
        if (!store) return;
        IndexedDBIndex* index = (IndexedDBIndex*)arena->allocate(sizeof(IndexedDBIndex));
        if (!index) return;
        index->name = name;
        index->key_path = key_path;
        index->unique = unique;
        index->multi_entry = multi;
        index->next = store->first_index;
        store->first_index = index;
    }

    // =========================================================================
    // CACHE STORAGE CRUD
    // =========================================================================
    CacheStorageCache* add_cache_storage(StringView name) {
        CacheStorageCache* cache = (CacheStorageCache*)arena->allocate(sizeof(CacheStorageCache));
        if (!cache) return nullptr;
        cache->name = name;
        cache->entry_count = 0;
        cache->total_size = 0;
        cache->first_entry = nullptr;
        cache->next = first_cache;
        first_cache = cache;
        cache_count++;
        return cache;
    }

    bool put_cache_entry(CacheStorageCache* cache, StringView url, uint32_t status, StringView body) {
        if (!cache) return false;
        CacheEntry* curr = cache->first_entry;
        while (curr) {
            if (curr->url.equals(url)) {
                cache->total_size -= curr->size;
                curr->status = status;
                curr->body = body;
                curr->size = url.length + body.length;
                cache->total_size += curr->size;
                return true;
            }
            curr = curr->next;
        }

        CacheEntry* entry = (CacheEntry*)arena->allocate(sizeof(CacheEntry));
        if (!entry) return false;
        entry->url = url;
        entry->status = status;
        entry->body = body;
        entry->size = url.length + body.length;
        entry->next = cache->first_entry;
        cache->first_entry = entry;
        cache->entry_count++;
        cache->total_size += entry->size;
        return true;
    }

    bool delete_cache_entry(CacheStorageCache* cache, StringView url) {
        if (!cache) return false;
        CacheEntry* curr = cache->first_entry;
        CacheEntry* prev = nullptr;
        while (curr) {
            if (curr->url.equals(url)) {
                if (prev) prev->next = curr->next;
                else cache->first_entry = curr->next;
                if (cache->entry_count > 0) cache->entry_count--;
                if (cache->total_size >= curr->size) cache->total_size -= curr->size;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    bool delete_cache_storage(StringView name) {
        CacheStorageCache* curr = first_cache;
        CacheStorageCache* prev = nullptr;
        while (curr) {
            if (curr->name.equals(name)) {
                if (prev) prev->next = curr->next;
                else first_cache = curr->next;
                if (cache_count > 0) cache_count--;
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    void set_manifest(StringView url, StringView name, StringView start_url, StringView display, StringView bg_color, StringView theme_color) {
        current_manifest.url = url;
        current_manifest.name = name;
        current_manifest.start_url = start_url;
        current_manifest.display = display;
        current_manifest.background_color = bg_color;
        current_manifest.theme_color = theme_color;
    }
};
