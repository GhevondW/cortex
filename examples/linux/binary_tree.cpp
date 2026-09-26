#include <cortex/generator.hpp>
#include <iostream>
#include <memory>
#include <vector>

struct Node {
    int value;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;

    explicit Node(int v)
        : value(v) {}
};

void insert(std::unique_ptr<Node>& root, int value) {
    if (!root) {
        root = std::make_unique<Node>(value);
        return;
    }
    if (value < root->value) {
        insert(root->left, value);
    } else {
        insert(root->right, value);
    }
}

// A plain recursive walk: the generator is stackful, so it can yield from any
// depth of recursion.
void traverse_in_order(Node* node, cortex::Generator<int>::YieldContext& yield) {
    if (!node) {
        return;
    }

    traverse_in_order(node->left.get(), yield);
    yield(node->value);
    traverse_in_order(node->right.get(), yield);
}

int main() {
    std::cout << "--- Cortex Binary Tree Traversal Example ---\n";

    std::unique_ptr<Node> root;
    std::vector<int> values = {50, 30, 70, 20, 40, 60, 80};

    std::cout << "Inserting values: ";
    for (int v : values) {
        std::cout << v << " ";
        insert(root, v);
    }
    std::cout << "\n\n";

    auto in_order = cortex::Generator<int>::Make([&](cortex::Generator<int>::YieldContext& yield) {
        traverse_in_order(root.get(), yield);
    });

    std::cout << "Traversing tree in-order using a generator:\n";
    int count = 0;
    for (int value : in_order) {
        std::cout << "Yielded value [" << ++count << "]: " << value << "\n";
    }

    std::cout << "\nTraversal complete!\n";
    return 0;
}
