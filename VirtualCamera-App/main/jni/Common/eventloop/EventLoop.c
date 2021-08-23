#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "ev.h"
#include "EventLoop.h"

#define MAX_SOCKET_COUNT 64
#define MAX_TIMER_COUNT 64

struct stEventLoop {
    ev_io io_array[MAX_SOCKET_COUNT];
	SocketHandle sock_handles[MAX_SOCKET_COUNT];
    int current_io;

	ev_timer timer_array[MAX_TIMER_COUNT];
	TimerHandle timer_handles[MAX_TIMER_COUNT];
	int current_timer;
    
    ev_async async_signal;

	struct ev_loop *evLoop;
    int ev_count;
};

static void async_cb(EV_P_ ev_async *w, int revents) {
    ev_break(w->data, EVBREAK_ALL);
}


CAPI EventLoop* EventLoop_Create(){
	EventLoop *loop = NULL;
	int i, ok = 0;
	do {
		loop = (EventLoop *)malloc(sizeof(EventLoop));
		if(!loop) break;

		memset(loop, 0, sizeof(EventLoop));

		loop->evLoop = ev_loop_new(EVFLAG_AUTO);
		loop->current_io = 0;
		loop->current_timer = 0;

		//register watcher
		for (i=0; i<MAX_SOCKET_COUNT; i++) {
			loop->io_array[i].fd = -1;
		}
		for (i=0; i<MAX_TIMER_COUNT; i++) {
			loop->timer_array[i].repeat = -1;
		}
        
        loop->async_signal.data = loop->evLoop;
        ev_async_init(&loop->async_signal, async_cb);
        ev_async_start(loop->evLoop, &loop->async_signal);

        loop->ev_count = 0;
		ok = 1;
	} while (0);

	if (!ok) {
		return NULL;
	}

	return loop;
}

CAPI void EventLoop_Destroy(EventLoop *loop){
	if (loop) {
        if (loop->evLoop)
            ev_loop_destroy(loop->evLoop);
        loop->evLoop = NULL;
		free(loop);
	}
}

#pragma mark -- ev_io_handle
static void ev_io_handle(struct ev_loop *loop, ev_io *w, int revents) {
	SocketHandle *handle = (SocketHandle *)w->data;
	if ((revents & EV_READ) && handle->read_handle) {
        handle->read_handle(handle->userdata);
	}
    
	if ((revents & EV_WRITE) && handle->write_handle) {
        handle->write_handle(handle->userdata);
	}
}

CAPI int EventLoop_HandleSocket(EventLoop *loop, SocketHandle handle) {
	int id = -1, i, index = loop->current_io;
	for (i=0; i<MAX_SOCKET_COUNT; i++) {
		if (loop->io_array[index].fd < 0) {
			id = index;
			break;
		}

		index++;
		if (index >= MAX_SOCKET_COUNT)
			index = 0;
	}

	if (id < 0) {
		// no avaliable io
		return id;
	}

    loop->current_io = id + 1;
	if (loop->current_io >= MAX_SOCKET_COUNT)
		loop->current_io = 0;

	// insert io_array[id] into loop
	int flag = 0;
	if (handle.read_handle)
		flag |= EV_READ;
	if (handle.write_handle)
		flag |= EV_WRITE;

	loop->io_array[id].fd = handle.sock_fd;
	loop->sock_handles[id] = handle;
	loop->io_array[id].data = &loop->sock_handles[id];
	ev_io_init(&loop->io_array[index], ev_io_handle, handle.sock_fd, flag);
	ev_io_start(loop->evLoop, &loop->io_array[index]);
    loop->ev_count++;
	return id;
}

CAPI void EventLoop_RemoveSocket(EventLoop *loop, int sock_id) {
	if (sock_id < 0 || sock_id >= MAX_SOCKET_COUNT)
		return;

	// remove io_array[sock_id] from loop
	ev_io_stop(loop->evLoop, &loop->io_array[sock_id]);
    loop->io_array[sock_id].fd = -1;
    loop->ev_count--;
}

#pragma mark -- ev_timer_handle

static void ev_timer_handle(struct ev_loop *loop, ev_timer *timer_w, int e){
	TimerHandle *th = (TimerHandle *)timer_w->data;
    int r = th->handle(th->userdata);
    th->repeat--;
    if (th->repeat == 0) {
        ev_timer_stop(loop, timer_w);
        timer_w->repeat = -1;
    }
    
    if (r < 0) {
        ev_break(loop, EVBREAK_ALL);
    }
}

CAPI int EventLoop_HandleTimer(EventLoop *loop, TimerHandle handle){

	int id = -1, i, index = loop->current_timer;
	double after, interval;
	for (i=0; i<MAX_TIMER_COUNT; i++) {
		if (loop->timer_array[index].repeat <= 0) {
			id = index;
			break;
		}

		index++;
		if (index >= MAX_TIMER_COUNT)
			index = 0;
	}

	if (id < 0) {
		// no avaliable io
		return id;
	}

	loop->current_timer = id + 1;
	if (loop->current_timer >= MAX_TIMER_COUNT)
		loop->current_timer = 0;

	interval = handle.interval;
    after = interval;
	loop->timer_handles[id] = handle;
	loop->timer_array[id].data = &loop->timer_handles[id];

	ev_timer_init(&loop->timer_array[index], ev_timer_handle, after, interval);
	ev_timer_start(loop->evLoop, &loop->timer_array[index]);
    loop->ev_count++;
	return id;
}

CAPI void EventLoop_RemoveTimer(EventLoop *loop, int timer_id){
    if (timer_id < 0 || timer_id >= MAX_TIMER_COUNT)
        return;

	ev_timer_stop(loop->evLoop, &loop->timer_array[timer_id]);
    loop->timer_array[timer_id].repeat = -1.0;
    loop->ev_count--;
}

#pragma mark -- loop run and quit

CAPI void EventLoop_Run(EventLoop *loop){
	ev_run(loop->evLoop, 0);
}

CAPI void EventLoop_Quit(EventLoop *loop){
    ev_async_send(loop->evLoop, &loop->async_signal);
//	ev_break(loop->evLoop, EVBREAK_ALL);
}

