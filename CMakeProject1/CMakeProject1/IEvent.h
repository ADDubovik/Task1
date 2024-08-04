#pragma once

class IEvent
{
public:
    virtual ~IEvent(void) = default;
    virtual void Process(void) = 0;
};
