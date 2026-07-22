#pragma once
#include "core.h"

enum class SecurityState {
    Unknown, Neutral, Insecure, Secure, Info
};

struct CertificateInfo {
    StringView subject_name;
    StringView issuer;
    uint64_t valid_from;
    uint64_t valid_to;
    StringView protocol;
    StringView key_exchange;
    StringView cipher;
    StringView san_list;
    bool is_valid;
    bool is_trusted;
};

struct LighthouseScores {
    uint8_t performance;
    uint8_t accessibility;
    uint8_t best_practices;
    uint8_t seo;
    uint8_t pwa;
};

struct CSPViolation {
    StringView blocked_uri;
    StringView violated_directive;
    StringView original_policy;
    StringView source_file;
    uint32_t line_number;
    CSPViolation* next;
};

class SecurityEngine {
private:
    ArenaAllocator* arena;
    SecurityState current_state;
    CertificateInfo active_cert;
    LighthouseScores latest_audit;
    
    CSPViolation* first_csp_violation;
    size_t violation_count;

public:
    SecurityEngine(ArenaAllocator* alloc) 
        : arena(alloc), current_state(SecurityState::Unknown), first_csp_violation(nullptr), violation_count(0) {
        active_cert = {{nullptr,0}, {nullptr,0}, 0, 0, {nullptr,0}, {nullptr,0}, {nullptr,0}, {nullptr,0}, false, false};
        latest_audit = {0, 0, 0, 0, 0};
    }

    void set_security_state(SecurityState state) {
        current_state = state;
    }

    void set_certificate(StringView subject, StringView issuer, uint64_t from, uint64_t to, StringView proto, StringView kex, StringView cipher, StringView san, bool valid, bool trusted) {
        active_cert.subject_name = subject;
        active_cert.issuer = issuer;
        active_cert.valid_from = from;
        active_cert.valid_to = to;
        active_cert.protocol = proto;
        active_cert.key_exchange = kex;
        active_cert.cipher = cipher;
        active_cert.san_list = san;
        active_cert.is_valid = valid;
        active_cert.is_trusted = trusted;
    }

    void add_csp_violation(StringView uri, StringView directive, StringView policy, StringView src, uint32_t line) {
        CSPViolation* v = (CSPViolation*)arena->allocate(sizeof(CSPViolation));
        if (!v) return;
        v->blocked_uri = uri;
        v->violated_directive = directive;
        v->original_policy = policy;
        v->source_file = src;
        v->line_number = line;
        v->next = first_csp_violation;
        first_csp_violation = v;
        violation_count++;
    }

    void set_lighthouse_scores(uint8_t perf, uint8_t a11y, uint8_t bp, uint8_t seo, uint8_t pwa) {
        latest_audit = {perf, a11y, bp, seo, pwa};
    }
};
