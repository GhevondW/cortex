#include <cortex/naive_coroutine.hpp>
#include <gtest/gtest.h>

#include <thread>

using namespace cortex;

struct TreeNode;

using TreeNodePtr = std::shared_ptr<TreeNode>;

struct TreeNode {
    TreeNodePtr left;
    TreeNodePtr right;
    std::string data;

    TreeNode(std::string d, TreeNodePtr l, TreeNodePtr r)
        : left(std::move(l))
        , right(std::move(r))
        , data(std::move(d)) {}

    static TreeNodePtr Fork(std::string data, TreeNodePtr left, TreeNodePtr right) {
        return std::make_shared<TreeNode>(std::move(data), std::move(left), std::move(right));
    }

    static TreeNodePtr Leaf(std::string data) {
        return std::make_shared<TreeNode>(std::move(data), nullptr, nullptr);
    }
};

class TreeIterator {
public:
    explicit TreeIterator(TreeNodePtr root)
        : walker_(naive_coroutine::create([this, root](api::suspendable& s) { TreeWalk(root, s); })) {}

    bool MoveNext() {
        walker_.resume();
        return !walker_.is_completed();
    }

    std::string_view Data() const {
        return data_;
    }

private:
    void TreeWalk(TreeNodePtr node, api::suspendable& s) {
        if (node->left) {
            TreeWalk(node->left, s);
        }

        data_ = node->data;
        s.suspend();

        if (node->right) {
            TreeWalk(node->right, s);
        }
    }

private:
    naive_coroutine walker_;
    std::string_view data_;
};

struct Threads {
    template <typename F>
    static void run(F task) {
        std::thread t([task = std::move(task)]() mutable { task(); });
        t.join();
    }
};

TEST(CortexNaiveCoroutineTest, JustWorks) {
    auto co = naive_coroutine::create([](api::suspendable& s) { s.suspend(); });

    EXPECT_EQ(co.is_completed(), false);
    co.resume();
    EXPECT_EQ(co.is_completed(), false);
    co.resume();
    EXPECT_EQ(co.is_completed(), true);
}

TEST(CortexNaiveCoroutineTest, Interleaving) {
    int step = 0;

    auto a = naive_coroutine::create([&](api::suspendable& s) {
        EXPECT_EQ(step, 0);
        step = 1;
        s.suspend();
        EXPECT_EQ(step, 2);
        step = 3;
    });

    auto b = naive_coroutine::create([&](api::suspendable& s) {
        EXPECT_EQ(step, 1);
        step = 2;
        s.suspend();
        EXPECT_EQ(step, 3);
        step = 4;
    });

    a.resume();
    b.resume();

    EXPECT_EQ(step, 2);

    a.resume();
    b.resume();

    EXPECT_EQ(a.is_completed(), true);
    EXPECT_EQ(b.is_completed(), true);

    EXPECT_EQ(step, 4);
}

TEST(CortexNaiveCoroutineTest, ThreadsTest) {
    size_t steps = 0;

    auto co = naive_coroutine::create([&steps](api::suspendable& s) {
        std::cout << "Step" << std::endl;
        ++steps;
        s.suspend();
        std::cout << "Step" << std::endl;
        ++steps;
        s.suspend();
        std::cout << "Step" << std::endl;
        ++steps;
    });

    auto resume = [&co] { co.resume(); };

    // Simulate fiber running in thread pool
    Threads threads;
    threads.run(resume);
    threads.run(resume);
    threads.run(resume);

    EXPECT_EQ(steps, 3);
}

TEST(CortexNaiveCoroutineTest, TreeWalk) {
    auto root = TreeNode::Fork(
        "B",
        TreeNode::Leaf("A"),
        TreeNode::Fork("F", TreeNode::Fork("D", TreeNode::Leaf("C"), TreeNode::Leaf("E")), TreeNode::Leaf("G")));

    std::stringstream traversal;

    TreeIterator iter(root);
    while (iter.MoveNext()) {
        traversal << iter.Data();
    }

    EXPECT_EQ(traversal.str(), "ABCDEFG");
}

