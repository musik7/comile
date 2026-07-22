#pragma once
#include "core.h"

enum class NodeType { 
    Element = 1, 
    Attribute = 2,
    Text = 3, 
    CDATA = 4,
    EntityReference = 5,
    Entity = 6,
    ProcessingInstruction = 7,
    Comment = 8, 
    Document = 9,
    DocumentType = 10,
    DocumentFragment = 11,
    ShadowRoot = 12
};

struct DOMAttribute {
    StringView name;
    StringView value;
    DOMAttribute* next;
};

struct CSSComputedStyle {
    StringView property;
    StringView value;
    CSSComputedStyle* next;
};

struct CSSMatchedRule {
    StringView selector;
    StringView source_url;
    uint32_t start_line;
    bool is_inherited;
    CSSMatchedRule* next;
};

struct LayoutRect { float x, y, width, height; };
struct BoxModel {
    LayoutRect content;
    LayoutRect padding;
    LayoutRect border;
    LayoutRect margin;
};

struct DOMNode {
    uint32_t id;
    uint32_t backend_node_id; 
    NodeType type;
    StringView node_name;  
    StringView node_value; 
    
    DOMNode* parent;
    DOMNode* first_child;
    DOMNode* last_child;
    DOMNode* next_sibling;
    DOMNode* prev_sibling;
    
    DOMNode* shadow_root; 
    
    DOMAttribute* first_attribute;
    CSSComputedStyle* first_computed_style;
    CSSMatchedRule* first_matched_rule;
    
    BoxModel* box_model; 
    
    uint32_t child_node_count;
    bool is_expanded; 
};

struct DOMMutationPatch {
    enum Action { Insert, Remove, UpdateAttribute, RemoveAttribute, UpdateText, ShadowRootPushed };
    Action action;
    uint32_t target_node_id;
    uint32_t related_node_id;
    StringView payload_key; 
    StringView payload_val; 
};

class ElementsEngine {
private:
    ArenaAllocator* arena;
    DOMNode* root_document;
    
    DOMNode** node_map; 
    size_t node_map_capacity;
    uint32_t next_node_id;

    DOMMutationPatch* mutation_queue;
    size_t mutation_count;
    size_t mutation_capacity;

    void push_mutation(DOMMutationPatch::Action act, uint32_t target, uint32_t related, StringView key = {nullptr,0}, StringView val = {nullptr,0}) {
        if (mutation_count < mutation_capacity) {
            mutation_queue[mutation_count++] = {act, target, related, key, val};
        }
    }

public:
    ElementsEngine(ArenaAllocator* alloc) 
        : arena(alloc), root_document(nullptr), next_node_id(1), 
          node_map_capacity(50000), mutation_count(0), mutation_capacity(2048) {
        node_map = (DOMNode**)arena->allocate(sizeof(DOMNode*) * node_map_capacity);
        mutation_queue = (DOMMutationPatch*)arena->allocate(sizeof(DOMMutationPatch) * mutation_capacity);
    }

    DOMNode* create_node(uint32_t backend_id, NodeType type, StringView name, StringView value = {nullptr, 0}) {
        if (next_node_id >= node_map_capacity) return nullptr;
        
        DOMNode* node = (DOMNode*)arena->allocate(sizeof(DOMNode));
        if (!node) return nullptr;
        
        node->id = next_node_id++;
        node->backend_node_id = backend_id;
        node->type = type;
        node->node_name = name;
        node->node_value = value;
        
        node->parent = node->first_child = node->last_child = nullptr;
        node->next_sibling = node->prev_sibling = node->shadow_root = nullptr;
        
        node->first_attribute = nullptr;
        node->first_computed_style = nullptr;
        node->first_matched_rule = nullptr;
        node->box_model = nullptr;
        
        node->child_node_count = 0;
        node->is_expanded = false;
        
        node_map[node->id] = node;
        return node;
    }

    DOMNode* get_node_by_id(uint32_t id) {
        if (id > 0 && id < next_node_id) return node_map[id];
        return nullptr;
    }

