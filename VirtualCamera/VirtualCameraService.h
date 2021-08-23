#pragma GCC system_header
#ifndef __GUIEXT_SERVICE_H__
#define __GUIEXT_SERVICE_H__

#include <utils/threads.h>
#include "IVirtualCameraService.h"

namespace android
{

class String16;
class VirtualCameraService :
        public BinderService<VirtualCameraService>,
        public BnVirtualCameraService
//        public Thread
{
    friend class BinderService<VirtualCameraService>;
public:

    VirtualCameraService();
    ~VirtualCameraService();

    static char const* getServiceName() { return "virtual.camera"; }

    virtual status_t createSession(const String16& name);
    virtual status_t destroySession();
    virtual status_t setSurface(const sp<IGraphicBufferProducer>& bufferProducer, int32_t width, int32_t height, int32_t format, int32_t transform);
    virtual status_t releaseSurface();
    virtual status_t setCallBackSurface(const sp<IGraphicBufferProducer>& bufferProducer, int32_t width, int32_t height, int32_t format, int32_t transform);
    virtual status_t releaseCallBackSurface();

private:

};
};
#endif
