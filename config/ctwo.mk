# Settings for compiling kalama camera architecture

# Localized KCONFIG settings
CONFIG_MOT_OIS_SEM1217S_DRIVER := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_MOT_OIS_SEM1217S_DRIVER=1
