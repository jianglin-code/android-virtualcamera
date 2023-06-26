/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "CamPrvdr@2.4-virtual"
#define LOG_NDEBUG 1
#include <log/log.h>

#include <regex>
#include <sys/inotify.h>
#include <errno.h>
#include <linux/videodev2.h>
#include <cutils/properties.h>
#include "VirtualCameraProviderImpl_2_4.h"
#include "VirtualCameraDevice_3_4.h"

#include <string>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
//#include <v4l2device.h>
#include "mediactl/mediactl.h"
#include "mediactl/mediactl-priv.h"
#include <linux/videodev2.h>

#define FILE_PATH_LEN                       64
#define CAMS_NUM_MAX                        2
#define FLASH_NUM_MAX                       2
/* The media topology instance that describes video device and
 * sub-device informations.
 *
 * @sd_isp_path, the isp sub-device path, e.g. /dev/v4l-subdev0
 * @vd_params_path, the params video device path
 * @vd_stats_path, the stats video device path
 * @cams, multipe cameras can attache to isp, but only one can be active
 * @sd_sensor_path, the sensor sub-device path
 * @sd_lens_path, the lens sub-device path that attached to sensor
 * @sd_flash_path, the flash sub-device path that attached to sensor,
 *      can be two or more.
 * @link_enabled, the link status of this sensor
 */
#define MAX_MEDIA_DEV_NUM  10

struct rkisp_media_info {
    char sd_isp_path[FILE_PATH_LEN];
    char vd_params_path[FILE_PATH_LEN];
    char vd_stats_path[FILE_PATH_LEN];
    char sd_ispp_path[FILE_PATH_LEN];
    char stream_cif_mipi_id0[FILE_PATH_LEN];
    struct {
            char sd_sensor_path[FILE_PATH_LEN];
            char sd_lens_path[FILE_PATH_LEN];
            char sd_flash_path[FLASH_NUM_MAX][FILE_PATH_LEN];
            bool link_enabled;
            char sensor_entity_name[FILE_PATH_LEN];
    } cams[CAMS_NUM_MAX];
};




static int
rkisp_get_devname(struct media_device *device, const char *name, char *dev_name)
{
    const char *devname;

    media_entity *entity =  NULL;

    entity = media_get_entity_by_name(device, name, strlen(name));
    if (!entity)
        return -1;

    devname = media_entity_get_devname(entity);
    if (!devname) {
        ALOGE("can't find %s device path!", name);
        return -1;
    }

    strncpy(dev_name, devname, FILE_PATH_LEN);

    ALOGD("get %s devname: %s\n", name, dev_name);

    return 0;
}

