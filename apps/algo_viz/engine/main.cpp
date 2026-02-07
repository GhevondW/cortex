/// @file main.cpp
/// @brief AlgoViz BST Visualizer — WASM engine
///
/// Contains: JS bridge functions, 8 tree algorithms, and the extern "C" API.
/// Pattern follows the proven Cortex WASM demos (sudoku_solver.cpp, fiber_workflow.cpp).

#include "bst.hpp"

#include <cortex/config.hpp>
#include <cortex/coroutine.hpp>

#include <iostream>
#include <memory>
#include <queue>
#include <string>

// =============================================================================
// JS Bridge Functions
// =============================================================================

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// clang-format off
EM_JS(void, js_tree_visit_node, (int node_id, int color), {
    if (typeof onTreeVisit === 'function') onTreeVisit(node_id, color);
});

EM_JS(void, js_tree_unvisit_all, (), {
    if (typeof onTreeUnvisitAll === 'function') onTreeUnvisitAll();
});

EM_JS(void, js_tree_add_node, (int node_id, int value, int parent_id, int is_left), {
    if (typeof onTreeAddNode === 'function') onTreeAddNode(node_id, value, parent_id, is_left);
});

EM_JS(void, js_tree_remove_node, (int node_id), {
    if (typeof onTreeRemoveNode === 'function') onTreeRemoveNode(node_id);
});

EM_JS(void, js_tree_update_value, (int node_id, int new_value), {
    if (typeof onTreeUpdateValue === 'function') onTreeUpdateValue(node_id, new_value);
});

EM_JS(void, js_tree_highlight_edge, (int from_id, int to_id, int color), {
    if (typeof onTreeHighlightEdge === 'function') onTreeHighlightEdge(from_id, to_id, color);
});

EM_JS(void, js_tree_unhighlight_edges, (), {
    if (typeof onTreeUnhighlightEdges === 'function') onTreeUnhighlightEdges();
});

EM_JS(void, js_tree_log, (const char* msg), {
    if (typeof onTreeLog === 'function') onTreeLog(UTF8ToString(msg));
});

EM_JS(void, js_tree_append_result, (int value), {
    if (typeof onTreeAppendResult === 'function') onTreeAppendResult(value);
});

EM_JS(void, js_tree_clear_result, (), {
    if (typeof onTreeClearResult === 'function') onTreeClearResult();
});

EM_JS(void, js_tree_done, (), {
    if (typeof onTreeDone === 'function') onTreeDone();
});
// clang-format on

#else
// Native stubs for compilation/testing
void js_tree_visit_node(int /*node_id*/, int /*color*/) {}
void js_tree_unvisit_all() {}
void js_tree_add_node(int /*node_id*/, int /*value*/, int /*parent_id*/, int /*is_left*/) {}
void js_tree_remove_node(int /*node_id*/) {}
void js_tree_update_value(int /*node_id*/, int /*new_value*/) {}
void js_tree_highlight_edge(int /*from_id*/, int /*to_id*/, int /*color*/) {}
void js_tree_unhighlight_edges() {}
void js_tree_log(const char* /*msg*/) {}
void js_tree_append_result(int /*value*/) {}
void js_tree_clear_result() {}
void js_tree_done() {}
#endif

// =============================================================================
// Color Constants (shared with JS via integer IDs)
// =============================================================================

namespace colors {
constexpr int kDefault = 0;
constexpr int kComparing = 1;
constexpr int kVisiting = 2;
constexpr int kFound = 3;
constexpr int kInserted = 4;
constexpr int kBacktrack = 5;
constexpr int kQueued = 6;
constexpr int kPath = 7;
constexpr int kDelete = 8;
} // namespace colors

// =============================================================================
// Helpers
// =============================================================================

