#pragma once
#include "core.h"

enum class ServiceWorkerStatus {
    Starting, Running, Stopping, Stopped
};

enum class ServiceWorkerRegistrationState {
    Installing, Waiting, Active, Redundant
};

struct ServiceWorkerEvent {
    StringView event_name;
    uint64_t dispatch_time_us;
    uint64_t completion_time_us;
    bool default_prevented;
    ServiceWorkerEvent* next;
};

struct ServiceWorker {
    uint32_t version_id;
    uint32_t registration_id;
    StringView script_url;
    StringView scope_url;
    ServiceWorkerStatus status;
    ServiceWorkerRegistrationState state;
    uint64_t last_update_check_time;
    ServiceWorkerEvent* first_event;
    ServiceWorker* next;
};

class ServiceWorkerEngine {
private:
    ArenaAllocator* arena;
    ServiceWorker* first_worker;
    size_t worker_count;

public:
    ServiceWorkerEngine(ArenaAllocator* alloc)
        : arena(alloc), first_worker(nullptr), worker_count(0) {
    }

    ServiceWorker* register_worker(uint32_t reg_id, uint32_t ver_id, StringView script, StringView scope) {
        ServiceWorker* sw = (ServiceWorker*)arena->allocate(sizeof(ServiceWorker));
        if (!sw) return nullptr;
        sw->version_id = ver_id;
        sw->registration_id = reg_id;
        sw->script_url = script;
        sw->scope_url = scope;
        sw->status = ServiceWorkerStatus::Starting;
        sw->state = ServiceWorkerRegistrationState::Installing;
        sw->last_update_check_time = 0;
        sw->first_event = nullptr;
        
        sw->next = first_worker;
        first_worker = sw;
        worker_count++;
        return sw;
    }
    
    void update_status(uint32_t ver_id, ServiceWorkerStatus status, ServiceWorkerRegistrationState state) {
        ServiceWorker* sw = first_worker;
        while (sw) {
            if (sw->version_id == ver_id) {
                sw->status = status;
                sw->state = state;
                return;
            }
            sw = sw->next;
        }
    }
    
    void record_event(uint32_t ver_id, StringView name, uint64_t dispatch_time, uint64_t completion_time, bool prevented) {
        ServiceWorker* sw = first_worker;
        while (sw) {
            if (sw->version_id == ver_id) {
                ServiceWorkerEvent* evt = (ServiceWorkerEvent*)arena->allocate(sizeof(ServiceWorkerEvent));
                if (!evt) return;
                evt->event_name = name;
                evt->dispatch_time_us = dispatch_time;
                evt->completion_time_us = completion_time;
                evt->default_prevented = prevented;
                evt->next = sw->first_event;
                sw->first_event = evt;
                return;
            }
            sw = sw->next;
        }
    }
};
