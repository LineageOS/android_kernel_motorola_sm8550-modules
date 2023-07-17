# Settings for compiling kalama camera architecture

# Localized KCONFIG settings
CONFIG_MOT_OIS_SEM1217S_DRIVER := y
CONFIG_CCI_DEBUG_INTF := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_MOT_OIS_SEM1217S_DRIVER=1
ccflags-y += -DCONFIG_CCI_DEBUG_INTF=1
