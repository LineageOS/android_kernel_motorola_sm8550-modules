# Settings for compiling waipio camera architecture

# Localized KCONFIG settings
CONFIG_CCI_ADDR_SWITCH := y
CONFIG_CAM_SENSOR_PROBE_DEBUG := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_CCI_ADDR_SWITCH=1
ccflags-y += -DCONFIG_CAM_SENSOR_PROBE_DEBUG=1
