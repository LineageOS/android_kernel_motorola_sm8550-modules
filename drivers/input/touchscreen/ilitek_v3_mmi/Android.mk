DLKM_DIR := motorola/kernel/modules
LOCAL_PATH := $(call my-dir)

ifneq ($(BOARD_USES_DOUBLE_TAP),)
	KERNEL_CFLAGS += CONFIG_INPUT_ILI_ENABLE_DOUBLE_TAP=y
endif

ifeq ($(BOARD_USES_DOUBLE_TAP_CTRL),true)
	KERNEL_CFLAGS += CONFIG_BOARD_USES_DOUBLE_TAP_CTRL=y
endif

ifneq ($(CONFIG_INPUT_SET_TOUCH_STATE),)
	KERNEL_CFLAGS += CONFIG_INPUT_ILI_SET_TOUCH_STATE=y
endif

ifneq ($(BOARD_USES_PANEL_NOTIFICATIONS),)
	KERNEL_CFLAGS += CONFIG_INPUT_ILI_PANEL_NOTIFICATIONS=y
endif

ifneq ($(BOARD_USES_MTK_CHECK_PANEL),)
	KERNEL_CFLAGS += CONFIG_ILITEK_MTK_CHECK_PANEL=y
endif

ifeq ($(ILITEK_MULTI_TP_MODULE),true)
	KERNEL_CFLAGS += CONFIG_ILITEK_MULTI_TP_MODULE=y
endif

ifneq ($(ILITEK_RESUME_BY_DDI),)
	KERNEL_CFLAGS += CONFIG_ILITEK_RESUME_BY_DDI=y
endif

ifeq ($(CONFIG_INPUT_CHARGER_DETECTION),true)
	KERNEL_CFLAGS += CONFIG_ILITEK_CHARGER=y
endif

ifeq ($(ILITEK_ESD),true)
	KERNEL_CFLAGS += CONFIG_ILITEK_ESD=y
endif

ifeq ($(ILITEK_GESTURE),true)
	KERNEL_CFLAGS += CONFIG_ILITEK_GESTURE=y
endif

ifeq ($(TOUCHSCREEN_LAST_TIME),true)
	KERNEL_CFLAGS += CONFIG_ILI_TOUCH_LAST_TIME=y
endif

ifneq ($(findstring mmi_info.ko,$(BOARD_VENDOR_KERNEL_MODULES)),)
      KBUILD_OPTIONS += CONFIG_INPUT_TOUCHSCREEN_MMI=y
 endif

include $(CLEAR_VARS)
ifneq ($(BOARD_USES_DOUBLE_TAP),)
LOCAL_ADDITIONAL_DEPENDENCIES += $(KERNEL_MODULES_OUT)/sensors_class.ko
LOCAL_REQUIRED_MODULES := sensors_class.ko
endif
LOCAL_MODULE := ilitek_v3_mmi.ko
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(KERNEL_MODULES_OUT)
KBUILD_OPTIONS_GKI += GKI_OBJ_MODULE_DIR=gki
LOCAL_ADDITIONAL_DEPENDENCIES += $(KERNEL_MODULES_OUT)/mmi_info.ko
ifneq ($(findstring touchscreen_mmi.ko,$(BOARD_VENDOR_KERNEL_MODULES)),)
	LOCAL_ADDITIONAL_DEPENDENCIES += $(KERNEL_MODULES_OUT)/touchscreen_mmi.ko
endif
include $(DLKM_DIR)/AndroidKernelModule.mk
