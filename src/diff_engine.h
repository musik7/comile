#pragma once
#include "core.h"

enum class DiffOpType : uint8_t {
    EQUAL = 0,
    INSERT = 1,
    DELETE = 2
};

struct DiffLine {
    DiffOpType type;
    StringView line_text;
    uint32_t old_line_num;
    uint32_t new_line_num;
};

struct DiffSummary {
    uint32_t total_lines;
    uint32_t additions;
    uint32_t deletions;
    uint32_t unchanged;
};

class DiffEngine {
private:
    ArenaAllocator* arena;
    DiffLine* diff_lines;
    size_t capacity;
    size_t line_count;

    size_t split_lines(StringView text, StringView* out_lines, size_t max_out) {
        if (!text.data || text.length == 0) return 0;
        size_t count = 0;
        size_t start = 0;
        for (size_t i = 0; i < text.length && count < max_out; ++i) {
            if (text.data[i] == '\n') {
                out_lines[count++] = { text.data + start, i - start };
                start = i + 1;
            }
        }
        if (start < text.length && count < max_out) {
            out_lines[count++] = { text.data + start, text.length - start };
        }
        return count;
    }

    bool strings_equal(StringView a, StringView b) {
        if (a.length != b.length) return false;
        for (size_t i = 0; i < a.length; ++i) {
            if (a.data[i] != b.data[i]) return false;
        }
        return true;
    }

public:
    DiffEngine(ArenaAllocator* alloc, size_t max_diff_lines = 10000)
        : arena(alloc), capacity(max_diff_lines), line_count(0) {
        diff_lines = (DiffLine*)arena->allocate(sizeof(DiffLine) * capacity);
    }

    void reset() {
        line_count = 0;
    }

    size_t get_line_count() const { return line_count; }
    const DiffLine* get_diff_lines() const { return diff_lines; }

    DiffSummary compute_diff(StringView old_text, StringView new_text) {
        reset();
        DiffSummary summary = {0, 0, 0, 0};

        size_t max_lines = 2000;
        StringView* old_lines = (StringView*)arena->allocate(sizeof(StringView) * max_lines);
        StringView* new_lines = (StringView*)arena->allocate(sizeof(StringView) * max_lines);

        size_t old_cnt = split_lines(old_text, old_lines, max_lines);
        size_t new_cnt = split_lines(new_text, new_lines, max_lines);

        size_t i = 0, j = 0;
        uint32_t old_line_no = 1;
        uint32_t new_line_no = 1;

        while ((i < old_cnt || j < new_cnt) && line_count < capacity) {
            if (i < old_cnt && j < new_cnt && strings_equal(old_lines[i], new_lines[j])) {
                DiffLine& dl = diff_lines[line_count++];
                dl.type = DiffOpType::EQUAL;
                dl.line_text = old_lines[i];
                dl.old_line_num = old_line_no++;
                dl.new_line_num = new_line_no++;
                summary.unchanged++;
                i++;
                j++;
            } else {
                // Check ahead for match in new_lines
                bool found_in_new = false;
                size_t lookahead_j = j + 1;
                while (lookahead_j < new_cnt && lookahead_j < j + 5) {
                    if (i < old_cnt && strings_equal(old_lines[i], new_lines[lookahead_j])) {
                        found_in_new = true;
                        break;
                    }
                    lookahead_j++;
                }

                if (found_in_new) {
                    // Lines inserted in new
                    while (j < lookahead_j && line_count < capacity) {
                        DiffLine& dl = diff_lines[line_count++];
                        dl.type = DiffOpType::INSERT;
                        dl.line_text = new_lines[j++];
                        dl.old_line_num = 0;
                        dl.new_line_num = new_line_no++;
                        summary.additions++;
                    }
                } else if (i < old_cnt) {
                    // Line deleted in old
                    DiffLine& dl = diff_lines[line_count++];
                    dl.type = DiffOpType::DELETE;
                    dl.line_text = old_lines[i++];
                    dl.old_line_num = old_line_no++;
                    dl.new_line_num = 0;
                    summary.deletions++;
                } else if (j < new_cnt) {
                    // Line added in new
                    DiffLine& dl = diff_lines[line_count++];
                    dl.type = DiffOpType::INSERT;
                    dl.line_text = new_lines[j++];
                    dl.old_line_num = 0;
                    dl.new_line_num = new_line_no++;
                    summary.additions++;
                }
            }
        }

        summary.total_lines = (uint32_t)line_count;
        return summary;
    }
};
