#define LOG_TAG "VIRTUALCAMERASERVICE"

#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <utils/Vector.h>
#include <utils/Timers.h>
#include <utils/String16.h>

#include <binder/Parcel.h>
#include <binder/IInterface.h>

#include <cutils/log.h>

#include "IVirtualCameraService.h"

namespace android {

class BpVirtualCameraService : public BpInterface<IVirtualCameraService>
{
public:
    BpVirtualCameraService(const sp<IBinder>& impl) : BpInterface<IVirtualCameraService>(impl)
    {
    }

    virtual status_t createSession(const String16& ip)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IVirtualCameraService::getInterfaceDescriptor());
        data.writeString16(ip);
        status_t result = remote()->transact(CREATESESSION, data, &reply);
        if (result != NO_ERROR) {
            ALOGE("could not create session\n");
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t destroySession()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IVirtualCameraService::getInterfaceDescriptor());
        status_t result = remote()->transact(DESTROYSESSION, data, &reply);
        if (result != NO_ERROR) {
            ALOGE("could not destroy session\n");
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t setSurface(const sp<IGraphicBufferProducer>& bufferProducer, int32_t width, int32_t height, int32_t format, int32_t transform)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IVirtualCameraService::getInterfaceDescriptor());
        sp<IBinder> b(IInterface::asBinder(bufferProducer));
        data.writeStrongBinder(b);
        data.writeInt32(width);
        data.writeInt32(height);
        data.writeInt32(format);
        data.writeInt32(transform);
        status_t result = remote()->transact(SETSURFACE, data, &reply);
        if (result != NO_ERROR) {
            ALOGE("could not set surface\n");
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t releaseSurface()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IVirtualCameraService::getInterfaceDescriptor());
        status_t result = remote()->transact(RELEASESURFACE, data, &reply);
        if (result != NO_ERROR) {
            ALOGE("could not release surface\n");
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t setCallBackSurface(const sp<IGraphicBufferProducer>& bufferProducer, int32_t width, int32_t height, int32_t format, int32_t transform)
    {
        Parcel data, reply;
        data.writeInterfaceToken(IVirtualCameraService::getInterfaceDescriptor());
        sp<IBinder> b(IInterface::asBinder(bufferProducer));
        data.writeStrongBinder(b);
        data.writeInt32(width);
        data.writeInt32(height);
        data.writeInt32(format);
        data.writeInt32(transform);
        status_t result = remote()->transact(SETCALLBACKSURFACE, data, &reply);
        if (result != NO_ERROR) {
            ALOGE("could not set callback surface\n");
            return result;
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t releaseCallBackSurface()
    {
        Parcel data, reply;
        data.writeInterfaceToken(IVirtualCameraService::getInterfaceDescriptor());
        status_t result = remote()->transact(RELEASECALLBACKSURFACE, data, &reply);
        if (result != NO_ERROR) {
            ALOGE("could not release callback surface\n");
            return result;
        }
        result = reply.readInt32();
        return result;
    }

};

IMPLEMENT_META_INTERFACE(VirtualCameraService, "VirtualCameraService");

status_t BnVirtualCameraService::onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    switch(code)
    {
        case CREATESESSION:
        {
            CHECK_INTERFACE(IVirtualCameraService, data, reply);
            String16 ip = data.readString16();

            status_t result = createSession(ip);
            reply->writeInt32(result);
            return NO_ERROR;
        }
        break;
        case DESTROYSESSION:
        {
            CHECK_INTERFACE(IVirtualCameraService, data, reply);

            status_t result = destroySession();
            reply->writeInt32(result);
            return NO_ERROR;
        }
        break;
        case SETSURFACE:
        {
            CHECK_INTERFACE(IVirtualCameraService, data, reply);
            sp<IGraphicBufferProducer> st =
                interface_cast<IGraphicBufferProducer>(data.readStrongBinder());
            int32_t width = data.readInt32();
            int32_t height = data.readInt32();
            int32_t format = data.readInt32();
            int32_t transform = data.readInt32();
            status_t result = setSurface(st, width, height, format, transform);
            reply->writeInt32(result);
            return NO_ERROR;
        }
        break;
        case RELEASESURFACE:
        {
            CHECK_INTERFACE(IVirtualCameraService, data, reply);

            status_t result = releaseSurface();
            reply->writeInt32(result);
            return NO_ERROR;
        }
        break;
        case SETCALLBACKSURFACE:
        {
            CHECK_INTERFACE(IVirtualCameraService, data, reply);
            sp<IGraphicBufferProducer> st =
                interface_cast<IGraphicBufferProducer>(data.readStrongBinder());
            int32_t width = data.readInt32();
            int32_t height = data.readInt32();
            int32_t format = data.readInt32();
            int32_t transform = data.readInt32();
            status_t result = setCallBackSurface(st, width, height, format, transform);
            reply->writeInt32(result);
            return NO_ERROR;
        }
        break;
        case RELEASECALLBACKSURFACE:
        {
            CHECK_INTERFACE(IVirtualCameraService, data, reply);

            status_t result = releaseCallBackSurface();
            reply->writeInt32(result);
            return NO_ERROR;
        }
        break;
    }
    return BBinder::onTransact(code, data, reply, flags);
}

};
