#pragma once

/**
 * Remplace T4CLog / DeadlockDetector / ThreadMonitor du binaire serveur Windows
 * lorsque le client Linux est compilé sans USE_CLIENT_CONNECTION (protocole S).
 */

#define _LOG_DEBUG if (false) (void)(
#define LOG_DEBUG_LVL1 0
#define LOG_DEBUG_LVL4 0
#define LOG_CRIT_ERRORS 0
#define LOG_ , 0);

#define DEADLOCK_LOG_LOCK ((void)0);

#define START_DEADLOCK_DETECTION(__handle, __threadname) ((void)0)
#define STOP_DEADLOCK_DETECTION ((void)0);
#define ENTER_TIMEOUT ((void)0);
#define LEAVE_TIMEOUT ((void)0);
#define KEEP_ALIVE ((void)0);

class CAutoThreadMonitor {
   public:
    explicit CAutoThreadMonitor(const char *) {}
};
