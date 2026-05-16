#include "Mutex.h"

#if defined(LINUX_PORT) && !defined(_WIN32)

CNMMutex::CNMMutex(bool locked) {
    if (locked) {
        Lock();
    }
}

CNMMutex::~CNMMutex() {}

CNMMutex &CNMMutex::operator=(CNMMutex &M) {
    (void)M;
    return *this;
}

CNMMutex::CNMMutex(const CNMMutex &) {}

void CNMMutex::Lock() const { m_mutex.lock(); }

void CNMMutex::Unlock() const { m_mutex.unlock(); }

#else

CNMMutex::CNMMutex(bool Locked) {
    m_pMutex = NULL;
    pthread_mutex_t *lMutex = new pthread_mutex_t;
    if ((*lMutex = ::CreateMutex(0, 0, 0)) == 0) {
    } else
        m_pMutex = lMutex;

    if (Locked)
        Lock();
}

CNMMutex::~CNMMutex() {
    pthread_mutex_t *lMutex = (pthread_mutex_t *)m_pMutex;
    while (::CloseHandle(*lMutex) == 0) {
        Lock();
        Unlock();
    }

    delete m_pMutex;
    m_pMutex = NULL;
}

void CNMMutex::Lock() const {
    pthread_mutex_t *lMutex = (pthread_mutex_t *)m_pMutex;
    if (::WaitForSingleObject(*lMutex, INFINITE) != WAIT_OBJECT_0) {
    }
}

void CNMMutex::Unlock() const {
    pthread_mutex_t *lMutex = (pthread_mutex_t *)m_pMutex;
    if (::ReleaseMutex(*lMutex) == 0) {
    }
}

CNMMutex &CNMMutex::operator=(CNMMutex &M) {
    (void)M;
    return *this;
}

CNMMutex::CNMMutex(const CNMMutex &) {}

#endif
