# Settings for compiling waipio camera architecture

# Localized KCONFIG settings
# MMI_STOPSHIP Camera: Remove for user build
CONFIG_CCI_DEBUG_INTF := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_CCI_DEBUG_INTF=1
