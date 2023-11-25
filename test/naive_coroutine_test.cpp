#define BOOST_TEST_MODULE cortex_naive_coroutine_test
#include <boost/test/included/unit_test.hpp>
#include <cortex/naive_coroutine.hpp>

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
    void run(F task) {
        std::thread t([task = std::move(task)]() mutable { task(); });
        t.join();
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(cortex_naive_coroutine_test_suite)

BOOST_AUTO_TEST_CASE(just_works) {
    auto co = naive_coroutine::create([](api::suspendable& s) { s.suspend(); });

    BOOST_CHECK_EQUAL(co.is_completed(), false);
    co.resume();
    BOOST_CHECK_EQUAL(co.is_completed(), false);
    co.resume();
    BOOST_CHECK_EQUAL(co.is_completed(), true);
}

BOOST_AUTO_TEST_CASE(interleaving) {
    int step = 0;

    auto a = naive_coroutine::create([&](api::suspendable& s) {
        BOOST_CHECK_EQUAL(step, 0);
        step = 1;
        s.suspend();
        BOOST_CHECK_EQUAL(step, 2);
        step = 3;
    });

    auto b = naive_coroutine::create([&](api::suspendable& s) {
        BOOST_CHECK_EQUAL(step, 1);
        step = 2;
        s.suspend();
        BOOST_CHECK_EQUAL(step, 3);
        step = 4;
    });

    a.resume();
    b.resume();

    BOOST_CHECK_EQUAL(step, 2);

    a.resume();
    b.resume();

    BOOST_CHECK_EQUAL(a.is_completed(), true);
    BOOST_CHECK_EQUAL(b.is_completed(), true);

    BOOST_CHECK_EQUAL(step, 4);
}

BOOST_AUTO_TEST_CASE(threads_test) {
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

    BOOST_CHECK_EQUAL(step_count, kSteps);
}

struct MyException : std::exception {};

BOOST_AUTO_TEST_CASE(exception) {
    auto co = naive_coroutine::create([&](api::suspendable& s) {
        s.suspend();
        throw MyException {};
        s.suspend();
    });

    BOOST_CHECK_EQUAL(co.is_completed(), false);
    co.resume();
    BOOST_CHECK_THROW(co.resume(), MyException);
    BOOST_CHECK_EQUAL(co.is_completed(), true);
}

BOOST_AUTO_TEST_CASE(nested_exception1) {
    naive_coroutine a = naive_coroutine::create([&](api::suspendable& s) {
        naive_coroutine b = naive_coroutine::create([](api::suspendable& s) { throw MyException(); });
        BOOST_CHECK_THROW(b.resume(), MyException);
    });

    a.resume();
}

BOOST_AUTO_TEST_CASE(nested_exception2) {
    naive_coroutine a = naive_coroutine::create([&](api::suspendable& s) {
        naive_coroutine b = naive_coroutine::create([](api::suspendable& s) { throw MyException(); });
        b.resume();
    });

    BOOST_CHECK_THROW(a.resume(), MyException);

    BOOST_CHECK_EQUAL(a.is_completed(), true);
}

BOOST_AUTO_TEST_CASE(exceptions_in_thread) {
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

    BOOST_CHECK_EQUAL(score, 1);
}

// TODO: fix test
// BOOST_AUTO_TEST_CASE(exceptions_hard) {
//    int score = 0;
//
//    naive_coroutine a = naive_coroutine::create([&](api::suspendable& s) {
//        naive_coroutine b = naive_coroutine::create([](api::suspendable& s) { throw MyException(); });
//        try {
//            b.resume();
//        } catch (const std::exception& exp) {
//            ++score;
//            // Context switch during stack unwinding
//            s.suspend();
//            throw;
//        }
//    });
//
//    a.resume();
//
//    std::thread t([&] {
//        try {
//            a.resume();
//        } catch (const std::exception& exp) {
//            ++score;
//        }
//    });
//    t.join();
//
//    BOOST_CHECK_EQUAL(score, 2);
//}

BOOST_AUTO_TEST_CASE(memory_leak) {
    auto shared_ptr = std::make_shared<int>(42);
    std::weak_ptr<int> weak_ptr = shared_ptr;

    {
        auto co = naive_coroutine::create([ptr = std::move(shared_ptr)](api::suspendable& s) {

        });
        co.resume();
    }

    BOOST_CHECK_EQUAL(weak_ptr.expired(), true);
}

BOOST_AUTO_TEST_CASE(sample_not_started) {
    int shared_counter = 0;
    auto co = naive_coroutine::create(([&shared_counter](api::suspendable& s) { shared_counter = 1; }));
    BOOST_CHECK_EQUAL(shared_counter, 0);
}

BOOST_AUTO_TEST_CASE(sample_started_but_not_ended) {
    int shared_counter = 0;
    auto co = naive_coroutine::create([&shared_counter](api::suspendable& s) {
        shared_counter = 1;
        s.suspend();
        shared_counter = 2;
    });
    co.resume();
    BOOST_CHECK_EQUAL(shared_counter, 1);
}

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
        auto co = naive_coroutine::create([&shared_counter](api::suspendable& s) {
            shared_counter = 1;
            lifetime lt(shared_counter);
            s.suspend();
            shared_counter = 2;
        });
        co.resume();
        BOOST_CHECK_EQUAL(shared_counter, 1);
    }

    BOOST_CHECK_EQUAL(shared_counter, 12);
}

BOOST_AUTO_TEST_SUITE_END()
