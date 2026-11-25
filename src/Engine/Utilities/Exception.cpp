#include "Engine/Utilities/Exception.hpp"

namespace NextStackWalk
{
    namespace
    {
        StackWalkFn GStackWalkHandler = nullptr;
    }

    void SetStackWalkHandler(StackWalkFn handler)
    {
        GStackWalkHandler = handler;
    }

    void PrintStack()
    {
        if (GStackWalkHandler != nullptr)
        {
            GStackWalkHandler();
        }
    }
}
