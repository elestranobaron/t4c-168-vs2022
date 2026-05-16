#ifndef CMUTEX_H
#define CMUTEX_H

#if _MSC_VER > 1000
#pragma once
#endif

#if defined(LINUX_PORT) && !defined(_WIN32)

#include "network/T4CLinuxCommPort.h"
#include <mutex>

class CNMMutex {
protected:
    mutable std::mutex m_mutex;

public:
    CNMMutex(bool locked = false);
    virtual ~CNMMutex();

    CNMMutex &operator=(CNMMutex &M);
    CNMMutex(const CNMMutex &);

    void Lock() const;
    void Unlock() const;
};

class TempLock {
private:
    CNMMutex &Mutex;

public:
    TempLock(CNMMutex const &lMutex) : Mutex(const_cast<CNMMutex &>(lMutex)) { Mutex.Lock(); }

    virtual ~TempLock() { Mutex.Unlock(); }
};

#else

#include <windows.h>

typedef HANDLE pthread_mutex_t;

class CNMMutex {
protected:
    pthread_mutex_t *m_pMutex;

public:
    CNMMutex(bool locked = false);
    virtual ~CNMMutex();

    CNMMutex &operator=(CNMMutex &M);
    CNMMutex(const CNMMutex &);

    void Lock() const;
    void Unlock() const;
};

class TempLock {
private:
    CNMMutex &Mutex;

public:
    TempLock(CNMMutex const &lMutex) : Mutex(const_cast<CNMMutex &>(lMutex)) { Mutex.Lock(); }

    virtual ~TempLock() { Mutex.Unlock(); }
};

#endif

#endif