    void insert_before(DOMNode* parent, DOMNode* child, DOMNode* ref_child) {
        if (!parent || !child) return;
        child->parent = parent;
        
        if (!ref_child) {
            if (!parent->first_child) {
                parent->first_child = child;
            } else {
                parent->last_child->next_sibling = child;
                child->prev_sibling = parent->last_child;
            }
            parent->last_child = child;
        } else {
            child->next_sibling = ref_child;
            child->prev_sibling = ref_child->prev_sibling;
            
            if (ref_child->prev_sibling) {
                ref_child->prev_sibling->next_sibling = child;
            } else {
                parent->first_child = child;
            }
            ref_child->prev_sibling = child;
        }
        parent->child_node_count++;
        push_mutation(DOMMutationPatch::Insert, parent->id, child->id, {nullptr,0}, {nullptr,0});
    }
    
    void remove_child(DOMNode* parent, DOMNode* child) {
        if (!parent || !child || child->parent != parent) return;
        
        if (child->prev_sibling) child->prev_sibling->next_sibling = child->next_sibling;
        else parent->first_child = child->next_sibling;
        
        if (child->next_sibling) child->next_sibling->prev_sibling = child->prev_sibling;
        else parent->last_child = child->prev_sibling;
        
        child->parent = nullptr;
        child->next_sibling = nullptr;
        child->prev_sibling = nullptr;
        parent->child_node_count--;
        
        push_mutation(DOMMutationPatch::Remove, parent->id, child->id, {nullptr,0}, {nullptr,0});
    }

    void set_attribute(DOMNode* node, StringView name, StringView value) {
        if (!node) return;
        DOMAttribute* attr = node->first_attribute;
        while (attr) {
            if (attr->name.equals(name)) {
                attr->value = value;
                push_mutation(DOMMutationPatch::UpdateAttribute, node->id, 0, name, value);
                return;
            }
            attr = attr->next;
        }
        
        attr = (DOMAttribute*)arena->allocate(sizeof(DOMAttribute));
        if(!attr) return;
        attr->name = name;
        attr->value = value;
        attr->next = node->first_attribute;
        node->first_attribute = attr;
        
        push_mutation(DOMMutationPatch::UpdateAttribute, node->id, 0, name, value);
    }
    
    void remove_attribute(DOMNode* node, StringView name) {
        if (!node) return;
        DOMAttribute* attr = node->first_attribute;
        DOMAttribute* prev = nullptr;
        
        while (attr) {
            if (attr->name.equals(name)) {
                if (prev) prev->next = attr->next;
                else node->first_attribute = attr->next;
                push_mutation(DOMMutationPatch::RemoveAttribute, node->id, 0, name, {nullptr,0});
                return;
            }
            prev = attr;
            attr = attr->next;
        }
    }

    void set_node_value(DOMNode* node, StringView value) {
        if (!node) return;
        node->node_value = value;
        push_mutation(DOMMutationPatch::UpdateText, node->id, 0, {nullptr,0}, value);
    }
    
    void attach_shadow_root(DOMNode* host, DOMNode* shadow_root) {
        if (!host || !shadow_root) return;
        host->shadow_root = shadow_root;
        shadow_root->parent = host;
        push_mutation(DOMMutationPatch::ShadowRootPushed, host->id, shadow_root->id, {nullptr,0}, {nullptr,0});
    }
    
    void add_computed_style(DOMNode* node, StringView property, StringView value) {
        if (!node) return;
        CSSComputedStyle* style = (CSSComputedStyle*)arena->allocate(sizeof(CSSComputedStyle));
        if(!style) return;
        style->property = property;
        style->value = value;
        style->next = node->first_computed_style;
        node->first_computed_style = style;
    }
    
    void update_box_model(DOMNode* node, const BoxModel& model) {
        if (!node) return;
        if (!node->box_model) {
            node->box_model = (BoxModel*)arena->allocate(sizeof(BoxModel));
        }
        if (node->box_model) {
            *node->box_model = model;
        }
    }

    void flush_mutations() {
        mutation_count = 0;
    }

