#if !defined(AFX_LOCK_H__5FD5B65A_35EB_11D2_83CE_00E02922FA40__INCLUDED_)
#define AFX_LOCK_H__5FD5B65A_35EB_11D2_83CE_00E02922FA40__INCLUDED_

#if defined(LINUX_PORT) && !defined(_WIN32)
#include "network/T4CLinuxCommPort.h"
#include <mutex>
#else
#include <windows.h>
#endif

#define MultiLock( __lock1, __lock2 ) (__lock1)->Lock();\
                        while( !(__lock2)->PickLock() ){\
                            (__lock1)->Unlock();\
                            Sleep(0);\
                            (__lock1)->Lock();\
                        }
#define MultiLock3( __lock1, __lock2, __lock3 ){\
    BOOL boFailed = TRUE;\
    while( boFailed ){\
        (__lock1)->Lock();\
        if( (__lock2)->PickLock() ){\
            if( (__lock3)->PickLock() ){\
                boFailed = FALSE;\
            }else{\
                (__lock1)->Unlock();\
                (__lock2)->Unlock();\
                boFailed = TRUE;\
            }\
        }else{\
            (__lock1)->Unlock();\
            boFailed = TRUE;\
        }\
    }\
}

// Time to Sleep when a MultiLock resource cannot be acquired right-away.
#define MULTI_LOCK_TIME_SLICE   0

class CLock  
{
public:
#if defined(LINUX_PORT) && !defined(_WIN32)
    CLock() = default;
    virtual ~CLock() = default;

    void Lock( void ){    
        csThreadLock.lock();
    };

    void Unlock( void ){    
        csThreadLock.unlock();
    }
    
    BOOL PickLock( void ){
        return csThreadLock.try_lock() ? TRUE : FALSE;
    }

private:
    std::recursive_mutex csThreadLock;

#else
    CLock(){ 
        InitializeCriticalSection( &csThreadLock ); 
    };
    virtual ~CLock(){
        DeleteCriticalSection( &csThreadLock );
    };

    //////////////////////////////////////////////////////////////////////////////////////////
    void Lock( void ){    
        EnterCriticalSection( &csThreadLock );  
    };
    //////////////////////////////////////////////////////////////////////////////////////////    

    void Unlock( void ){    
        LeaveCriticalSection( &csThreadLock );
    }
    
// Picklock only works on WinNT 4.0 or more.
#if( _WIN32_WINNT >= 0x400 )    
    //////////////////////////////////////////////////////////////////////////////////////////       
    BOOL PickLock( void ){
        return TryEnterCriticalSection( &csThreadLock );
    }
#endif

private:
    CRITICAL_SECTION csThreadLock;

#endif

};

class CAutoLock{
public:
    CAutoLock( CLock *theLock ){
        lpLock = theLock;
        lpLock->Lock();
    }
    ~CAutoLock(){
        lpLock->Unlock();
    }
private:
    CLock *lpLock;
};

typedef CLock *LPPCLock[];

#endif // !defined(AFX_LOCK_H__5FD5B65A_35EB_11D2_83CE_00E02922FA40__INCLUDED_)
