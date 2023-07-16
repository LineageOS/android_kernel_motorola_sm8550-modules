DLKM_DIR := motorola/kernel/modules
LOCAL_PATH := $(call my-dir)


ifeq ($(DRM_PANEL_NOTIFICATIONS),true)
	KBUILD_OPTIONS += CONFIG_DRM_PANEL_NOTIFICATIONS=y
endif

ifeq ($(TOUCHSCREEN_SYNA_TCM2_SYSFS),true)
	KBUILD_OPTIONS += CONFIG_TOUCHSCREEN_SYNA_TCM2_SYSFS=y
endif

ifeq ($(TOUCHSCREEN_SYNA_TCM2_TESTING),true)
	KBUILD_OPTIONS += CONFIG_TOUCHSCREEN_SYNA_TCM2_TESTING=y
endif

ifneq ($(findstring touchscreen_mmi.ko,$(BOARD_VENDOR_KERNEL_MODULES)),)
	KBUILD_OPTIONS += CONFIG_INPUT_TOUCHSCREEN_MMI=y
endif



include $(CLEAR_VARS)
LOCAL_MODULE := synaptics_v2_mmi.ko
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
LOCAL_ADDITIONAL_DEPENDENCIES += $(KERNEL_MODULES_OUT)/mmi_info.ko
ifneq ($(findstring touchscreen_mmi.ko,$(BOARD_VENDOR_KERNEL_MODULES)),)
	KBUILD_OPTIONS += CONFIG_INPUT_TOUCHSCREEN_MMI=y
	LOCAL_ADDITIONAL_DEPENDENCIES += $(KERNEL_MODULES_OUT)/touchscreen_mmi.ko
endif
KBUILD_OPTIONS_GKI += GKI_OBJ_MODULE_DIR=gki
include $(DLKM_DIR)/AndroidKernelModule.mk