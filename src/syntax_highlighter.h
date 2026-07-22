#pragma once
#include "core.h"

enum class HighlightTokenType : uint8_t {
    KEYWORD = 1,
    STRING = 2,
    NUMBER = 3,
    COMMENT = 4,
    IDENTIFIER = 5,
    OPERATOR = 6,
    PUNCTUATION = 7,
    TAG_NAME = 8,
    ATTR_NAME = 9,
    PLAIN = 0
};

struct SyntaxToken {
    HighlightTokenType type;
    uint32_t start_offset;
    uint32_t length;
    uint32_t line;
    uint32_t column;
};

class SyntaxHighlighterEngine {
private:
    ArenaAllocator* arena;
    SyntaxToken* token_buffer;
    size_t capacity;
    size_t token_count;

    bool is_keyword(const char* str, size_t len) {
        static const char* keywords[] = {
            "const", "let", "var", "function", "return", "if", "else", "for", "while",
            "switch", "case", "break", "continue", "import", "export", "from", "default",
            "class", "extends", "super", "this", "new", "async", "await", "try", "catch",
            "finally", "throw", "typeof", "instanceof", "true", "false", "null", "undefined",
            "interface", "type", "enum", "void", "public", "private", "protected"
        };
        for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
            size_t kw_len = 0;
            while (keywords[i][kw_len]) kw_len++;
            if (kw_len == len) {
                bool match = true;
                for (size_t j = 0; j < len; ++j) {
                    if (keywords[i][j] != str[j]) { match = false; break; }
                }
                if (match) return true;
            }
        }
        return false;
    }

public:
    SyntaxHighlighterEngine(ArenaAllocator* alloc, size_t max_tokens = 50000)
        : arena(alloc), capacity(max_tokens), token_count(0) {
        token_buffer = (SyntaxToken*)arena->allocate(sizeof(SyntaxToken) * capacity);
    }

    void reset() {
        token_count = 0;
    }

    size_t get_token_count() const { return token_count; }
    const SyntaxToken* get_tokens() const { return token_buffer; }

    void tokenize_code(StringView code) {
        reset();
        if (code.length == 0 || !code.data) return;

        size_t idx = 0;
        uint32_t current_line = 1;
        uint32_t current_col = 1;

        while (idx < code.length && token_count < capacity) {
            char c = code.data[idx];

            // Whitespace & newlines
            if (c == '\n') {
                idx++;
                current_line++;
                current_col = 1;
                continue;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                idx++;
                current_col++;
                continue;
            }

            SyntaxToken& tok = token_buffer[token_count];
            tok.start_offset = (uint32_t)idx;
            tok.line = current_line;
            tok.column = current_col;

            // Single line / Multi-line Comments
            if (c == '/' && idx + 1 < code.length && (code.data[idx + 1] == '/' || code.data[idx + 1] == '*')) {
                tok.type = HighlightTokenType::COMMENT;
                size_t start = idx;
                if (code.data[idx + 1] == '/') {
                    while (idx < code.length && code.data[idx] != '\n') idx++;
                } else {
                    idx += 2;
                    while (idx + 1 < code.length && !(code.data[idx] == '*' && code.data[idx + 1] == '/')) {
                        if (code.data[idx] == '\n') { current_line++; current_col = 1; }
                        idx++;
                    }
                    if (idx + 1 < code.length) idx += 2;
                }
                tok.length = (uint32_t)(idx - start);
                current_col += tok.length;
                token_count++;
                continue;
            }

            // String literals
            if (c == '"' || c == '\'' || c == '`') {
                tok.type = HighlightTokenType::STRING;
                char quote = c;
                size_t start = idx++;
                while (idx < code.length && code.data[idx] != quote) {
                    if (code.data[idx] == '\\' && idx + 1 < code.length) idx++;
                    if (code.data[idx] == '\n') { current_line++; current_col = 1; }
                    idx++;
                }
                if (idx < code.length) idx++; // skip closing quote
                tok.length = (uint32_t)(idx - start);
                current_col += tok.length;
                token_count++;
                continue;
            }

            // Numbers
            if (c >= '0' && c <= '9') {
                tok.type = HighlightTokenType::NUMBER;
                size_t start = idx;
                while (idx < code.length && ((code.data[idx] >= '0' && code.data[idx] <= '9') || code.data[idx] == '.' || code.data[idx] == 'x' || code.data[idx] == 'X')) {
                    idx++;
                }
                tok.length = (uint32_t)(idx - start);
                current_col += tok.length;
                token_count++;
                continue;
            }

            // Identifiers / Keywords
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$') {
                size_t start = idx;
                while (idx < code.length && ((code.data[idx] >= 'a' && code.data[idx] <= 'z') || 
                                             (code.data[idx] >= 'A' && code.data[idx] <= 'Z') || 
                                             (code.data[idx] >= '0' && code.data[idx] <= '9') || 
                                             code.data[idx] == '_' || code.data[idx] == '$')) {
                    idx++;
                }
                tok.length = (uint32_t)(idx - start);
                if (is_keyword(code.data + start, tok.length)) {
                    tok.type = HighlightTokenType::KEYWORD;
                } else {
                    tok.type = HighlightTokenType::IDENTIFIER;
                }
                current_col += tok.length;
                token_count++;
                continue;
            }

            // Operators & Punctuation
            if (c == '+' || c == '-' || c == '*' || c == '/' || c == '=' || c == '!' || c == '<' || c == '>' || c == '&' || c == '|' || c == '^' || c == '%') {
                tok.type = HighlightTokenType::OPERATOR;
                tok.length = 1;
                idx++;
                current_col++;
                token_count++;
                continue;
            }

            tok.type = HighlightTokenType::PUNCTUATION;
            tok.length = 1;
            idx++;
            current_col++;
            token_count++;
        }
    }
};
