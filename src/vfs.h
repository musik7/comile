#pragma once
#include "core.h"

struct VFSNode {
    StringView name;
    StringView path;
    StringView content;
    size_t size;
    uint64_t modified_time;
    bool is_directory;
    VFSNode* first_child;
    VFSNode* next_sibling;
    VFSNode* parent;
};

class VFSEngine {
private:
    ArenaAllocator* arena;
    VFSNode* root;
    size_t total_files;
    size_t total_bytes;

    VFSNode* create_node(StringView name, StringView path, bool is_dir) {
        VFSNode* node = (VFSNode*)arena->allocate(sizeof(VFSNode));
        if (!node) return nullptr;
        node->name = name;
        node->path = path;
        node->content = {nullptr, 0};
        node->size = 0;
        node->modified_time = 0;
        node->is_directory = is_dir;
        node->first_child = nullptr;
        node->next_sibling = nullptr;
        node->parent = nullptr;
        return node;
    }

public:
    VFSEngine(ArenaAllocator* alloc) : arena(alloc), total_files(0), total_bytes(0) {
        root = create_node({"/", 1}, {"/", 1}, true);
    }

    VFSNode* get_root() const { return root; }

    VFSNode* find_node(StringView path) {
        if (!root || path.length == 0) return nullptr;
        if (path.equals("/") || path.equals("")) return root;

        // Traverse tree matching path segments
        VFSNode* current = root->first_child;
        while (current) {
            if (current->path.equals(path)) return current;
            if (current->is_directory && path.length > current->path.length && 
                path.data[current->path.length] == '/') {
                current = current->first_child;
            } else {
                current = current->next_sibling;
            }
        }
        return nullptr;
    }

    bool write_file(StringView path, StringView content, uint64_t timestamp = 0) {
        VFSNode* existing = find_node(path);
        if (existing) {
            if (existing->is_directory) return false;
            existing->content = content;
            existing->size = content.length;
            existing->modified_time = timestamp;
            return true;
        }

        // Create file under root
        VFSNode* file = create_node(path, path, false);
        if (!file) return false;
        file->content = content;
        file->size = content.length;
        file->modified_time = timestamp;
        file->parent = root;

        file->next_sibling = root->first_child;
        root->first_child = file;

        total_files++;
        total_bytes += content.length;
        return true;
    }

    StringView read_file(StringView path) {
        VFSNode* node = find_node(path);
        if (node && !node->is_directory) {
            return node->content;
        }
        return {nullptr, 0};
    }

    bool file_exists(StringView path) {
        VFSNode* node = find_node(path);
        return node != nullptr;
    }

    size_t get_total_files() const { return total_files; }
    size_t get_total_bytes() const { return total_bytes; }
};
