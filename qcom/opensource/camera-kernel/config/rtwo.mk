# Settings for compiling kalama camera architecture

# Localized KCONFIG settings
# Camera: Remove for user build
ifneq (,$(filter userdebug eng, $(TARGET_BUILD_VARIANT)))
CONFIG_CCI_DEBUG_INTF := y

ccflags-y += -DCONFIG_CCI_DEBUG_INTF=1
endif

CONFIG_CAM_SENSOR_PROBE_DEBUG := y
CONFIG_CAM_SENSOR_CTLE_FOR_CANYON := y
CONFIG_MOT_SENSOR_PRE_POWERUP := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_MOT_DONGWOON_OIS_AF_DRIFT=1
ccflags-y += -DCONFIG_MOT_OIS_AFTER_SALES_SERVICE=1
ccflags-y += -DCONFIG_DONGWOON_OIS_VSYNC=1
ccflags-y += -DCONFIG_CAM_SENSOR_PROBE_DEBUG=1
ccflags-y += -DCONFIG_CAM_SENSOR_CTLE_FOR_CANYON=1
ccflags-y += -DCONFIG_MOT_SENSOR_PRE_POWERUP=1
