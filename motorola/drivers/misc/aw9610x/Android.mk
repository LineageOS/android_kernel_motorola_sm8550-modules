DLKM_DIR := motorola/kernel/modules
LOCAL_PATH := $(call my-dir)

ifeq ($(AW9610X_USB_USE_ONLINE),true)
	KBUILD_OPTIONS += CONFIG_AW9610X_POWER_SUPPLY_ONLINE=y
endif

ifeq ($(AW9610X_HARDWARE_VERSION),true)
	KBUILD_OPTIONS += CONFIG_AW9610X_HARDWARE_VERSION=y
endif

include $(CLEAR_VARS)
LOCAL_MODULE := aw9610x.ko
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
LOCAL_ADDITIONAL_DEPENDENCIES := $(KERNEL_MODULES_OUT)/sensors_class.ko
KBUILD_OPTIONS_GKI += GKI_OBJ_MODULE_DIR=gki
KBUILD_OPTIONS += MODULE_KERNEL_VERSION=$(TARGET_KERNEL_VERSION)
include $(DLKM_DIR)/AndroidKernelModule.mk
