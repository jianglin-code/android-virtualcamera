CALIB_PATH := $(TOP)/hardware/rockchip/camera_vir/calib
CONFIG_PATH := $(TOP)/hardware/rockchip/camera_vir/config
PRODUCT_COPY_FILES += \
        $(call find-copy-subdir-files,*,$(CALIB_PATH)/,$(TARGET_COPY_OUT_VENDOR)/etc/camera/calib)

PRODUCT_COPY_FILES += \
        $(call find-copy-subdir-files,*,$(CONFIG_PATH)/,$(TARGET_COPY_OUT_VENDOR)/etc)