namespace {

void log_msg(const std::string& msg) {
    js_tree_log(msg.c_str());
    std::cout << "[AlgoViz] " << msg << "\n";
}

/// Unlink a node from the tree. The node must have at most one child.
/// @param child_id The single child to link in place of del_id, or -1 if leaf.
void unlink_node(algoviz::BST& tree, int del_id, int child_id) {
    int parent = tree[del_id].parent;

    if (child_id != -1) {
        tree[child_id].parent = parent;
    }

    if (parent == -1) {
        tree.set_root(child_id);
    } else if (tree[parent].left == del_id) {
        tree[parent].left = child_id;
    } else {
        tree[parent].right = child_id;
    }
}

} // namespace

// =============================================================================
// Algorithm Implementations
// =============================================================================

namespace {

using Ctx = cortex::CoroutineSuspendContext;
using BST = algoviz::BST;

// ---------------------------------------------------------------------------
// BST Insert (visualized)
// ---------------------------------------------------------------------------
void algo_bst_insert(Ctx& ctx, BST& tree, int value) {
    log_msg("Inserting " + std::to_string(value));
    js_tree_clear_result();

    if (tree.root() == -1) {
        int new_id = tree.create_node(value);
        tree.set_root(new_id);
        js_tree_add_node(new_id, value, -1, 0);
        js_tree_visit_node(new_id, colors::kInserted);
        log_msg("Tree was empty. Inserted " + std::to_string(value) + " as root.");
        ctx.Suspend();
        js_tree_unvisit_all();
        js_tree_done();
        return;
    }

    int cur = tree.root();
    for (;;) {
        js_tree_visit_node(cur, colors::kComparing);
        log_msg("Comparing " + std::to_string(value) + " with " + std::to_string(tree[cur].value));
        ctx.Suspend();

        if (value == tree[cur].value) {
            // Duplicate — reject
            js_tree_visit_node(cur, colors::kFound);
            log_msg("Value " + std::to_string(value) + " already exists. Duplicate not inserted.");
            ctx.Suspend();
            js_tree_unvisit_all();
            js_tree_unhighlight_edges();
            js_tree_done();
            return;
        }

        if (value < tree[cur].value) {
            // Go left
            js_tree_visit_node(cur, colors::kPath);
            if (tree[cur].left == -1) {
                int new_id = tree.create_node(value);
                tree[cur].left = new_id;
                tree[new_id].parent = cur;
                js_tree_add_node(new_id, value, cur, 1);
                js_tree_visit_node(new_id, colors::kInserted);
                log_msg("Inserted " + std::to_string(value) + " as left child of " + std::to_string(tree[cur].value));
                ctx.Suspend();
                js_tree_unvisit_all();
                js_tree_unhighlight_edges();
                js_tree_done();
                return;
            }
            js_tree_highlight_edge(cur, tree[cur].left, colors::kPath);
            cur = tree[cur].left;
        } else {
            // Go right
            js_tree_visit_node(cur, colors::kPath);
            if (tree[cur].right == -1) {
                int new_id = tree.create_node(value);
                tree[cur].right = new_id;
                tree[new_id].parent = cur;
                js_tree_add_node(new_id, value, cur, 0);
                js_tree_visit_node(new_id, colors::kInserted);
                log_msg("Inserted " + std::to_string(value) + " as right child of " + std::to_string(tree[cur].value));
                ctx.Suspend();
                js_tree_unvisit_all();
                js_tree_unhighlight_edges();
                js_tree_done();
                return;
            }
            js_tree_highlight_edge(cur, tree[cur].right, colors::kPath);
            cur = tree[cur].right;
        }
    }
}

// ---------------------------------------------------------------------------
// BST Find (visualized)
// ---------------------------------------------------------------------------
void algo_bst_find(Ctx& ctx, BST& tree, int target) {
    log_msg("Searching for " + std::to_string(target));
    js_tree_clear_result();

    if (tree.root() == -1) {
        log_msg("Tree is empty. Not found.");
        ctx.Suspend();
        js_tree_done();
        return;
    }

    int cur = tree.root();
    while (cur != -1) {
        js_tree_visit_node(cur, colors::kComparing);
        log_msg("Comparing " + std::to_string(target) + " with " + std::to_string(tree[cur].value));
        ctx.Suspend();

        if (target == tree[cur].value) {
            js_tree_visit_node(cur, colors::kFound);
            log_msg("Found " + std::to_string(target) + "!");
            js_tree_append_result(target);
            ctx.Suspend();
            js_tree_unvisit_all();
            js_tree_unhighlight_edges();
            js_tree_done();
            return;
        }

        js_tree_visit_node(cur, colors::kPath);

        if (target < tree[cur].value) {
            log_msg(std::to_string(target) + " < " + std::to_string(tree[cur].value) + " → go left");
            if (tree[cur].left != -1) {
                js_tree_highlight_edge(cur, tree[cur].left, colors::kPath);
            }
            cur = tree[cur].left;
        } else {
            log_msg(std::to_string(target) + " > " + std::to_string(tree[cur].value) + " → go right");
            if (tree[cur].right != -1) {
                js_tree_highlight_edge(cur, tree[cur].right, colors::kPath);
            }
            cur = tree[cur].right;
        }
        ctx.Suspend();
    }

    log_msg(std::to_string(target) + " not found in tree.");
    ctx.Suspend();
    js_tree_unvisit_all();
    js_tree_unhighlight_edges();
    js_tree_done();
}

// ---------------------------------------------------------------------------
// BST Delete (visualized)
// ---------------------------------------------------------------------------
void algo_bst_delete(Ctx& ctx, BST& tree, int target) {
    log_msg("Deleting " + std::to_string(target));
    js_tree_clear_result();

    if (tree.root() == -1) {
        log_msg("Tree is empty. Nothing to delete.");
        ctx.Suspend();
        js_tree_done();
        return;
    }

    // Phase 1: Find the node
    int cur = tree.root();
    while (cur != -1 && tree[cur].value != target) {
        js_tree_visit_node(cur, colors::kComparing);
        log_msg("Comparing " + std::to_string(target) + " with " + std::to_string(tree[cur].value));
        ctx.Suspend();
        js_tree_visit_node(cur, colors::kPath);

        if (target < tree[cur].value) {
            if (tree[cur].left != -1) {
                js_tree_highlight_edge(cur, tree[cur].left, colors::kPath);
            }
            cur = tree[cur].left;
        } else {
            if (tree[cur].right != -1) {
                js_tree_highlight_edge(cur, tree[cur].right, colors::kPath);
            }
            cur = tree[cur].right;
        }
    }

    if (cur == -1) {
        log_msg(std::to_string(target) + " not found. Nothing to delete.");
        ctx.Suspend();
        js_tree_unvisit_all();
        js_tree_unhighlight_edges();
        js_tree_done();
        return;
    }

    js_tree_visit_node(cur, colors::kDelete);
    log_msg("Found " + std::to_string(target) + ". Determining delete case...");
    ctx.Suspend();

    int left = tree[cur].left;
    int right = tree[cur].right;

    if (left == -1 && right == -1) {
        // Case 1: Leaf node
        log_msg("Case 1: Leaf node. Removing directly.");
        ctx.Suspend();

        unlink_node(tree, cur, -1);
        js_tree_remove_node(cur);
        log_msg("Deleted " + std::to_string(target) + ".");
        ctx.Suspend();

    } else if (left == -1 || right == -1) {
        // Case 2: One child
        int child = (left != -1) ? left : right;
        js_tree_visit_node(child, colors::kFound);
        log_msg("Case 2: One child. Replacing with " + std::to_string(tree[child].value) + ".");
        ctx.Suspend();

        unlink_node(tree, cur, child);
        js_tree_remove_node(cur);
        log_msg("Deleted " + std::to_string(target) + ".");
        ctx.Suspend();

    } else {
        // Case 3: Two children — find in-order successor
        log_msg("Case 3: Two children. Finding in-order successor...");
        ctx.Suspend();

        int succ = right;
        js_tree_visit_node(succ, colors::kQueued);
        ctx.Suspend();

        while (tree[succ].left != -1) {
            js_tree_visit_node(succ, colors::kPath);
            succ = tree[succ].left;
            js_tree_visit_node(succ, colors::kQueued);
            log_msg("Going left to find smallest...");
            ctx.Suspend();
        }

        js_tree_visit_node(succ, colors::kFound);
        log_msg("Successor: " + std::to_string(tree[succ].value) + ". Swapping values.");
        ctx.Suspend();

        // Copy successor's value into the node we're "deleting"
        int succ_value = tree[succ].value;
        tree[cur].value = succ_value;
        js_tree_update_value(cur, succ_value);
        js_tree_visit_node(cur, colors::kInserted);
        ctx.Suspend();

        // Delete the successor (has at most a right child)
        int succ_child = tree[succ].right;
        unlink_node(tree, succ, succ_child);
        js_tree_remove_node(succ);
        log_msg("Deleted successor node.");
        ctx.Suspend();
    }

    js_tree_unvisit_all();
    js_tree_unhighlight_edges();
    js_tree_done();
}

// ---------------------------------------------------------------------------
// In-order Traversal (Left, Visit, Right) — recursive
// ---------------------------------------------------------------------------
void inorder_impl(Ctx& ctx, BST& tree, int id) {
    if (id == -1) return;

    // Go left
    if (tree[id].left != -1) {
        js_tree_highlight_edge(id, tree[id].left, colors::kPath);
    }
    inorder_impl(ctx, tree, tree[id].left);

    // Visit
    js_tree_visit_node(id, colors::kVisiting);
    js_tree_append_result(tree[id].value);
    log_msg("Visit " + std::to_string(tree[id].value));
    ctx.Suspend();
    js_tree_visit_node(id, colors::kPath);

    // Go right
    if (tree[id].right != -1) {
        js_tree_highlight_edge(id, tree[id].right, colors::kPath);
    }
    inorder_impl(ctx, tree, tree[id].right);
}

void algo_inorder(Ctx& ctx, BST& tree) {
    log_msg("In-order Traversal (Left → Root → Right)");
    js_tree_clear_result();
    ctx.Suspend();

    if (tree.root() == -1) {
        log_msg("Tree is empty.");
    } else {
        inorder_impl(ctx, tree, tree.root());
        log_msg("In-order traversal complete!");
    }
    js_tree_unvisit_all();
    js_tree_unhighlight_edges();
    js_tree_done();
}

// ---------------------------------------------------------------------------
// Pre-order Traversal (Visit, Left, Right) — recursive
// ---------------------------------------------------------------------------
void preorder_impl(Ctx& ctx, BST& tree, int id) {
    if (id == -1) return;

    // Visit
    js_tree_visit_node(id, colors::kVisiting);
    js_tree_append_result(tree[id].value);
    log_msg("Visit " + std::to_string(tree[id].value));
    ctx.Suspend();
    js_tree_visit_node(id, colors::kPath);

    // Go left
    if (tree[id].left != -1) {
        js_tree_highlight_edge(id, tree[id].left, colors::kPath);
    }
    preorder_impl(ctx, tree, tree[id].left);

    // Go right
    if (tree[id].right != -1) {
        js_tree_highlight_edge(id, tree[id].right, colors::kPath);
    }
    preorder_impl(ctx, tree, tree[id].right);
}

void algo_preorder(Ctx& ctx, BST& tree) {
    log_msg("Pre-order Traversal (Root → Left → Right)");
    js_tree_clear_result();
    ctx.Suspend();

    if (tree.root() == -1) {
        log_msg("Tree is empty.");
    } else {
        preorder_impl(ctx, tree, tree.root());
        log_msg("Pre-order traversal complete!");
    }
    js_tree_unvisit_all();
    js_tree_unhighlight_edges();
    js_tree_done();
}

// ---------------------------------------------------------------------------
// Post-order Traversal (Left, Right, Visit) — recursive
// ---------------------------------------------------------------------------
void postorder_impl(Ctx& ctx, BST& tree, int id) {
    if (id == -1) return;

    // Go left
    if (tree[id].left != -1) {
        js_tree_highlight_edge(id, tree[id].left, colors::kPath);
    }
    postorder_impl(ctx, tree, tree[id].left);

    // Go right
    if (tree[id].right != -1) {
        js_tree_highlight_edge(id, tree[id].right, colors::kPath);
    }
    postorder_impl(ctx, tree, tree[id].right);

    // Visit
    js_tree_visit_node(id, colors::kVisiting);
    js_tree_append_result(tree[id].value);
    log_msg("Visit " + std::to_string(tree[id].value));
    ctx.Suspend();
    js_tree_visit_node(id, colors::kPath);
}

void algo_postorder(Ctx& ctx, BST& tree) {
    log_msg("Post-order Traversal (Left → Right → Root)");
    js_tree_clear_result();
    ctx.Suspend();

    if (tree.root() == -1) {
        log_msg("Tree is empty.");
    } else {
        postorder_impl(ctx, tree, tree.root());
        log_msg("Post-order traversal complete!");
    }
    js_tree_unvisit_all();
    js_tree_unhighlight_edges();
    js_tree_done();
}

// ---------------------------------------------------------------------------
// BFS (Level-order Traversal) — iterative with queue
// ---------------------------------------------------------------------------
void algo_bfs(Ctx& ctx, BST& tree) {
    log_msg("BFS — Level-order Traversal");
    js_tree_clear_result();
    ctx.Suspend();

    if (tree.root() == -1) {
        log_msg("Tree is empty.");
        js_tree_done();
        return;
    }

    std::queue<int> q;
    q.push(tree.root());
    js_tree_visit_node(tree.root(), colors::kQueued);
    ctx.Suspend();

    while (!q.empty()) {
        int id = q.front();
        q.pop();

        js_tree_visit_node(id, colors::kVisiting);
        js_tree_append_result(tree[id].value);
        log_msg("Visit " + std::to_string(tree[id].value));
        ctx.Suspend();

        js_tree_visit_node(id, colors::kPath);

        if (tree[id].left != -1) {
            q.push(tree[id].left);
            js_tree_visit_node(tree[id].left, colors::kQueued);
            js_tree_highlight_edge(id, tree[id].left, colors::kQueued);
        }
        if (tree[id].right != -1) {
            q.push(tree[id].right);
            js_tree_visit_node(tree[id].right, colors::kQueued);
            js_tree_highlight_edge(id, tree[id].right, colors::kQueued);
        }
        ctx.Suspend();
    }

    log_msg("BFS traversal complete!");
    js_tree_unvisit_all();
    js_tree_unhighlight_edges();
    js_tree_done();
}

// ---------------------------------------------------------------------------
// DFS (Depth-first Traversal) — recursive, pre-order style
// ---------------------------------------------------------------------------
void dfs_impl(Ctx& ctx, BST& tree, int id) {
    if (id == -1) return;

    js_tree_visit_node(id, colors::kVisiting);
    js_tree_append_result(tree[id].value);
    log_msg("Visit " + std::to_string(tree[id].value));
    ctx.Suspend();

    // Go left
    if (tree[id].left != -1) {
        js_tree_highlight_edge(id, tree[id].left, colors::kPath);
        dfs_impl(ctx, tree, tree[id].left);
    }

    // Go right
    if (tree[id].right != -1) {
        js_tree_highlight_edge(id, tree[id].right, colors::kPath);
        dfs_impl(ctx, tree, tree[id].right);
    }

    // Backtrack
    js_tree_visit_node(id, colors::kBacktrack);
    log_msg("Backtrack from " + std::to_string(tree[id].value));
    ctx.Suspend();
    js_tree_visit_node(id, colors::kPath);
}

void algo_dfs(Ctx& ctx, BST& tree) {
    log_msg("DFS — Depth-first Traversal");
    js_tree_clear_result();
    ctx.Suspend();

    if (tree.root() == -1) {
        log_msg("Tree is empty.");
    } else {
        dfs_impl(ctx, tree, tree.root());
        log_msg("DFS traversal complete!");
    }
    js_tree_unvisit_all();
    js_tree_unhighlight_edges();
    js_tree_done();
}

} // namespace

