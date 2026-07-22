#pragma once
#include "core.h"

enum class HeapNodeType {
    Hidden, Array, String, Object, Code, Closure, RegExp, HeapNumber, 
    Native, Synthetic, ConsString, SlicedString, Symbol, BigInt, ObjectShape,
    WasmObject, Unresolved
};

struct HeapNode {
    uint32_t id;
    HeapNodeType type;
    StringView name;
    size_t shallow_size;
    size_t retained_size;
    uint32_t distance_to_root;
    
    // Retainer Tree state
    uint32_t dominator_node_id; 
    struct RetainerEdge* first_retainer; // Edges coming INTO this node
    struct HeapEdge* first_outgoing;     // Edges going OUT of this node
};

enum class HeapEdgeType {
    ContextVariable, Element, Property, Internal, Hidden, Shortcut, Weak
};

struct HeapEdge {
    HeapEdgeType type;
    StringView name_or_index;
    uint32_t from_node_id;
    uint32_t to_node_id;
    HeapEdge* next_outgoing;
};

struct RetainerEdge {
    HeapEdge* original_edge;
    RetainerEdge* next_retainer;
};

struct AllocationProfileNode {
    uint32_t id;
    StringView function_name;
    StringView script_name;
    uint32_t script_id;
    uint32_t line_number;
    uint32_t column_number;
    size_t alloc_size;
    uint32_t alloc_count;
    AllocationProfileNode* parent;
    AllocationProfileNode* first_child;
    AllocationProfileNode* next_sibling;
};

class MemoryEngine {
private:
    ArenaAllocator* arena;
    
    HeapNode** node_map; // ID to Node mapping
    size_t map_capacity;
    
    HeapNode* nodes;
    size_t max_nodes;
    size_t node_count;

    HeapEdge* edges;
    size_t max_edges;
    size_t edge_count;
    
    AllocationProfileNode* first_alloc_node;

public:
    MemoryEngine(ArenaAllocator* alloc, size_t m_nodes = 250000, size_t m_edges = 1000000)
        : arena(alloc), max_nodes(m_nodes), max_edges(m_edges), node_count(0), edge_count(0), first_alloc_node(nullptr), map_capacity(500000) {
        nodes = (HeapNode*)arena->allocate(sizeof(HeapNode) * max_nodes);
        edges = (HeapEdge*)arena->allocate(sizeof(HeapEdge) * max_edges);
        node_map = (HeapNode**)arena->allocate(sizeof(HeapNode*) * map_capacity);
        if (node_map) {
            for(size_t i = 0; i < map_capacity; i++) node_map[i] = nullptr;
        }
    }

    HeapNode* get_node(uint32_t id) {
        if (!node_map || id >= map_capacity) return nullptr;
        return node_map[id];
    }

    void add_node(uint32_t id, HeapNodeType type, StringView name, size_t shallow, size_t retained, uint32_t distance) {
        if (!nodes || node_count >= max_nodes || id >= map_capacity) return;
        HeapNode* node = &nodes[node_count++];
        node->id = id;
        node->type = type;
        node->name = name;
        node->shallow_size = shallow;
        node->retained_size = retained;
        node->distance_to_root = distance;
        node->dominator_node_id = 0;
        node->first_retainer = nullptr;
        node->first_outgoing = nullptr;
        
        node_map[id] = node;
    }

    void add_edge(HeapEdgeType type, StringView name_or_index, uint32_t from, uint32_t to) {
        if (!edges || edge_count >= max_edges) return;
        HeapEdge* edge = &edges[edge_count++];
        edge->type = type;
        edge->name_or_index = name_or_index;
        edge->from_node_id = from;
        edge->to_node_id = to;
        
        HeapNode* from_node = get_node(from);
        HeapNode* to_node = get_node(to);
        
        if (from_node) {
            edge->next_outgoing = from_node->first_outgoing;
            from_node->first_outgoing = edge;
        }
        
        if (to_node) {
            RetainerEdge* ret = (RetainerEdge*)arena->allocate(sizeof(RetainerEdge));
            if (ret) {
                ret->original_edge = edge;
                ret->next_retainer = to_node->first_retainer;
                to_node->first_retainer = ret;
            }
        }
    }
    
