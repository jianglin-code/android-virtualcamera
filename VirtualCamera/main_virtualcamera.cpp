#define LOG_TAG "VirtualCameraService"

#include <fcntl.h>

#include <cutils/log.h>
#include <cutils/properties.h>
#include <binder/BinderService.h>
#include <android/native_window.h>

#include <VirtualCameraService.h>

using namespace android;

int main(int /*argc*/, char** /*argv*/)
{
	ALOGI("GuiExt service start...");

	VirtualCameraService::publishAndJoinThreadPool(true);

	ProcessState::self()->setThreadPoolMaxThreadCount(8);

	ALOGD("GuiExt service exit...");
    return 0;
}
