# Settings for compiling waipio camera architecture

# Localized KCONFIG settings
CONFIG_CCI_ADDR_SWITCH := y

# Flags to pass into C preprocessor
ccflags-y += -DCONFIG_CCI_ADDR_SWITCH=1
