#ifndef CSEMAPHORE_H
#define CSEMAPHORE_H

#if _MSC_VER > 1000
#pragma once
#endif

#if defined(LINUX_PORT) && !defined(_WIN32)

#include "network/T4CLinuxCommPort.h"
#include <chrono>
#include <condition_variable>
#include <mutex>

class CNMSemaphore {
public:
    CNMSemaphore(unsigned char count = 1, unsigned int waitsec = 0);
    virtual ~CNMSemaphore();

    int Wait();
    int Post();

private:
    long initCount;
    long WaitSec;
    long currentCount;
    std::mutex mtx;
    std::condition_variable cv;
};

#else

#include <windows.h>

class CNMSemaphore {
public:
    CNMSemaphore(unsigned char count = 1, unsigned int waitsec = 0);
    virtual ~CNMSemaphore();

    int Wait();
    int Post();

private:
    long initCount;
    long WaitSec;
    HANDLE semaphore;
    LPSECURITY_ATTRIBUTES sa;
};

#endif

#endif
