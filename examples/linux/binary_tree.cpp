#include <cortex/coroutine.hpp>
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

void traverse_in_order(Node* node, cortex::CoroutineSuspendContext& ctx, int& out_value) {
    if (!node) {
        return;
    }

    traverse_in_order(node->left.get(), ctx, out_value);

    out_value = node->value;
    ctx.Suspend();

    traverse_in_order(node->right.get(), ctx, out_value);
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

    int current_yielded_value = 0;
    auto generator = cortex::Coroutine::Make([&](cortex::CoroutineSuspendContext& ctx) {
        traverse_in_order(root.get(), ctx, current_yielded_value);
    });

    std::cout << "Traversing tree in-order using coroutine:\n";
    int count = 0;
    while (!generator.IsDone()) {
        generator.Resume();

        if (!generator.IsDone()) {
            std::cout << "Yielded value [" << ++count << "]: " << current_yielded_value << "\n";
        }
    }

    std::cout << "\nTraversal complete!\n";
    return 0;
}
