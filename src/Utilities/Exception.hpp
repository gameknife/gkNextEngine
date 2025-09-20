#pragma once
#include <stdexcept>
#include "Common/CoreMinimal.hpp"

namespace NextStackWalk
{
    void PrintStack();
}

template <class E>
[[noreturn]] void Throw(const E& e) noexcept(false)
{
    SPDLOG_ERROR("\nException: {}\n------------------", e.what());
    NextStackWalk::PrintStack();
    throw e;
}