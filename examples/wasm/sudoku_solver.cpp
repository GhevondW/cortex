#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include <cortex/config.hpp>
#include <cortex/coroutine.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Bridge functions to communicate with JavaScript
// clang-format off
EM_JS(void, js_update_cell, (int row, int col, int value, int is_backtrack), {
    if (typeof updateCell === 'function') {
        updateCell(row, col, value, is_backtrack);
    }
});

EM_JS(void, js_update_stats, (int attempts, int backtracks), {
    if (typeof updateStats === 'function') {
        updateStats(attempts, backtracks);
    }
});

EM_JS(void, js_solving_complete, (double elapsed_ms, int success), {
    if (typeof onSolvingComplete === 'function') {
        onSolvingComplete(elapsed_ms, success);
    }
});

EM_JS(void, js_update_status, (const char* msg), {
    if (typeof updateStatus === 'function') {
        updateStatus(UTF8ToString(msg));
    }
});
// clang-format on
#endif

namespace {

constexpr int GRID_SIZE = 9;
constexpr int SUBGRID_SIZE = 3;

// Current sudoku board state
std::array<std::array<int, GRID_SIZE>, GRID_SIZE> board;
std::array<std::array<bool, GRID_SIZE>, GRID_SIZE> is_original; // Track original cells

// Statistics
int solve_attempts = 0;
int backtrack_count = 0;
std::chrono::steady_clock::time_point start_time;

// Solver coroutine
std::unique_ptr<cortex::Coroutine> solver_coro;

// Check if placing value at (row, col) is valid
bool is_valid(int row, int col, int value) {
    const auto urow = static_cast<std::size_t>(row);
    const auto ucol = static_cast<std::size_t>(col);

    // Check row
    for (std::size_t c = 0; c < GRID_SIZE; ++c) {
        if (c != ucol && board[urow][c] == value) {
            return false;
        }
    }

    // Check column
    for (std::size_t r = 0; r < GRID_SIZE; ++r) {
        if (r != urow && board[r][ucol] == value) {
            return false;
        }
    }

    // Check 3x3 subgrid
    const int subgrid_row = (row / SUBGRID_SIZE) * SUBGRID_SIZE;
    const int subgrid_col = (col / SUBGRID_SIZE) * SUBGRID_SIZE;
    for (int r = subgrid_row; r < subgrid_row + SUBGRID_SIZE; ++r) {
        for (int c = subgrid_col; c < subgrid_col + SUBGRID_SIZE; ++c) {
            const auto ur = static_cast<std::size_t>(r);
            const auto uc = static_cast<std::size_t>(c);
            if ((r != row || c != col) && board[ur][uc] == value) {
                return false;
            }
        }
    }

    return true;
}

// Recursive backtracking solver - this is the heart of the demo!
// Without stackful coroutines, visualizing this would require completely
// restructuring the algorithm into an iterative state machine.
bool solve_sudoku_recursive(cortex::CoroutineSuspendContext& ctx, int cell_index) {
    // Base case: all cells filled
    if (cell_index >= GRID_SIZE * GRID_SIZE) {
        return true;
    }

    const int row = cell_index / GRID_SIZE;
    const int col = cell_index % GRID_SIZE;
    const auto urow = static_cast<std::size_t>(row);
    const auto ucol = static_cast<std::size_t>(col);

    // Skip original cells
    if (is_original[urow][ucol]) {
        return solve_sudoku_recursive(ctx, cell_index + 1);
    }

    // Try each number 1-9
    for (int num = 1; num <= GRID_SIZE; ++num) {
        solve_attempts++;

        if (is_valid(row, col, num)) {
            // Place the number
            board[urow][ucol] = num;

#ifdef __EMSCRIPTEN__
            js_update_cell(row, col, num, 0); // 0 = not backtracking
            js_update_stats(solve_attempts, backtrack_count);
#endif

            // Suspend to visualize this step!
            // This is the magic - we can pause in the middle of recursion
            ctx.Suspend();

            // Recurse to next cell
            if (solve_sudoku_recursive(ctx, cell_index + 1)) {
                return true; // Solution found!
            }

            // Backtrack: this number didn't lead to a solution
            backtrack_count++;
            board[urow][ucol] = 0;

#ifdef __EMSCRIPTEN__
            js_update_cell(row, col, 0, 1); // 1 = backtracking
            js_update_stats(solve_attempts, backtrack_count);
#endif

            // Suspend to visualize the backtrack
            ctx.Suspend();
        }
    }

    // No valid number found, backtrack
    return false;
}

} // namespace

