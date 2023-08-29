# Settings for compiling kalama camera architecture

# Localized KCONFIG settings
ifneq (,$(filter userdebug eng, $(TARGET_BUILD_VARIANT)))
CONFIG_CCI_DEBUG_INTF := y
ccflags-y += -DCONFIG_CCI_DEBUG_INTF=1
endif
CONFIG_MOT_OIS_SEM1217S_DRIVER := y
CONFIG_CAM_SENSOR_PROBE_DEBUG := y
CONFIG_MOT_OIS_EARLY_UPGRADE_FW := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_MOT_OIS_SEM1217S_DRIVER=1
ccflags-y += -DCONFIG_CAM_SENSOR_PROBE_DEBUG=1
ccflags-y += -DCONFIG_MOT_OIS_AFTER_SALES_SERVICE=1
ccflags-y += -DCONFIG_MOT_OIS_EARLY_UPGRADE_FW=1
