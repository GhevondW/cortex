#define BOOST_TEST_MODULE cortex_coroutine_test
#include <boost/test/included/unit_test.hpp>
#include <cortex/coroutine.hpp>

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

//////////////////////////////////////////////////////////////////////

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
    void run(F task) {
        std::thread t([task = std::move(task)]() mutable { task(); });
        t.join();
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(cortex_coroutine_test_suite)

BOOST_AUTO_TEST_CASE(just_works) {
    coroutine* ptr = nullptr;
    auto routine = coroutine::make_routine([&]() { ptr->suspend(); });
    auto co = coroutine::create(routine.get());
    ptr = &co;

    BOOST_CHECK_EQUAL(co.is_completed(), false);
    co.resume();
    BOOST_CHECK_EQUAL(co.is_completed(), false);
    co.resume();
    BOOST_CHECK_EQUAL(co.is_completed(), true);
}

BOOST_AUTO_TEST_CASE(interleaving) {
    int step = 0;

    coroutine* aptr = nullptr;
    coroutine* bptr = nullptr;

    auto a = coroutine::make_routine([&]() {
        BOOST_CHECK_EQUAL(step, 0);
        step = 1;
        aptr->suspend();
        BOOST_CHECK_EQUAL(step, 2);
        step = 3;
    });

    auto b = coroutine::make_routine([&]() {
        BOOST_CHECK_EQUAL(step, 1);
        step = 2;
        bptr->suspend();
        BOOST_CHECK_EQUAL(step, 3);
        step = 4;
    });

    coroutine acoro = coroutine::create(a.get());
    coroutine bcoro = coroutine::create(b.get());
    aptr = &acoro;
    bptr = &bcoro;

    acoro.resume();
    bcoro.resume();

    BOOST_CHECK_EQUAL(step, 2);

    acoro.resume();
    bcoro.resume();

    BOOST_CHECK_EQUAL(acoro.is_completed(), true);
    BOOST_CHECK_EQUAL(bcoro.is_completed(), true);

    BOOST_CHECK_EQUAL(step, 4);
}

BOOST_AUTO_TEST_CASE(threads_test) {
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

    BOOST_CHECK_EQUAL(steps, 3);
}

BOOST_AUTO_TEST_CASE(tree_walk) {
    auto root = TreeNode::Fork(
        "B",
        TreeNode::Leaf("A"),
        TreeNode::Fork("F", TreeNode::Fork("D", TreeNode::Leaf("C"), TreeNode::Leaf("E")), TreeNode::Leaf("G")));

    std::stringstream traversal;

    TreeIterator iter(root);
    while (iter.MoveNext()) {
        traversal << iter.Data();
    }

    BOOST_CHECK_EQUAL(traversal.str(), "ABCDEFG");
}

BOOST_AUTO_TEST_CASE(pipeline) {
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

    BOOST_CHECK_EQUAL(step_count, kSteps);
}

struct MyException : std::exception {};

BOOST_AUTO_TEST_CASE(exception) {
    coroutine* ptr = nullptr;
    auto routine = coroutine::basic_routine::make([&]() {
        ptr->suspend();
        throw MyException {};
        ptr->suspend();
    });

    auto co = coroutine::create(routine.get());
    ptr = &co;

    BOOST_CHECK_EQUAL(co.is_completed(), false);
    co.resume();
    BOOST_CHECK_THROW(co.resume(), MyException);
    BOOST_CHECK_EQUAL(co.is_completed(), true);
}

BOOST_AUTO_TEST_CASE(nested_exception1) {
    auto a_routine = coroutine::make_routine([&]() {
        auto b_routine = coroutine::make_routine([]() { throw MyException(); });
        coroutine b = coroutine::create(b_routine.get());
        BOOST_CHECK_THROW(b.resume(), MyException);
    });

    coroutine a = coroutine::create(a_routine.get());

    a.resume();
}

BOOST_AUTO_TEST_CASE(nested_exception2) {
    auto a_routine = coroutine::make_routine([&]() {
        auto b_routine = coroutine::make_routine([]() { throw MyException(); });
        coroutine b = coroutine::create(b_routine.get());
        b.resume();
    });

    coroutine a = coroutine::create(a_routine.get());

    BOOST_CHECK_THROW(a.resume(), MyException);

    BOOST_CHECK_EQUAL(a.is_completed(), true);
}

BOOST_AUTO_TEST_CASE(ExceptionsInThread) {
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

    BOOST_CHECK_EQUAL(score, 1);
}

//// TODO: fix test
// BOOST_AUTO_TEST_CASE(ExceptionsHard) {
//     int score = 0;
//
//     coroutine* ptr = nullptr;
//     auto a_routine = coroutine::make_routine([&]() {
//         auto b_routine = coroutine::make_routine([&]() { throw MyException(); });
//         coroutine b = coroutine::create(b_routine.get());
//         try {
//             b.resume();
//         } catch (const std::exception& exp) {
//             ++score;
//             // Context switch during stack unwinding
//             ptr->suspend();
//             throw;
//         }
//     });
//
//     coroutine a = coroutine::create(a_routine.get());
//     ptr = &a;
//
//     a.resume();
//
//     std::thread t([&] {
//         try {
//             a.resume();
//         } catch (const std::exception& exp) {
//             ++score;
//         }
//     });
//     t.join();
//
//     BOOST_CHECK_EQUAL(score, 2);
// }

BOOST_AUTO_TEST_CASE(sample_not_started) {
    int shared_counter = 0;
    auto routine = coroutine::make_routine([&shared_counter]() { shared_counter = 1; });
    auto co = coroutine::create(routine.get());
    BOOST_CHECK_EQUAL(shared_counter, 0);
}

BOOST_AUTO_TEST_CASE(sample_started_but_not_ended) {
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
    BOOST_CHECK_EQUAL(shared_counter, 1);
}

// Stack unwinding does not work in sure, so always end the routine.
BOOST_AUTO_TEST_CASE(sample_started_but_not_ended_stack_unwinding) {
    struct lifetime {
        lifetime(int& r)
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
        BOOST_CHECK_EQUAL(shared_counter, 1);
    }

    BOOST_CHECK_EQUAL(shared_counter, 12);
}

BOOST_AUTO_TEST_SUITE_END()
