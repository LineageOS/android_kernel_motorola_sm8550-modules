# Settings for compiling kalama camera architecture

# Localized KCONFIG settings
# Camera: Remove for user build
CONFIG_CCI_DEBUG_INTF := y
CONFIG_CAM_SENSOR_PROBE_DEBUG := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_MOT_OIS_AFTER_SALES_SERVICE=1
ccflags-y += -DCONFIG_CCI_DEBUG_INTF=1
ccflags-y += -DCONFIG_CAM_SENSOR_PROBE_DEBUG=1
