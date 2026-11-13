// SPDX-License-Identifier: GPL-3.0-or-later
// OpenRevers — cancelling a long command. The runner gives the thread doing
// the work a flag; the long algorithms call checkCancel() between their
// stages, which stops them with Cancelled. Commands change the document only
// at the end, so a cancelled one leaves it as it was.
#pragma once

#include <atomic>

namespace orv {

/// Not a std::exception on purpose: the algorithms' own catch blocks for
/// errors must not swallow it on its way to the runner.
struct Cancelled {};

inline thread_local const std::atomic<bool>* tl_cancelFlag = nullptr;

/// Throws Cancelled if this thread's command was asked to stop.
inline void checkCancel() {
    if (tl_cancelFlag && tl_cancelFlag->load(std::memory_order_relaxed)) throw Cancelled();
}

/// Sets the flag checkCancel() reads on this thread, for one command.
class CancelScope {
public:
    explicit CancelScope(const std::atomic<bool>* flag) : prev_(tl_cancelFlag) { tl_cancelFlag = flag; }
    ~CancelScope() { tl_cancelFlag = prev_; }
    CancelScope(const CancelScope&) = delete;
    CancelScope& operator=(const CancelScope&) = delete;

private:
    const std::atomic<bool>* prev_;
};

}  // namespace orv
