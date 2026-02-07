#pragma once

/// @file bst.hpp
/// @brief Simple Binary Search Tree with a node pool for stable IDs.
/// Used by all tree visualization algorithms.

#include <cstddef>
#include <vector>

namespace algoviz {

/// A BST backed by a node pool. Nodes are identified by stable integer IDs
/// so the JS renderer can track them across operations.
class BST {
public:
    struct Node {
        int value = 0;
        int left = -1; ///< Left child ID (-1 = null)
        int right = -1; ///< Right child ID (-1 = null)
        int parent = -1; ///< Parent ID (-1 = root or detached)
    };

    /// Create a new detached node. Returns its stable ID.
    [[nodiscard]] int create_node(int value) {
        auto id = static_cast<int>(nodes_.size());
        nodes_.push_back(Node {value, -1, -1, -1});
        return id;
    }

    /// Standard BST insert without visualization. Returns new node ID.
    int insert(int value) {
        int id = create_node(value);
        if (root_ == -1) {
            root_ = id;
            return id;
        }
        int cur = root_;
        for (;;) {
            if (value < nodes_[idx(cur)].value) {
                if (nodes_[idx(cur)].left == -1) {
                    nodes_[idx(cur)].left = id;
                    nodes_[idx(id)].parent = cur;
                    return id;
                }
                cur = nodes_[idx(cur)].left;
            } else {
                if (nodes_[idx(cur)].right == -1) {
                    nodes_[idx(cur)].right = id;
                    nodes_[idx(id)].parent = cur;
                    return id;
                }
                cur = nodes_[idx(cur)].right;
            }
        }
    }

    /// Clear the entire tree.
    void clear() {
        nodes_.clear();
        root_ = -1;
    }

    [[nodiscard]] int root() const {
        return root_;
    }
    void set_root(int id) {
        root_ = id;
    }

    [[nodiscard]] int node_count() const {
        return static_cast<int>(nodes_.size());
    }

    [[nodiscard]] bool valid(int id) const {
        return id >= 0 && id < node_count();
    }

    Node& operator[](int id) {
        return nodes_[idx(id)];
    }
    const Node& operator[](int id) const {
        return nodes_[idx(id)];
    }

private:
    static std::size_t idx(int id) {
        return static_cast<std::size_t>(id);
    }

    std::vector<Node> nodes_;
    int root_ = -1;
};

} // namespace algoviz
