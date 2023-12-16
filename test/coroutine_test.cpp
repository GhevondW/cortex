#include <cortex/coroutine.hpp>
#include <gtest/gtest.h>
#include <sstream>
#include <thread>

using namespace cortex;

namespace {

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
        : routine_(coroutine::make_routine([this, root]() { TreeWalk(root); }))
        , walker_(coroutine::create(routine_.get())) {}

    bool MoveNext() {
        walker_.resume();
        return !walker_.is_completed();
    }

    std::string_view Data() const {
        return data_;
    }

private:
    void TreeWalk(TreeNodePtr node) {
        if (node->left) {
            TreeWalk(node->left);
        }

        data_ = node->data;
        walker_.suspend();

        if (node->right) {
            TreeWalk(node->right);
        }
    }

private:
    std::unique_ptr<coroutine::basic_routine> routine_;
    coroutine walker_;
    std::string_view data_;
};

struct Threads {
    template <typename F>
    static void run(F task) {
        std::thread t([task = std::move(task)]() mutable { task(); });
        t.join();
    }
};

} // namespace

TEST(CortexCoroutineTest, JustWorks) {
    coroutine* ptr = nullptr;
    auto routine = coroutine::make_routine([&]() { ptr->suspend(); });
    auto co = coroutine::create(routine.get());
    ptr = &co;

    EXPECT_EQ(co.is_completed(), false);
    co.resume();
    EXPECT_EQ(co.is_completed(), false);
    co.resume();
    EXPECT_EQ(co.is_completed(), true);
}

TEST(CortexCoroutineTest, Interleaving) {
    int step = 0;

    coroutine* aptr = nullptr;
    coroutine* bptr = nullptr;

    auto a = coroutine::make_routine([&]() {
        EXPECT_EQ(step, 0);
        step = 1;
        aptr->suspend();
        EXPECT_EQ(step, 2);
        step = 3;
    });

    auto b = coroutine::make_routine([&]() {
        EXPECT_EQ(step, 1);
        step = 2;
        bptr->suspend();
        EXPECT_EQ(step, 3);
        step = 4;
    });

    coroutine acoro = coroutine::create(a.get());
    coroutine bcoro = coroutine::create(b.get());
    aptr = &acoro;
    bptr = &bcoro;

    acoro.resume();
    bcoro.resume();

    EXPECT_EQ(step, 2);

    acoro.resume();
    bcoro.resume();

    EXPECT_EQ(acoro.is_completed(), true);
    EXPECT_EQ(bcoro.is_completed(), true);

    EXPECT_EQ(step, 4);
}

TEST(CortexCoroutineTest, ThreadsTest) {
    size_t steps = 0;

    coroutine* ptr = nullptr;
    auto routine = coroutine::make_routine([&]() {
        std::cout << "Step" << std::endl;
        ++steps;
        ptr->suspend();
        std::cout << "Step" << std::endl;
        ++steps;
        ptr->suspend();
        std::cout << "Step" << std::endl;
        ++steps;
    });
    auto co = coroutine::create(routine.get());
    ptr = &co;

    auto resume = [&co] { co.resume(); };

    // Simulate fiber running in thread pool
    Threads threads;
    threads.run(resume);
    threads.run(resume);
    threads.run(resume);

    EXPECT_EQ(steps, 3);
}

TEST(CortexCoroutineTest, TreeWalk) {
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

TEST(CortexCoroutineTest, Pipeline) {
    const size_t kSteps = 123;

    size_t step_count = 0;

    coroutine* a_ptr = nullptr;
    coroutine* b_ptr = nullptr;

    auto a_routine = coroutine::basic_routine::make([&]() {
        auto b_routine = coroutine::basic_routine::make([&]() {
            for (size_t i = 0; i < kSteps; ++i) {
                ++step_count;
                b_ptr->suspend();
            }
        });
        auto b = coroutine::create(b_routine.get());
        b_ptr = &b;

        while (!b.is_completed()) {
            b.resume();
            a_ptr->suspend();
        }
    });

    auto a = coroutine::create(a_routine.get());
    a_ptr = &a;

    while (!a.is_completed()) {
        a.resume();
    }

    EXPECT_EQ(step_count, kSteps);
}

struct MyException : std::exception {};

TEST(CortexCoroutineTest, Exception) {
    coroutine* ptr = nullptr;
    auto routine = coroutine::basic_routine::make([&]() {
        ptr->suspend();
        throw MyException {};
        ptr->suspend();
    });

    auto co = coroutine::create(routine.get());
    ptr = &co;

    EXPECT_EQ(co.is_completed(), false);
    EXPECT_THROW(co.resume(), MyException);
    EXPECT_EQ(co.is_completed(), true);
}

TEST(CortexCoroutineTest, NestedException1) {
    auto a_routine = coroutine::make_routine([&]() {
        auto b_routine = coroutine::make_routine([]() { throw MyException(); });
        coroutine b = coroutine::create(b_routine.get());
        EXPECT_THROW(b.resume(), MyException);
    });

    coroutine a = coroutine::create(a_routine.get());

    a.resume();
}

TEST(CortexCoroutineTest, NestedException2) {
    auto a_routine = coroutine::make_routine([&]() {
        auto b_routine = coroutine::make_routine([]() { throw MyException(); });
        coroutine b = coroutine::create(b_routine.get());
        b.resume();
    });

    coroutine a = coroutine::create(a_routine.get());

    EXPECT_THROW(a.resume(), MyException);

    EXPECT_EQ(a.is_completed(), true);
}

TEST(CortexCoroutineTest, ExceptionsInThread) {
    int score = 0;

    auto a_routine = coroutine::make_routine([&]() { throw MyException(); });
    coroutine a = coroutine::create(a_routine.get());

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

TEST(CortexCoroutineTest, SampleNotStarted) {
    int shared_counter = 0;
    auto routine = coroutine::make_routine([&shared_counter]() { shared_counter = 1; });
    auto co = coroutine::create(routine.get());
    EXPECT_EQ(shared_counter, 0);
}

TEST(CortexCoroutineTest, SampleStartedButNotEnded) {
    int shared_counter = 0;
    coroutine* ptr = nullptr;
    auto routine = coroutine::make_routine([&]() {
        shared_counter = 1;
        ptr->suspend();
        shared_counter = 2;
    });
    auto co = coroutine::create(routine.get());
    ptr = &co;

    co.resume();
    EXPECT_EQ(shared_counter, 1);
}

TEST(CortexCoroutineTest, SampleStartedButNotEndedStackUnwinding) {
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
        coroutine* ptr = nullptr;
        auto routine = coroutine::make_routine([&]() {
            shared_counter = 1;
            lifetime lt(shared_counter);
            ptr->suspend();
            shared_counter = 2;
        });

        auto co = coroutine::create(routine.get());
        ptr = &co;

        co.resume();
        EXPECT_EQ(shared_counter, 1);
    }

    EXPECT_EQ(shared_counter, 12);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
