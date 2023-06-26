#ifndef __IVIRTUALCAMERASERVICE_H__
#define __IVIRTUALCAMERASERVICE_H__

#include <binder/IInterface.h>
#include <binder/Parcel.h>
#include <binder/BinderService.h>
#include <cutils/properties.h>
#include <utils/String16.h>

#include <gui/IGraphicBufferProducer.h>
#include <gui/Surface.h>

namespace android
{

class String16;
class IVirtualCameraService : public IInterface
{
protected:
    enum {
        CREATESESSION = IBinder::FIRST_CALL_TRANSACTION,
        DESTROYSESSION = IBinder::FIRST_CALL_TRANSACTION + 1,
        SETSURFACE = IBinder::FIRST_CALL_TRANSACTION + 2,
        RELEASESURFACE = IBinder::FIRST_CALL_TRANSACTION + 3,
        SETCALLBACKSURFACE = IBinder::FIRST_CALL_TRANSACTION + 4,
        RELEASECALLBACKSURFACE = IBinder::FIRST_CALL_TRANSACTION + 5,
    };

public:
    DECLARE_META_INTERFACE(VirtualCameraService);

    virtual status_t createSession(const String16& name) = 0;
    virtual status_t destroySession() = 0;
    virtual status_t setSurface(const sp<IGraphicBufferProducer>& bufferProducer, int32_t width, int32_t height, int32_t format, int32_t transform) = 0;
    virtual status_t releaseSurface() = 0;
    virtual status_t setCallBackSurface(const sp<IGraphicBufferProducer>& bufferProducer, int32_t width, int32_t height, int32_t format, int32_t transform) = 0;
    virtual status_t releaseCallBackSurface() = 0;
};

class BnVirtualCameraService : public BnInterface<IVirtualCameraService>
{
    virtual status_t onTransact(uint32_t code,
                                const Parcel& data,
                                Parcel* reply,
                                uint32_t flags = 0);
};

};

#endif