    // ------------------------------------------------------------------------
    // Virtual DOM Diffing Engine
    // ------------------------------------------------------------------------
    void diff_attributes(DOMNode* old_node, DOMNode* new_node) {
        if (!old_node || !new_node) return;

        // 1. Remove attributes present in old_node but missing in new_node
        DOMAttribute* old_attr = old_node->first_attribute;
        while (old_attr) {
            bool found = false;
            DOMAttribute* new_attr = new_node->first_attribute;
            while (new_attr) {
                if (old_attr->name.equals(new_attr->name)) {
                    found = true;
                    break;
                }
                new_attr = new_attr->next;
            }
            if (!found) {
                remove_attribute(old_node, old_attr->name);
            }
            old_attr = old_attr->next;
        }

        // 2. Add or update attributes in new_node
        DOMAttribute* new_attr = new_node->first_attribute;
        while (new_attr) {
            bool matches = false;
            DOMAttribute* o_attr = old_node->first_attribute;
            while (o_attr) {
                if (o_attr->name.equals(new_attr->name)) {
                    if (o_attr->value.equals(new_attr->value)) {
                        matches = true;
                    }
                    break;
                }
                o_attr = o_attr->next;
            }
            if (!matches) {
                set_attribute(old_node, new_attr->name, new_attr->value);
            }
            new_attr = new_attr->next;
        }
    }

    void diff_nodes(DOMNode* old_node, DOMNode* new_node) {
        if (!old_node || !new_node) return;

        // If node type or tag name differs, replacement needed
        if (old_node->type != new_node->type || !old_node->node_name.equals(new_node->node_name)) {
            if (old_node->parent) {
                remove_child(old_node->parent, old_node);
                insert_before(old_node->parent, new_node, nullptr);
            }
            return;
        }

        // 1. Diff text values
        if (old_node->type == NodeType::Text || old_node->type == NodeType::Comment) {
            if (!old_node->node_value.equals(new_node->node_value)) {
                set_node_value(old_node, new_node->node_value);
            }
        }

        // 2. Diff attributes
        diff_attributes(old_node, new_node);

        // 3. Diff children recursively
        DOMNode* old_child = old_node->first_child;
        DOMNode* new_child = new_node->first_child;

        while (old_child && new_child) {
            DOMNode* next_old = old_child->next_sibling;
            DOMNode* next_new = new_child->next_sibling;
            diff_nodes(old_child, new_child);
            old_child = next_old;
            new_child = next_new;
        }

        // Remove remaining old children
        while (old_child) {
            DOMNode* next_old = old_child->next_sibling;
            remove_child(old_node, old_child);
            old_child = next_old;
        }

        // Append remaining new children
        while (new_child) {
            DOMNode* next_new = new_child->next_sibling;
            insert_before(old_node, new_child, nullptr);
            new_child = next_new;
        }
    }

    void diff_trees(DOMNode* old_root, DOMNode* new_root) {
        if (!old_root || !new_root) return;
        diff_nodes(old_root, new_root);
    }

    // ------------------------------------------------------------------------
    // Tree Traversal & CSS Selector Matching
    // ------------------------------------------------------------------------
    
    // 1. Basic DFS Tree Walker
    void walk_tree(DOMNode* root, void(*callback)(DOMNode*, void*), void* user_data) {
        if (!root) return;
        callback(root, user_data);
        
        DOMNode* child = root->first_child;
        while (child) {
            walk_tree(child, callback, user_data);
            child = child->next_sibling;
        }
    }

    // 2. getElementById
    DOMNode* get_element_by_id(DOMNode* root, StringView id) {
        if (!root) return nullptr;
        
        if (root->type == NodeType::Element) {
            DOMAttribute* attr = root->first_attribute;
            while (attr) {
                if (attr->name.equals("id") && attr->value.equals(id)) {
                    return root;
                }
                attr = attr->next;
            }
        }
        
        DOMNode* child = root->first_child;
        while (child) {
            DOMNode* res = get_element_by_id(child, id);
            if (res) return res;
            child = child->next_sibling;
        }
        return nullptr;
    }

