#pragma once

union SDL_Event;

namespace Runtime::Agent
{
    class IAgentControlService
    {
    public:
        virtual ~IAgentControlService() = default;
        virtual bool IsRunning() const = 0;
        virtual void Pump() = 0;
        virtual void Stop() = 0;
        // True when the event must not reach the application, e.g. real keyboard or
        // mouse input while a person watches an agent run. Called for every SDL event.
        virtual bool ShouldDropEvent(const SDL_Event& event) const
        {
            (void)event;
            return false;
        }
    };
}
