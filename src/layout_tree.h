#pragma once
#include "core.h"

enum class DisplayType : uint8_t {
    BLOCK = 0,
    FLEX = 1,
    GRID = 2,
    INLINE = 3,
    INLINE_BLOCK = 4,
    NONE = 5
};

enum class PositionType : uint8_t {
    STATIC = 0,
    RELATIVE = 1,
    ABSOLUTE = 2,
    FIXED = 3,
    STICKY = 4
};

struct LayoutBoxNode {
    uint32_t node_id;
    uint32_t parent_id;
    StringView tag_name;
    StringView element_id;
    StringView class_names;

    // BoundingClientRect (viewport relative)
    float x;
    float y;
    float width;
    float height;

    // Box Model Dimensions (px)
    float padding_top;
    float padding_right;
    float padding_bottom;
    float padding_left;

    float border_top;
    float border_right;
    float border_bottom;
    float border_left;

    float margin_top;
    float margin_right;
    float margin_bottom;
    float margin_left;

    int32_t z_index;
    DisplayType display;
    PositionType position;

    bool is_hovered;
    bool is_selected;
};

struct LayoutTreeMetricsSummary {
    uint32_t total_nodes;
    uint32_t flex_containers;
    uint32_t grid_containers;
    uint32_t fixed_positioned;
    float max_depth;
    float total_layout_area;
};

class LayoutTreeEngine {
private:
    ArenaAllocator* arena;
    LayoutBoxNode* nodes;
    size_t capacity;
    size_t node_count;

public:
    LayoutTreeEngine(ArenaAllocator* alloc, size_t max_nodes = 20000)
        : arena(alloc), capacity(max_nodes), node_count(0) {
        nodes = (LayoutBoxNode*)arena->allocate(sizeof(LayoutBoxNode) * capacity);
    }

    void reset() {
        node_count = 0;
    }

    size_t get_node_count() const { return node_count; }
    const LayoutBoxNode* get_nodes() const { return nodes; }

    void add_node(uint32_t node_id, uint32_t parent_id, StringView tag, StringView elem_id, StringView classes,
                  float x, float y, float w, float h,
                  float pt, float pr, float pb, float pl,
                  float bt, float br, float bb, float bl,
                  float mt, float mr, float mb, float ml,
                  int32_t z_idx, uint8_t display, uint8_t position) {
        if (node_count >= capacity) return;
        LayoutBoxNode& node = nodes[node_count++];
        node.node_id = node_id;
        node.parent_id = parent_id;
        node.tag_name = tag;
        node.element_id = elem_id;
        node.class_names = classes;

        node.x = x;
        node.y = y;
        node.width = w;
        node.height = h;

        node.padding_top = pt;
        node.padding_right = pr;
        node.padding_bottom = pb;
        node.padding_left = pl;

        node.border_top = bt;
        node.border_right = br;
        node.border_bottom = bb;
        node.border_left = bl;

        node.margin_top = mt;
        node.margin_right = mr;
        node.margin_bottom = mb;
        node.margin_left = ml;

        node.z_index = z_idx;
        node.display = (DisplayType)display;
        node.position = (PositionType)position;
        node.is_hovered = false;
        node.is_selected = false;
    }

    const LayoutBoxNode* hit_test(float click_x, float click_y) const {
        const LayoutBoxNode* best_match = nullptr;
        float smallest_area = 1e9f;

        for (size_t i = 0; i < node_count; ++i) {
            const LayoutBoxNode& n = nodes[i];
            if (n.display == DisplayType::NONE) continue;

            if (click_x >= n.x && click_x <= (n.x + n.width) &&
                click_y >= n.y && click_y <= (n.y + n.height)) {
                float area = n.width * n.height;
                if (area < smallest_area) {
                    smallest_area = area;
                    best_match = &n;
                }
            }
        }
        return best_match;
    }

    LayoutTreeMetricsSummary get_summary() const {
        LayoutTreeMetricsSummary summary = {0, 0, 0, 0, 0.0f, 0.0f};
        summary.total_nodes = (uint32_t)node_count;

        for (size_t i = 0; i < node_count; ++i) {
            const LayoutBoxNode& n = nodes[i];
            if (n.display == DisplayType::FLEX) summary.flex_containers++;
            if (n.display == DisplayType::GRID) summary.grid_containers++;
            if (n.position == PositionType::FIXED || n.position == PositionType::ABSOLUTE) summary.fixed_positioned++;
            summary.total_layout_area += (n.width * n.height);
        }

        return summary;
    }
};