extern "C" {

CORTEX_API void set_sudoku_board(const char* board_string) {
    // Parse board string (81 characters: '0' = empty, '1'-'9' = filled)
    if (!board_string) {
        return;
    }

    for (int i = 0; i < GRID_SIZE * GRID_SIZE && board_string[i] != '\0'; ++i) {
        const int row = i / GRID_SIZE;
        const int col = i % GRID_SIZE;
        const auto urow = static_cast<std::size_t>(row);
        const auto ucol = static_cast<std::size_t>(col);
        const int value = board_string[i] - '0';

        board[urow][ucol] = value;
        is_original[urow][ucol] = (value != 0);
    }

    std::cout << "[C++] Board initialized\n";
}

CORTEX_API void start_solving() {
    solve_attempts = 0;
    backtrack_count = 0;
    start_time = std::chrono::steady_clock::now();

    std::cout << "[C++] Starting Sudoku solver with coroutines\n";

#ifdef __EMSCRIPTEN__
    js_update_status("Solving...");
#endif

    // Create the solver coroutine
    solver_coro = std::make_unique<cortex::Coroutine>(cortex::Coroutine::Make([](cortex::CoroutineSuspendContext& ctx) {
        bool success = solve_sudoku_recursive(ctx, 0);

        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        std::cout << "[C++] Solving complete! Success: " << success << ", Attempts: " << solve_attempts
                  << ", Backtracks: " << backtrack_count << ", Time: " << elapsed << "ms\n";

#ifdef __EMSCRIPTEN__
        js_solving_complete(static_cast<double>(elapsed), success ? 1 : 0);
#endif
    }));

    // Start the coroutine
    solver_coro->Resume();
}

CORTEX_API void resume_solving() {
    if (solver_coro && !solver_coro->IsDone()) {
        solver_coro->Resume();
    }
}

CORTEX_API int is_solving_done() {
    return (!solver_coro || solver_coro->IsDone()) ? 1 : 0;
}

CORTEX_API int get_cell_value(int row, int col) {
    if (row >= 0 && row < GRID_SIZE && col >= 0 && col < GRID_SIZE) {
        const auto urow = static_cast<std::size_t>(row);
        const auto ucol = static_cast<std::size_t>(col);
        return board[urow][ucol];
    }
    return 0;
}

CORTEX_API int is_cell_original(int row, int col) {
    if (row >= 0 && row < GRID_SIZE && col >= 0 && col < GRID_SIZE) {
        const auto urow = static_cast<std::size_t>(row);
        const auto ucol = static_cast<std::size_t>(col);
        return is_original[urow][ucol] ? 1 : 0;
    }
    return 0;
}

CORTEX_API void reset_solver() {
    solver_coro.reset();
    solve_attempts = 0;
    backtrack_count = 0;

    // Clear non-original cells
    for (std::size_t r = 0; r < GRID_SIZE; ++r) {
        for (std::size_t c = 0; c < GRID_SIZE; ++c) {
            if (!is_original[r][c]) {
                board[r][c] = 0;
            }
        }
    }

    std::cout << "[C++] Solver reset\n";
}

} // extern "C"

int main() {
    std::cout << "Cortex Sudoku Solver Demo Ready\n";
    return 0;
}
