/*
* Copyright (c) 2021 Pedro Falcato
* This file is part of Onyx, and is released under the terms of the MIT License
* check LICENSE at the root directory for more information
*/

#ifndef _ONYX_COMPLETION_H
#define _ONYX_COMPLETION_H

#include <onyx/wait_queue.h>

class completion
{
private:
    wait_queue wq_;
    bool done_;
public:
    constexpr completion() : done_{false}
    {
        init_wait_queue_head(&wq_);
    }

    ~completion()
    {
        assert(done_ != false);
    }

    bool is_done() const
    {
        return done_;
    }

    void wait()
    {
        wait_for_event(&wq_, is_done());
    }

    void signal()
    {
        done_ = true;
        wait_queue_wake_all(&wq_);
    }
};

#endif