static int
rkisp_enumrate_modules (struct media_device *device,struct rkisp_media_info *media_info)
{
    int sensor_index = -1;
    uint32_t nents, i;
    const char* dev_name = NULL;
    int active_sensor = -1;
    ;
    nents = media_get_entities_count (device);
    for (i = 0; i < nents; ++i) {
        int module_idx = -1;
        struct media_entity *e;
        const struct media_entity_desc *ef;
        const struct media_link *link;

        e = media_get_entity(device, i);
        ef = media_entity_get_info(e);
        if (ef->type != MEDIA_ENT_T_V4L2_SUBDEV_SENSOR &&
            ef->type != MEDIA_ENT_T_V4L2_SUBDEV_FLASH &&
            ef->type != MEDIA_ENT_T_V4L2_SUBDEV_LENS)
            continue;

        if (ef->name[0] != 'm' && ef->name[3] != '_') {
            ALOGE("sensor/lens/flash entity name format is incorrect,"
                "pls check driver version !\n");
            return -1;
        }

        /* Retrive the sensor index from sensor name,
         * which is indicated by two characters after 'm',
         *   e.g.  m00_b_ov13850 1-0010
         *          ^^, 00 is the module index
         */
        module_idx = atoi(ef->name + 1);
        //ALOGE("sensor:%s,module_idx:%d",ef->name,module_idx);
        if (module_idx >= CAMS_NUM_MAX) {
            ALOGE("multiple sensors more than two not supported, %s\n", ef->name);
            continue;
        }

        if (sensor_index >= 0 && module_idx != sensor_index) {
            continue;
        }

        dev_name = media_entity_get_devname (e);

        switch (ef->type) {
        case MEDIA_ENT_T_V4L2_SUBDEV_SENSOR:
            strncpy(media_info->cams[module_idx].sd_sensor_path,
                    dev_name, FILE_PATH_LEN);
            link = media_entity_get_link(e, 0);
            if (link && (link->flags & MEDIA_LNK_FL_ENABLED)) {
                media_info->cams[module_idx].link_enabled = true;
                active_sensor = module_idx;
                strcpy(media_info->cams[module_idx].sensor_entity_name, ef->name);
                ALOGD("%s(%d) sensor_entity_name(%s)", __FUNCTION__, __LINE__, media_info->cams[module_idx].sensor_entity_name);
            }
            break;
        case MEDIA_ENT_T_V4L2_SUBDEV_FLASH:
            // TODO, support multiple flashes attached to one module
            strncpy(media_info->cams[module_idx].sd_flash_path[0],
                    dev_name, FILE_PATH_LEN);
            break;
        case MEDIA_ENT_T_V4L2_SUBDEV_LENS:
            strncpy(media_info->cams[module_idx].sd_lens_path,
                    dev_name, FILE_PATH_LEN);
            break;
        default:
            break;
        }
    }

    if (active_sensor < 0) {
        ALOGE("Not sensor link is enabled, does sensor probe correctly?\n");
        return -1;
    }

    return 0;
}
int get_media_info(const char* sensor_name,const char* dev_name) {
  int ret = 0;
  unsigned int i, index = 0;
  char sys_path[64];
  int find_sensor = 0;
  int find_isp = 0;
  int linked_sensor = -1;
  struct media_device *device = NULL;
  const struct media_device_info *info = NULL;
  struct rkisp_media_info media_info;
  int find_ispp = 0;
  char model[64] = "\0";
  while (index < MAX_MEDIA_DEV_NUM) {
    info = NULL;
    snprintf(sys_path, 64, "/dev/media%d", index++);
    ALOGD("media : %s\n", sys_path);
    if(access(sys_path,F_OK))
      continue;

    device = media_device_new(sys_path);
    if (device == NULL) {
      ALOGE("Failed to create media %s\n", sys_path);
      continue;
    }

    ret = media_device_enumerate(device);
    if (ret < 0) {
      ALOGE("Failed to enumerate %s (%d)\n", sys_path, ret);
      media_device_unref(device);
      continue;
    }

    info = media_get_info(device);

    /* Try Sensor */
    if (find_sensor!=2) {
      unsigned int nents = media_get_entities_count(device);
      for (i = 0; i < nents; ++i) {
        struct media_entity *entity = media_get_entity(device, i);
        const struct media_entity_desc *desc = media_entity_get_info(entity);
        unsigned int type = desc ->type;
        if (MEDIA_ENT_T_V4L2_SUBDEV == (type & MEDIA_ENT_TYPE_MASK)) {
           unsigned int subtype = type & MEDIA_ENT_SUBTYPE_MASK;
           if (subtype == 1) {
               ret = rkisp_enumrate_modules(device,&media_info);
               if (!ret) {
                   linked_sensor = index;
                   find_sensor = 1;
                   if (info && !strncmp(info->driver, "rkcif", strlen("rkcif"))) {
                       strncpy(model, info->model, 64);
                       for (size_t i = 0; i < CAMS_NUM_MAX; i++)
                       {
                           if(strlen(media_info.cams[i].sensor_entity_name)>0){
                                ALOGD("%s:model:%s,info->model:%s",media_info.cams[i].sensor_entity_name,model, info->model);
                                if(strstr(media_info.cams[i].sensor_entity_name,sensor_name)!=NULL){
                                    find_sensor = 2;
                                    ret = rkisp_get_devname(device, "stream_cif_mipi_id0", media_info.stream_cif_mipi_id0);
                                    memcpy((void*)dev_name,media_info.stream_cif_mipi_id0,strlen(media_info.stream_cif_mipi_id0)+1);
                                    char compact_test[255]={0};
                                    sprintf(compact_test,"/sys/devices/platform/%s/compact_test",model);
                                     //write /sys/devices/platform/rkcif-mipi-lvds2/compact_test "0 0 0 0"
                                     FILE *fp = fopen(compact_test,"wb+");
                                     if(fp != NULL){
                                         const char *compact_test_value="0 0 0 0";
                                        ALOGD("write %s %s",compact_test,compact_test_value);
                                        fwrite(compact_test_value,1,strlen(compact_test_value),fp);
                                        fclose(fp);
                                     }else{
                                         ALOGE("failed to open:%s",compact_test);
                                     }
                                }
                           }
                       }
                   }
               }
           }
        }
        if(find_sensor == 2){
            break;
        }
      }
    }
    media_device_unref(device);
    if(find_sensor == 2){
        break;
    }
  }

  return ret;
}

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace V2_4 {
namespace implementation {

template struct VirCameraProvider<VirtualCameraProviderImpl_2_4>;

namespace {
// "device@<version>/external/<id>"
const std::regex kDeviceNameRE("device@([0-9]+\\.[0-9]+)/virtual/(.+)");
//const int kMaxDevicePathLen = 256;
const char* kDevicePath = "/dev/";
constexpr char kPrefix[] = "video";
constexpr int kPrefixLen = 5;
constexpr int kDevicePrefixLen = 5 + kPrefixLen;

bool matchDeviceName(int cameraIdOffset,
                     const hidl_string& deviceName, std::string* deviceVersion,
                     std::string* cameraDevicePath) {
#if 1
    ALOGE("@%s, deviceName %s", __FUNCTION__, deviceName.c_str());
    std::string deviceNameStd(deviceName.c_str());
    std::smatch sm;
    if (std::regex_match(deviceNameStd, sm, kDeviceNameRE)) {
        if (deviceVersion != nullptr) {
            *deviceVersion = sm[1];
            //ALOGD("@%s,deviceVersion:%s",__FUNCTION__,(*deviceVersion).c_str());
        }
        if (cameraDevicePath != nullptr) {
            //*cameraDevicePath = "/dev/video" + std::to_string(std::stoi(sm[2]) - cameraIdOffset);
            *cameraDevicePath = sm[2];
        }
        return true;
    }
    ALOGE("@%s,match Failed",__FUNCTION__);
    return false;
#else
        if (deviceVersion != nullptr) {
            *deviceVersion = "0";
        }
        if (cameraDevicePath != nullptr) {
            *cameraDevicePath = "/dev/video1";
        }
    return true;
#endif
}

} // anonymous namespace

VirtualCameraProviderImpl_2_4::VirtualCameraProviderImpl_2_4() :
        mCfg(VirtualCameraConfig::loadFromCfg()),
        mHotPlugThread(this) {
    mHotPlugThread.run("VirCamHotPlug", PRIORITY_BACKGROUND);

    mPreferredHal3MinorVersion =
        property_get_int32("ro.vendor.camera.external.hal3TrebleMinorVersion", 4);
    ALOGV("Preferred HAL 3 minor version is %d", mPreferredHal3MinorVersion);
    switch(mPreferredHal3MinorVersion) {
        case 4:
        case 5:
        case 6:
            // OK
            break;
        default:
            ALOGW("Unknown minor camera device HAL version %d in property "
                    "'camera.external.hal3TrebleMinorVersion', defaulting to 4",
                    mPreferredHal3MinorVersion);
            mPreferredHal3MinorVersion = 4;
    }
}

VirtualCameraProviderImpl_2_4::~VirtualCameraProviderImpl_2_4() {
    mHotPlugThread.requestExit();
}


Return<Status> VirtualCameraProviderImpl_2_4::setCallback(
        const sp<ICameraProviderCallback>& callback) {
    {
        Mutex::Autolock _l(mLock);
        mCallbacks = callback;
    }
    if (mCallbacks == nullptr) {
        return Status::OK;
    }
    // Send a callback for all devices to initialize
    {
        for (const auto& pair : mCameraStatusMap) {
            mCallbacks->cameraDeviceStatusChange(pair.first, pair.second);
        }
    }

    return Status::OK;
}

Return<void> VirtualCameraProviderImpl_2_4::getVendorTags(
        ICameraProvider::getVendorTags_cb _hidl_cb) {
    // No vendor tag support for USB camera
    hidl_vec<VendorTagSection> zeroSections;
    _hidl_cb(Status::OK, zeroSections);
    return Void();
}

Return<void> VirtualCameraProviderImpl_2_4::getCameraIdList(
        ICameraProvider::getCameraIdList_cb _hidl_cb) {
    // External camera HAL always report 0 camera, and extra cameras
    // are just reported via cameraDeviceStatusChange callbacks
    hidl_vec<hidl_string> hidlDeviceNameList;
    _hidl_cb(Status::OK, hidlDeviceNameList);
    return Void();
}

Return<void> VirtualCameraProviderImpl_2_4::isSetTorchModeSupported(
        ICameraProvider::isSetTorchModeSupported_cb _hidl_cb) {
    // setTorchMode API is supported, though right now no external camera device
    // has a flash unit.
    _hidl_cb (Status::OK, true);
    return Void();
}

Return<void> VirtualCameraProviderImpl_2_4::getCameraDeviceInterface_V1_x(
        const hidl_string&,
        ICameraProvider::getCameraDeviceInterface_V1_x_cb _hidl_cb) {
    // External Camera HAL does not support HAL1
    _hidl_cb(Status::OPERATION_NOT_SUPPORTED, nullptr);
    return Void();
}

Return<void> VirtualCameraProviderImpl_2_4::getCameraDeviceInterface_V3_x(
        const hidl_string& cameraDeviceName,
        ICameraProvider::getCameraDeviceInterface_V3_x_cb _hidl_cb) {

    std::string cameraDevicePath, deviceVersion;
    bool match = matchDeviceName(mCfg.cameraIdOffset, cameraDeviceName,
                                 &deviceVersion, &cameraDevicePath);
    if (!match) {
        _hidl_cb(Status::ILLEGAL_ARGUMENT, nullptr);
        return Void();
    }

    if (mCameraStatusMap.count(cameraDeviceName) == 0 ||
            mCameraStatusMap[cameraDeviceName] != CameraDeviceStatus::PRESENT) {
        _hidl_cb(Status::ILLEGAL_ARGUMENT, nullptr);
        return Void();
    }

    sp<device::V3_4::virtuals::implementation::VirtualCameraDevice> deviceImpl;
    switch (mPreferredHal3MinorVersion) {
        case 4: {
            ALOGV("Constructing v3.4 external camera device");
            deviceImpl = new device::V3_4::virtuals::implementation::VirtualCameraDevice(
                    cameraDevicePath, mCfg);
            break;
        }
        case 5: {
            ALOGV("Constructing v3.5 external camera device");
            // deviceImpl = new device::V3_5::implementation::ExternalCameraDevice(
            //         cameraDevicePath, mCfg);
            break;
        }
        case 6: {
            ALOGV("Constructing v3.6 external camera device");
            // deviceImpl = new device::V3_6::implementation::ExternalCameraDevice(
            //         cameraDevicePath, mCfg);
            break;
        }
        default:
            ALOGE("%s: Unknown HAL minor version %d!", __FUNCTION__, mPreferredHal3MinorVersion);
            _hidl_cb(Status::INTERNAL_ERROR, nullptr);
            return Void();
    }

    if (deviceImpl == nullptr || deviceImpl->isInitFailed()) {
        ALOGE("%s: camera device %s init failed!", __FUNCTION__, cameraDevicePath.c_str());
        _hidl_cb(Status::INTERNAL_ERROR, nullptr);
        return Void();
    }

    IF_ALOGV() {
        deviceImpl->getInterface()->interfaceChain([](
            ::android::hardware::hidl_vec<::android::hardware::hidl_string> interfaceChain) {
                ALOGV("Device interface chain:");
                for (auto iface : interfaceChain) {
                    ALOGV("  %s", iface.c_str());
                }
            });
    }

    _hidl_cb (Status::OK, deviceImpl->getInterface());

    return Void();
}

void VirtualCameraProviderImpl_2_4::addCamera(const char* devName) {
    Mutex::Autolock _l(mLock);
    std::string deviceName;
    std::string cameraId = devName;//std::to_string(mCfg.cameraIdOffset +
                                   //std::atoi(devName + kDevicePrefixLen));
    if (mPreferredHal3MinorVersion == 6) {
        deviceName = std::string("device@3.6/virtual/") + cameraId;
    } else if (mPreferredHal3MinorVersion == 5) {
        deviceName = std::string("device@3.5/virtual/") + cameraId;
    } else {
        deviceName = std::string("device@3.4/virtual/") + cameraId;
    }
    ALOGD("@%s devName:%s,cameraId:%s,deviceName:%s",__FUNCTION__,devName,cameraId.c_str(),deviceName.c_str());
    mCameraStatusMap[deviceName] = CameraDeviceStatus::PRESENT;
    if (mCallbacks != nullptr) {
        mCallbacks->cameraDeviceStatusChange(deviceName, CameraDeviceStatus::PRESENT);
    }
}

void VirtualCameraProviderImpl_2_4::deviceAdded(const char* devName) {
#if 0
    {
        base::unique_fd fd(::open(devName, O_RDWR));
        if (fd.get() < 0) {
            ALOGE("%s open v4l2 device %s failed:%s", __FUNCTION__, devName, strerror(errno));
            return;
        }

        struct v4l2_capability capability;
        int ret = ioctl(fd.get(), VIDIOC_QUERYCAP, &capability);
        if (ret < 0) {
            ALOGE("%s v4l2 QUERYCAP %s failed", __FUNCTION__, devName);
            return;
        }

        if (!((capability.device_caps & V4L2_CAP_VIDEO_CAPTURE) || (capability.device_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE))) {
            ALOGW("%s wpzz test device %s does not support VIDEO_CAPTURE", __FUNCTION__, devName);
            return;
        }
    }
#endif
    ALOGW("%s: Attempt to init camera device %s", __FUNCTION__, devName);
    // See if we can initialize VirtualCameraDevice correctly
    sp<device::V3_4::virtuals::implementation::VirtualCameraDevice> deviceImpl =
            new device::V3_4::virtuals::implementation::VirtualCameraDevice(devName, mCfg);
    if (deviceImpl == nullptr || deviceImpl->isInitFailed()) {
        ALOGW("%s: Attempt to init camera device %s failed!", __FUNCTION__, devName);
        return;
    }
    deviceImpl.clear();

    addCamera(devName);
    return;
}

void VirtualCameraProviderImpl_2_4::deviceRemoved(const char* devName) {
    Mutex::Autolock _l(mLock);
    std::string deviceName;
    std::string cameraId = std::to_string(mCfg.cameraIdOffset +
                                          std::atoi(devName + kDevicePrefixLen));
    cameraId= devName;
    //cameraId= "888";
    if (mPreferredHal3MinorVersion == 6) {
        deviceName = std::string("device@3.6/virtual/") + cameraId;
    } else if (mPreferredHal3MinorVersion == 5) {
        deviceName = std::string("device@3.5/virtual/") + cameraId;
    } else {
        deviceName = std::string("device@3.4/virtual/") + cameraId;
    }
    if (mCameraStatusMap.find(deviceName) != mCameraStatusMap.end()) {
        mCameraStatusMap.erase(deviceName);
        if (mCallbacks != nullptr) {
            mCallbacks->cameraDeviceStatusChange(deviceName, CameraDeviceStatus::NOT_PRESENT);
        }
    } else {
        ALOGE("%s: cannot find camera device %s", __FUNCTION__, devName);
    }
}

VirtualCameraProviderImpl_2_4::HotplugThread::HotplugThread(
        VirtualCameraProviderImpl_2_4* parent) :
        Thread(/*canCallJava*/false),
        mParent(parent),
        mInternalDevices(parent->mCfg.mInternalDevices) {}

VirtualCameraProviderImpl_2_4::HotplugThread::~HotplugThread() {}

bool VirtualCameraProviderImpl_2_4::HotplugThread::threadLoop() {
    // Find existing /dev/video* devices
    DIR* devdir = opendir(kDevicePath);
    if(devdir == 0) {
        ALOGE("%s: cannot open %s! Exiting threadloop", __FUNCTION__, kDevicePath);
        return false;
    }

    char dev_name[256]={0};
    //char sub_dev_name[256]={0};
    get_media_info(mParent->mCfg.snsName.c_str(),dev_name);
    if (strlen(dev_name)>0){
        ALOGE("%s: %s", __FUNCTION__, dev_name);
        //mParent->deviceAdded(dev_name);

        //int subid =std::atoi(dev_name + kDevicePrefixLen)+1;
        //sprintf(sub_dev_name,"/dev/video%d",subid);
        //mParent->deviceAdded(sub_dev_name);
    }else{
        ALOGE("%s: 0", __FUNCTION__);
        mParent->deviceAdded("0");
        mParent->deviceAdded("1");
    }

    closedir(devdir);

    // Watch new video devices
    mINotifyFD = inotify_init();
    if (mINotifyFD < 0) {
        ALOGE("%s: inotify init failed! Exiting threadloop", __FUNCTION__);
        return true;
    }

    mWd = inotify_add_watch(mINotifyFD, kDevicePath, IN_CREATE | IN_DELETE);
    if (mWd < 0) {
        ALOGE("%s: inotify add watch failed! Exiting threadloop", __FUNCTION__);
        return true;
    }

    ALOGI("%s start monitoring new V4L2 devices", __FUNCTION__);

    bool done = false;
    char eventBuf[512];
    while (!done) {
        int offset = 0;
        int ret = read(mINotifyFD, eventBuf, sizeof(eventBuf));
        if (ret >= (int)sizeof(struct inotify_event)) {
            while (offset < ret) {
                struct inotify_event* event = (struct inotify_event*)&eventBuf[offset];
                if (event->wd == mWd) {
                    if (!strncmp(kPrefix, event->name, kPrefixLen)) {
                        std::string deviceId(event->name + kPrefixLen);
                        if (mInternalDevices.count(deviceId) == 0) {

                        }
                    }
                }
                offset += sizeof(struct inotify_event) + event->len;
            }
        }
    }

    return true;
}

}  // namespace implementation
}  // namespace V2_4
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