    // Helper for class name matching
    bool contains_class(StringView class_list, StringView class_name) {
        size_t i = 0;
        while (i < class_list.length) {
            while (i < class_list.length && class_list.data[i] == ' ') i++;
            if (i >= class_list.length) break;
            
            size_t start = i;
            while (i < class_list.length && class_list.data[i] != ' ') i++;
            size_t len = i - start;
            
            if (len == class_name.length) {
                bool match = true;
                for (size_t j = 0; j < len; j++) {
                    if (class_list.data[start + j] != class_name.data[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) return true;
            }
        }
        return false;
    }

    // 3. getElementsByClassName
    void get_elements_by_class_name(DOMNode* root, StringView class_name, DOMNode** result_array, size_t max_results, size_t& current_count) {
        if (!root || current_count >= max_results) return;

        if (root->type == NodeType::Element) {
            DOMAttribute* attr = root->first_attribute;
            while (attr) {
                if (attr->name.equals("class")) {
                    if (contains_class(attr->value, class_name)) {
                        result_array[current_count++] = root;
                    }
                    break; // only one class attribute per node
                }
                attr = attr->next;
            }
        }

        DOMNode* child = root->first_child;
        while (child) {
            get_elements_by_class_name(child, class_name, result_array, max_results, current_count);
            child = child->next_sibling;
        }
    }

    // 4. CSS Selector Matching (Simple: tag, .class, #id)
    bool matches_simple_selector(DOMNode* node, StringView selector) {
        if (selector.length == 0 || !node || node->type != NodeType::Element) return false;

        if (selector.data[0] == '#') {
            StringView id = { selector.data + 1, selector.length - 1 };
            DOMAttribute* attr = node->first_attribute;
            while (attr) {
                if (attr->name.equals("id") && attr->value.equals(id)) return true;
                attr = attr->next;
            }
            return false;
        } 
        else if (selector.data[0] == '.') {
            StringView cls = { selector.data + 1, selector.length - 1 };
            DOMAttribute* attr = node->first_attribute;
            while (attr) {
                if (attr->name.equals("class")) {
                    return contains_class(attr->value, cls);
                }
                attr = attr->next;
            }
            return false;
        }
        else {
            return node->node_name.equals(selector);
        }
    }

    // Advanced CSS matching: evaluates a split array of descendant selectors backwards
    // e.g. "div .container #btn" -> split into ["div", ".container", "#btn"]
    // We check if `node` matches "#btn", then its ancestor matches ".container", then "div".
    bool matches_complex_selector(DOMNode* node, StringView* selectors, size_t count) {
        if (count == 0 || !node) return false;
        
        // Node must match the last selector part
        if (!matches_simple_selector(node, selectors[count - 1])) {
            return false;
        }
        
        // If there's only one part, we matched it
        if (count == 1) return true;
        
        // Match ancestors for the remaining parts
        DOMNode* current_ancestor = node->parent;
        size_t current_selector_idx = count - 2;
        
        while (current_ancestor) {
            if (matches_simple_selector(current_ancestor, selectors[current_selector_idx])) {
                if (current_selector_idx == 0) return true; // all parts matched
                current_selector_idx--;
            }
            current_ancestor = current_ancestor->parent;
        }
        
        return false;
    }

    // Helper to parse a descendant selector like "div .container #btn"
    void query_selector_all_complex(DOMNode* root, StringView full_selector, DOMNode** result_array, size_t max_results, size_t& current_count) {
        if (!root || current_count >= max_results) return;

        // 1. Split selector by spaces (max 10 parts for simplicity)
        StringView parts[10];
        size_t part_count = 0;
        
        size_t i = 0;
        while (i < full_selector.length && part_count < 10) {
            while (i < full_selector.length && full_selector.data[i] == ' ') i++;
            if (i >= full_selector.length) break;
            
            size_t start = i;
            while (i < full_selector.length && full_selector.data[i] != ' ') i++;
            size_t len = i - start;
            
            parts[part_count++] = { full_selector.data + start, len };
        }
        
        if (part_count > 0) {
            // 2. Traverse tree and evaluate
            walk_and_match(root, parts, part_count, result_array, max_results, current_count);
        }
    }
    
    void walk_and_match(DOMNode* node, StringView* parts, size_t part_count, DOMNode** result_array, size_t max_results, size_t& current_count) {
        if (!node || current_count >= max_results) return;
        
        if (matches_complex_selector(node, parts, part_count)) {
            result_array[current_count++] = node;
        }
        
        DOMNode* child = node->first_child;
        while (child) {
            walk_and_match(child, parts, part_count, result_array, max_results, current_count);
            child = child->next_sibling;
        }
    }

    // 5. querySelectorAll (wrapper)
    void query_selector_all(DOMNode* root, StringView selector, DOMNode** result_array, size_t max_results, size_t& current_count) {
        query_selector_all_complex(root, selector, result_array, max_results, current_count);
    }
};
