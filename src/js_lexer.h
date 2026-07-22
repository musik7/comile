#pragma once
#include "core.h"

enum class JSTokenType : uint32_t {
    Keyword = 1,
    Identifier = 2,
    StringLiteral = 3,
    NumericLiteral = 4,
    Punctuator = 5,
    Comment = 6,
    Whitespace = 7,
    RegexLiteral = 8,
    Unknown = 9
};

struct JSToken {
    JSTokenType type;
    StringView value;
    uint32_t line;
    uint32_t column;
};

struct JSASTSummary {
    uint32_t total_tokens;
    uint32_t function_count;
    uint32_t variable_count;
    uint32_t import_count;
    uint32_t export_count;
    uint32_t class_count;
};

class JSLexer {
private:
    static bool is_alpha(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
    }

    static bool is_digit(char c) {
        return c >= '0' && c <= '9';
    }

    static bool is_alnum(char c) {
        return is_alpha(c) || is_digit(c);
    }

    static bool is_keyword(StringView sv) {
        const char* keywords[] = {
            "const", "let", "var", "function", "class", "return", "if", "else", 
            "for", "while", "do", "switch", "case", "break", "continue", "try", 
            "catch", "finally", "throw", "async", "await", "import", "export", 
            "from", "default", "new", "this", "typeof", "instanceof", "void", 
            "delete", "yield", "static", "extends", "super", "null", "true", "false"
        };
        for (const char* kw : keywords) {
            if (sv.equals(kw)) return true;
        }
        return false;
    }

public:
    static size_t tokenize(StringView code, JSToken* out_tokens, size_t max_tokens, JSASTSummary* out_summary = nullptr) {
        if (!out_tokens || max_tokens == 0 || code.length == 0) return 0;

        size_t i = 0;
        size_t token_count = 0;
        uint32_t line = 1;
        uint32_t col = 1;

        if (out_summary) {
            out_summary->total_tokens = 0;
            out_summary->function_count = 0;
            out_summary->variable_count = 0;
            out_summary->import_count = 0;
            out_summary->export_count = 0;
            out_summary->class_count = 0;
        }

        while (i < code.length && token_count < max_tokens) {
            char c = code.data[i];

            // 1. Whitespace & Newlines
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                size_t start = i;
                uint32_t start_col = col;
                while (i < code.length) {
                    char ch = code.data[i];
                    if (ch == '\n') { line++; col = 1; }
                    else if (ch == ' ' || ch == '\t' || ch == '\r') { col++; }
                    else break;
                    i++;
                }
                out_tokens[token_count++] = { JSTokenType::Whitespace, { code.data + start, i - start }, line, start_col };
                continue;
            }

            // 2. Comments (// and /* */)
            if (c == '/' && i + 1 < code.length) {
                if (code.data[i + 1] == '/') {
                    size_t start = i;
                    uint32_t start_col = col;
                    while (i < code.length && code.data[i] != '\n') { i++; col++; }
                    out_tokens[token_count++] = { JSTokenType::Comment, { code.data + start, i - start }, line, start_col };
                    continue;
                } else if (code.data[i + 1] == '*') {
                    size_t start = i;
                    uint32_t start_col = col;
                    i += 2; col += 2;
                    while (i + 1 < code.length && !(code.data[i] == '*' && code.data[i + 1] == '/')) {
                        if (code.data[i] == '\n') { line++; col = 1; }
                        else { col++; }
                        i++;
                    }
                    if (i + 1 < code.length) { i += 2; col += 2; }
                    out_tokens[token_count++] = { JSTokenType::Comment, { code.data + start, i - start }, line, start_col };
                    continue;
                }
            }

            // 3. String Literals ('', "", ``)
            if (c == '"' || c == '\'' || c == '`') {
                char quote = c;
                size_t start = i;
                uint32_t start_col = col;
                i++; col++;
                while (i < code.length && code.data[i] != quote) {
                    if (code.data[i] == '\\' && i + 1 < code.length) { i += 2; col += 2; continue; }
                    if (code.data[i] == '\n') { line++; col = 1; }
                    else { col++; }
                    i++;
                }
                if (i < code.length && code.data[i] == quote) { i++; col++; }
                out_tokens[token_count++] = { JSTokenType::StringLiteral, { code.data + start, i - start }, line, start_col };
                continue;
            }

            // 4. Numbers
            if (is_digit(c)) {
                size_t start = i;
                uint32_t start_col = col;
                while (i < code.length && (is_digit(code.data[i]) || code.data[i] == '.' || code.data[i] == 'x' || code.data[i] == 'X')) {
                    i++; col++;
                }
                out_tokens[token_count++] = { JSTokenType::NumericLiteral, { code.data + start, i - start }, line, start_col };
                continue;
            }

            // 5. Identifiers and Keywords
            if (is_alpha(c)) {
                size_t start = i;
                uint32_t start_col = col;
                while (i < code.length && is_alnum(code.data[i])) {
                    i++; col++;
                }
                StringView val = { code.data + start, i - start };
                bool kw = is_keyword(val);
                JSTokenType ttype = kw ? JSTokenType::Keyword : JSTokenType::Identifier;

                if (out_summary) {
                    if (val.equals("function")) out_summary->function_count++;
                    else if (val.equals("const") || val.equals("let") || val.equals("var")) out_summary->variable_count++;
                    else if (val.equals("import")) out_summary->import_count++;
                    else if (val.equals("export")) out_summary->export_count++;
                    else if (val.equals("class")) out_summary->class_count++;
                }

                out_tokens[token_count++] = { ttype, val, line, start_col };
                continue;
            }

            // 6. Punctuators
            size_t start = i;
            uint32_t start_col = col;
            i++; col++;
            out_tokens[token_count++] = { JSTokenType::Punctuator, { code.data + start, 1 }, line, start_col };
        }

        if (out_summary) {
            out_summary->total_tokens = (uint32_t)token_count;
        }

        return token_count;
    }
};
