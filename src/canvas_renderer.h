#pragma once
#include "core.h"

// Pixel RGBA Color representation
struct ColorRGBA {
    uint8_t r, g, b, a;

    static ColorRGBA FromHex(uint32_t hex, uint8_t alpha = 255) {
        return {
            (uint8_t)((hex >> 16) & 0xFF),
            (uint8_t)((hex >> 8) & 0xFF),
            (uint8_t)(hex & 0xFF),
            alpha
        };
    }
};

// Canvas Draw Command Type
enum class CanvasCmdType : uint8_t {
    CLEAR = 1,
    RECT_FILL = 2,
    RECT_STROKE = 3,
    LINE = 4,
    SPARKLINE = 5,
    TEXT = 6,
    OVERLAY_BOX = 7
};

// Drawing Command structure for high-performance batching
struct CanvasDrawCmd {
    CanvasCmdType type;
    float x, y, width, height;
    float x2, y2; // For lines
    ColorRGBA color;
    ColorRGBA stroke_color;
    float line_width;
    StringView label;
    uint32_t flags;
};

// DOM Inspection Overlay Box Model Dimensions
struct InspectOverlayBox {
    float content_x, content_y, content_w, content_h;
    float padding_top, padding_right, padding_bottom, padding_left;
    float border_top, border_right, border_bottom, border_left;
    float margin_top, margin_right, margin_bottom, margin_left;
    StringView element_tag_id;
};

class CanvasRendererEngine {
private:
    ArenaAllocator* arena;
    
    // Pixel Framebuffer (RGBA32)
    uint32_t* pixel_buffer;
    uint32_t buffer_width;
    uint32_t buffer_height;

    // Command Queue for WebGL / Canvas2D bridge
    CanvasDrawCmd* cmd_queue;
    size_t cmd_capacity;
    size_t cmd_count;

public:
    CanvasRendererEngine(ArenaAllocator* alloc, uint32_t max_cmds = 10000)
        : arena(alloc), pixel_buffer(nullptr), buffer_width(0), buffer_height(0),
          cmd_capacity(max_cmds), cmd_count(0) {
        cmd_queue = (CanvasDrawCmd*)arena->allocate(sizeof(CanvasDrawCmd) * cmd_capacity);
    }

    void resize_framebuffer(uint32_t w, uint32_t h) {
        if (w == buffer_width && h == buffer_height && pixel_buffer) return;
        buffer_width = w;
        buffer_height = h;
        pixel_buffer = (uint32_t*)arena->allocate(sizeof(uint32_t) * w * h);
        clear_framebuffer(0xFF1E1E1E); // DevTools Dark Canvas BG
    }

    uint32_t* get_pixel_buffer() const { return pixel_buffer; }
    uint32_t get_buffer_width() const { return buffer_width; }
    uint32_t get_buffer_height() const { return buffer_height; }

    void clear_framebuffer(uint32_t color_abgr) {
        if (!pixel_buffer) return;
        size_t total = buffer_width * buffer_height;
        for (size_t i = 0; i < total; ++i) {
            pixel_buffer[i] = color_abgr;
        }
    }

    void reset_cmd_queue() {
        cmd_count = 0;
    }

    size_t get_cmd_count() const { return cmd_count; }
    const CanvasDrawCmd* get_cmd_queue() const { return cmd_queue; }

    // =========================================================================
    // DRAWING PRIMITIVES & BATCH COMMANDS
    // =========================================================================
    void push_rect_fill(float x, float y, float w, float h, ColorRGBA color) {
        if (cmd_count >= cmd_capacity) return;
        CanvasDrawCmd& cmd = cmd_queue[cmd_count++];
        cmd.type = CanvasCmdType::RECT_FILL;
        cmd.x = x; cmd.y = y; cmd.width = w; cmd.height = h;
        cmd.color = color;
    }

    void push_rect_stroke(float x, float y, float w, float h, ColorRGBA color, float line_width = 1.0f) {
        if (cmd_count >= cmd_capacity) return;
        CanvasDrawCmd& cmd = cmd_queue[cmd_count++];
        cmd.type = CanvasCmdType::RECT_STROKE;
        cmd.x = x; cmd.y = y; cmd.width = w; cmd.height = h;
        cmd.color = color;
        cmd.line_width = line_width;
    }

    void push_line(float x1, float y1, float x2, float y2, ColorRGBA color, float line_width = 1.0f) {
        if (cmd_count >= cmd_capacity) return;
        CanvasDrawCmd& cmd = cmd_queue[cmd_count++];
        cmd.type = CanvasCmdType::LINE;
        cmd.x = x1; cmd.y = y1; cmd.x2 = x2; cmd.y2 = y2;
        cmd.color = color;
        cmd.line_width = line_width;
    }

    // =========================================================================
    // HIGH-LEVEL DEVTOOLS METRIC RENDERERS
    // =========================================================================