    void compute_dominators_and_retained_sizes() {
        // 1. Reset dominators and retained sizes
        for (size_t i = 0; i < node_count; ++i) {
            nodes[i].dominator_node_id = 0;
            nodes[i].retained_size = nodes[i].shallow_size;
            nodes[i].distance_to_root = 0xFFFFFFFF; // Reset distances
        }

        if (node_count == 0) return;

        // Find root node (assume id == 1 is the synthetic GC root)
        HeapNode* root_node = get_node(1);
        if (!root_node) root_node = &nodes[0]; // fallback
        
        // 2. BFS to set accurate distance_to_root
        uint32_t* queue = (uint32_t*)arena->allocate(sizeof(uint32_t) * node_count);
        if (queue) {
            size_t head = 0;
            size_t tail = 0;
            
            root_node->distance_to_root = 0;
            queue[tail++] = root_node->id;
            
            while (head < tail) {
                uint32_t curr_id = queue[head++];
                HeapNode* curr = get_node(curr_id);
                if (!curr) continue;
                
                HeapEdge* edge = curr->first_outgoing;
                while (edge) {
                    HeapNode* to = get_node(edge->to_node_id);
                    if (to && to->distance_to_root == 0xFFFFFFFF) {
                        to->distance_to_root = curr->distance_to_root + 1;
                        queue[tail++] = to->id;
                    }
                    edge = edge->next_outgoing;
                }
            }
        }

        // 3. Iterative Dominator Algorithm (Cooper, Harvey, Kennedy)
        root_node->dominator_node_id = root_node->id; // self-dominates
        
        bool changed = true;
        int iterations = 0;
        
        while (changed && iterations < 20) { // cap iterations
            changed = false;
            iterations++;
            
            for (size_t i = 0; i < node_count; ++i) {
                HeapNode* node = &nodes[i];
                if (node == root_node || node->distance_to_root == 0xFFFFFFFF) continue;
                
                uint32_t new_idom = 0;
                
                // Find first processed predecessor
                RetainerEdge* ret = node->first_retainer;
                while (ret) {
                    if (ret->original_edge) {
                        HeapNode* pred = get_node(ret->original_edge->from_node_id);
                        if (pred && pred->dominator_node_id != 0) {
                            new_idom = pred->id;
                            break;
                        }
                    }
                    ret = ret->next_retainer;
                }
                
                // Intersect with other predecessors
                ret = node->first_retainer;
                while (ret) {
                    if (ret->original_edge) {
                        HeapNode* pred = get_node(ret->original_edge->from_node_id);
                        if (pred && pred->id != new_idom && pred->dominator_node_id != 0) {
                            new_idom = intersect_dominators(pred->id, new_idom);
                        }
                    }
                    ret = ret->next_retainer;
                }
                
                if (node->dominator_node_id != new_idom) {
                    node->dominator_node_id = new_idom;
                    changed = true;
                }
            }
        }

        // 4. Compute Retained Sizes (Bottom-Up)
        uint32_t max_dist = 0;
        for (size_t i = 0; i < node_count; ++i) {
            if (nodes[i].distance_to_root > max_dist && nodes[i].distance_to_root != 0xFFFFFFFF) {
                max_dist = nodes[i].distance_to_root;
            }
        }

        // Propagate sizes from children to their dominators
        for (int d = max_dist; d >= 0; --d) {
            for (size_t i = 0; i < node_count; ++i) {
                if (nodes[i].distance_to_root == (uint32_t)d) {
                    uint32_t dom_id = nodes[i].dominator_node_id;
                    if (dom_id != 0 && dom_id != nodes[i].id) {
                        HeapNode* dom_node = get_node(dom_id);
                        if (dom_node) {
                            dom_node->retained_size += nodes[i].retained_size;
                        }
                    }
                }
            }
        }
    }

    uint32_t intersect_dominators(uint32_t b1_id, uint32_t b2_id) {
        HeapNode* finger1 = get_node(b1_id);
        HeapNode* finger2 = get_node(b2_id);
        
        while (finger1 && finger2 && finger1->id != finger2->id) {
            while (finger1 && finger2 && finger1->distance_to_root > finger2->distance_to_root) {
                finger1 = get_node(finger1->dominator_node_id);
            }
            while (finger1 && finger2 && finger2->distance_to_root > finger1->distance_to_root) {
                finger2 = get_node(finger2->dominator_node_id);
            }
            if (finger1 && finger2 && finger1->distance_to_root == finger2->distance_to_root && finger1->id != finger2->id) {
                finger1 = get_node(finger1->dominator_node_id);
                finger2 = get_node(finger2->dominator_node_id);
            }
        }
        return finger1 ? finger1->id : 0;
    }

    AllocationProfileNode* add_allocation_profile_node(uint32_t id, StringView func_name, StringView script, uint32_t script_id, uint32_t line, uint32_t col) {
        AllocationProfileNode* node = (AllocationProfileNode*)arena->allocate(sizeof(AllocationProfileNode));
        if (!node) return nullptr;
        
        node->id = id;
        node->function_name = func_name;
        node->script_name = script;
        node->script_id = script_id;
        node->line_number = line;
        node->column_number = col;
        node->alloc_size = 0;
        node->alloc_count = 0;
        node->parent = node->first_child = node->next_sibling = nullptr;
        
        node->next_sibling = first_alloc_node;
        first_alloc_node = node;
        
        return node;
    }
    
    void add_allocation(AllocationProfileNode* node, size_t size, uint32_t count) {
        if (node) {
            node->alloc_size += size;
            node->alloc_count += count;
        }
    }

    size_t get_node_count() const { return node_count; }
    HeapNode* get_nodes_array() { return nodes; }
    
    size_t get_edge_count() const { return edge_count; }
    HeapEdge* get_edges_array() { return edges; }
};