// =============================================================================
// Global State
// =============================================================================

namespace {

algoviz::BST g_tree;
std::unique_ptr<cortex::Coroutine> g_coro;
int g_operation_value = 0;

} // namespace

// =============================================================================
// WASM API (extern "C")
// =============================================================================

extern "C" {

// --- Tree building (no visualization) ---

CORTEX_API void build_preset_tree(int preset_id) {
    if (g_coro && !g_coro->IsDone()) return;
    g_tree.clear();
    switch (preset_id) {
    case 0: // Balanced (7 nodes)
        g_tree.insert(50);
        g_tree.insert(30);
        g_tree.insert(70);
        g_tree.insert(20);
        g_tree.insert(40);
        g_tree.insert(60);
        g_tree.insert(80);
        break;
    case 1: // Skewed left
        g_tree.insert(80);
        g_tree.insert(70);
        g_tree.insert(60);
        g_tree.insert(50);
        g_tree.insert(40);
        break;
    case 2: // Skewed right
        g_tree.insert(10);
        g_tree.insert(20);
        g_tree.insert(30);
        g_tree.insert(40);
        g_tree.insert(50);
        break;
    case 3: // Larger (10 nodes)
        for (int v : {45, 25, 65, 15, 35, 55, 75, 10, 30, 50}) {
            g_tree.insert(v);
        }
        break;
    default:
        break;
    }
    std::cout << "[AlgoViz] Preset " << preset_id << " loaded (" << g_tree.node_count() << " nodes)\n";
}

CORTEX_API void add_node(int value) {
    if (g_coro && !g_coro->IsDone()) return;
    g_tree.insert(value);
}

CORTEX_API void clear_tree() {
    g_coro.reset();
    g_tree.clear();
}

// --- Algorithm execution ---

CORTEX_API void set_operation_value(int value) {
    g_operation_value = value;
}

CORTEX_API void start_algorithm(int algo_id) {
    g_coro.reset();
    int value = g_operation_value;

    g_coro = std::make_unique<cortex::Coroutine>(
        cortex::Coroutine::Make([algo_id, value](cortex::CoroutineSuspendContext& ctx) {
            switch (algo_id) {
            case 0:
                algo_bst_insert(ctx, g_tree, value);
                break;
            case 1:
                algo_bst_delete(ctx, g_tree, value);
                break;
            case 2:
                algo_bst_find(ctx, g_tree, value);
                break;
            case 3:
                algo_inorder(ctx, g_tree);
                break;
            case 4:
                algo_preorder(ctx, g_tree);
                break;
            case 5:
                algo_postorder(ctx, g_tree);
                break;
            case 6:
                algo_bfs(ctx, g_tree);
                break;
            case 7:
                algo_dfs(ctx, g_tree);
                break;
            default:
                break;
            }
        }));

    g_coro->Resume();
}

CORTEX_API void step_algorithm() {
    if (g_coro && !g_coro->IsDone()) {
        g_coro->Resume();
    }
}

CORTEX_API int is_algorithm_done() {
    return (!g_coro || g_coro->IsDone()) ? 1 : 0;
}

CORTEX_API void reset_algorithm() {
    g_coro.reset();
}

// --- Tree query (for JS renderer) ---

CORTEX_API int get_root_id() {
    return g_tree.root();
}

CORTEX_API int get_node_count() {
    return g_tree.node_count();
}

CORTEX_API int get_node_value(int id) {
    return g_tree.valid(id) ? g_tree[id].value : -1;
}

CORTEX_API int get_node_left(int id) {
    return g_tree.valid(id) ? g_tree[id].left : -1;
}

CORTEX_API int get_node_right(int id) {
    return g_tree.valid(id) ? g_tree[id].right : -1;
}

CORTEX_API int get_node_parent(int id) {
    return g_tree.valid(id) ? g_tree[id].parent : -1;
}

} // extern "C"

int main() {
    std::cout << "AlgoViz BST Visualizer Ready\n";
    return 0;
}