    // 1. Render FPS / Frame Drop Sparkline Graph
    void render_fps_sparkline(float x, float y, float w, float h, const double* fps_samples, size_t sample_count, double target_fps = 60.0) {
        if (sample_count < 2) return;

        // Draw background container
        push_rect_fill(x, y, w, h, ColorRGBA::FromHex(0x18181B, 240));
        push_rect_stroke(x, y, w, h, ColorRGBA::FromHex(0x27272A, 255));

        // Target FPS line (e.g. 60 FPS line)
        float target_y = y + h - (float)(target_fps / 75.0 * h);
        push_line(x, target_y, x + w, target_y, ColorRGBA::FromHex(0x10B981, 120), 1.0f); // Green target line

        float step_x = w / (float)(sample_count - 1);
        for (size_t i = 0; i < sample_count - 1; ++i) {
            float x1 = x + (float)i * step_x;
            float x2 = x + (float)(i + 1) * step_x;

            double val1 = fps_samples[i] > 75.0 ? 75.0 : fps_samples[i];
            double val2 = fps_samples[i + 1] > 75.0 ? 75.0 : fps_samples[i + 1];

            float y1 = y + h - (float)(val1 / 75.0 * h);
            float y2 = y + h - (float)(val2 / 75.0 * h);

            // Color code based on performance (Red < 30fps, Yellow < 50fps, Green >= 50fps)
            ColorRGBA line_col = (val2 < 30.0) ? ColorRGBA::FromHex(0xEF4444, 255) :
                                 (val2 < 50.0) ? ColorRGBA::FromHex(0xF59E0B, 255) :
                                                 ColorRGBA::FromHex(0x10B981, 255);

            push_line(x1, y1, x2, y2, line_col, 1.8f);
        }
    }

    // 2. Render Chromium DOM Inspector Highlight Overlay (Margin, Border, Padding, Content)
    void render_dom_inspect_overlay(const InspectOverlayBox& box) {
        // Content Box (Blue #3B82F6 / 40% alpha)
        push_rect_fill(box.content_x, box.content_y, box.content_w, box.content_h, ColorRGBA::FromHex(0x3B82F6, 100));
        push_rect_stroke(box.content_x, box.content_y, box.content_w, box.content_h, ColorRGBA::FromHex(0x3B82F6, 200), 1.0f);

        // Padding Box (Green #10B981 / 30% alpha)
        float pad_x = box.content_x - box.padding_left;
        float pad_y = box.content_y - box.padding_top;
        float pad_w = box.content_w + box.padding_left + box.padding_right;
        float pad_h = box.content_h + box.padding_top + box.padding_bottom;
        push_rect_stroke(pad_x, pad_y, pad_w, pad_h, ColorRGBA::FromHex(0x10B981, 180), 1.0f);

        // Border Box (Yellow #F59E0B / 35% alpha)
        float border_x = pad_x - box.border_left;
        float border_y = pad_y - box.border_top;
        float border_w = pad_w + box.border_left + box.border_right;
        float border_h = pad_h + box.border_top + box.border_bottom;
        push_rect_stroke(border_x, border_y, border_w, border_h, ColorRGBA::FromHex(0xF59E0B, 200), 1.0f);

        // Margin Box (Orange #F97316 / 30% alpha)
        float margin_x = border_x - box.margin_left;
        float margin_y = border_y - box.margin_top;
        float margin_w = border_w + box.margin_left + box.margin_right;
        float margin_h = border_h + box.margin_top + box.margin_bottom;
        push_rect_stroke(margin_x, margin_y, margin_w, margin_h, ColorRGBA::FromHex(0xF97316, 200), 1.0f);
    }

    // 3. Render Network Timeline Waterfall Bar
    void render_network_waterfall_bar(float x, float y, float total_w, float h, 
                                     float dns_pct, float connect_pct, float ttfb_pct, float download_pct) {
        float curr_x = x;

        // DNS (Dark Orange)
        float dns_w = total_w * dns_pct;
        if (dns_w > 0) {
            push_rect_fill(curr_x, y, dns_w, h, ColorRGBA::FromHex(0xD97706, 255));
            curr_x += dns_w;
        }

        // Connect (Orange)
        float conn_w = total_w * connect_pct;
        if (conn_w > 0) {
            push_rect_fill(curr_x, y, conn_w, h, ColorRGBA::FromHex(0xF59E0B, 255));
            curr_x += conn_w;
        }

        // TTFB (Green)
        float ttfb_w = total_w * ttfb_pct;
        if (ttfb_w > 0) {
            push_rect_fill(curr_x, y, ttfb_w, h, ColorRGBA::FromHex(0x10B981, 255));
            curr_x += ttfb_w;
        }

        // Download (Blue)
        float dl_w = total_w * download_pct;
        if (dl_w > 0) {
            push_rect_fill(curr_x, y, dl_w, h, ColorRGBA::FromHex(0x3B82F6, 255));
        }
    }
};
