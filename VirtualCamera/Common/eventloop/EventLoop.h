#ifndef __EVENT_LOOP_H__
#define __EVENT_LOOP_H__


typedef int (*event_callback)( void *userdata );

typedef struct stSocketHandle {
    int sock_fd;
    
    void *userdata;
    event_callback read_handle;
    event_callback write_handle;
}SocketHandle;

typedef struct stTimerHandle {
    double interval;
    int repeat;
    void *userdata;
    event_callback handle;
}TimerHandle;

typedef struct stEventLoop EventLoop;

#ifndef CAPI
#ifdef __cplusplus
#define CAPI extern "C"
#else
#define CAPI
#endif
#endif

CAPI EventLoop* EventLoop_Create();
CAPI void EventLoop_Destroy(EventLoop *loop);

CAPI int EventLoop_HandleSocket(EventLoop *loop, SocketHandle handle);
CAPI void EventLoop_RemoveSocket(EventLoop *loop, int sock_id);

CAPI int EventLoop_HandleTimer(EventLoop *loop, TimerHandle handle);
CAPI void EventLoop_RemoveTimer(EventLoop *loop, int timer_id);

CAPI void EventLoop_Run(EventLoop *loop);
CAPI void EventLoop_Quit(EventLoop *loop);

#endif /* __EVENT_LOOP_H__ */
