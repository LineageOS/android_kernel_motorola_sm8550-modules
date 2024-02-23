# Settings for compiling kalama camera architecture

# Localized KCONFIG settings
ifneq (,$(filter userdebug eng, $(TARGET_BUILD_VARIANT)))
CONFIG_CCI_DEBUG_INTF := y
ccflags-y += -DCONFIG_CCI_DEBUG_INTF=1
endif
CONFIG_MOT_OIS_DW9784_DRIVER := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_MOT_OIS_DW9784_DRIVER=1