TEST(CortexNaiveCoroutineTest, Pipeline) {
    const size_t kSteps = 123;

    size_t step_count = 0;

    auto a = naive_coroutine::create([&](api::suspendable& s) {
        auto b = naive_coroutine::create([&](api::suspendable& s) {
            for (size_t i = 0; i < kSteps; ++i) {
                ++step_count;
                s.suspend();
            }
        });

        while (!b.is_completed()) {
            b.resume();
            s.suspend();
        }
    });

    while (!a.is_completed()) {
        a.resume();
    }

    EXPECT_EQ(step_count, kSteps);
}

struct MyException : std::exception {};

TEST(CortexNaiveCoroutineTest, Exception) {
    auto co = naive_coroutine::create([&](api::suspendable& s) {
        s.suspend();
        throw MyException {};
        s.suspend();
    });

    EXPECT_EQ(co.is_completed(), false);
    co.resume();
    EXPECT_THROW(co.resume(), MyException);
    EXPECT_EQ(co.is_completed(), true);
}

TEST(CortexNaiveCoroutineTest, NestedException1) {
    naive_coroutine a = naive_coroutine::create([&](api::suspendable& s) {
        naive_coroutine b = naive_coroutine::create([](api::suspendable& s) { throw MyException(); });
        EXPECT_THROW(b.resume(), MyException);
    });

    a.resume();
}

TEST(CortexNaiveCoroutineTest, NestedException2) {
    naive_coroutine a = naive_coroutine::create([&](api::suspendable& s) {
        naive_coroutine b = naive_coroutine::create([](api::suspendable& s) { throw MyException(); });
        b.resume();
    });

    EXPECT_THROW(a.resume(), MyException);

    EXPECT_EQ(a.is_completed(), true);
}

TEST(CortexNaiveCoroutineTest, ExceptionsInThread) {
    int score = 0;

    naive_coroutine a = naive_coroutine::create([&](api::suspendable& s) { throw MyException(); });

    std::thread t([&] {
        try {
            a.resume();
        } catch (const std::exception& exp) {
            ++score;
        }
    });
    t.join();

    EXPECT_EQ(score, 1);
}

TEST(CortexNaiveCoroutineTest, MemoryLeak) {
    auto shared_ptr = std::make_shared<int>(42);
    std::weak_ptr<int> weak_ptr = shared_ptr;

    {
        auto co = naive_coroutine::create([ptr = std::move(shared_ptr)](api::suspendable& s) {

        });
        co.resume();
    }

    EXPECT_EQ(weak_ptr.expired(), true);
}

TEST(CortexNaiveCoroutineTest, SampleNotStarted) {
    int shared_counter = 0;
    auto co = naive_coroutine::create(([&shared_counter](api::suspendable& s) { shared_counter = 1; }));
    EXPECT_EQ(shared_counter, 0);
}

TEST(CortexNaiveCoroutineTest, SampleStartedButNotEnded) {
    int shared_counter = 0;
    auto co = naive_coroutine::create([&shared_counter](api::suspendable& s) {
        shared_counter = 1;
        s.suspend();
        shared_counter = 2;
    });
    co.resume();
    EXPECT_EQ(shared_counter, 1);
}

TEST(CortexNaiveCoroutineTest, SampleStartedButNotEndedStackUnwinding) {
    struct lifetime {
        explicit lifetime(int& r)
            : ref(r) {}
        ~lifetime() {
            ref = 12;
        }
        int& ref;
    };

    int shared_counter = 0;
    {
        auto co = naive_coroutine::create([&shared_counter](api::suspendable& s) {
            shared_counter = 1;
            lifetime lt(shared_counter);
            s.suspend();
            shared_counter = 2;
        });
        co.resume();
        EXPECT_EQ(shared_counter, 1);
    }

    EXPECT_EQ(shared_counter, 12);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
