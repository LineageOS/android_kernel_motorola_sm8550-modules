KBUILD_OPTIONS += CONFIG_DRM_PANEL_EVENT_NOTIFICATIONS=y
KBUILD_OPTIONS += CONFIG_BOARD_USES_DOUBLE_TAP_CTRL=y

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) INSTALL_MOD_STRIP=1 -C $(KERNEL_SRC) M=$(M) modules_install

%:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $@ $(KBUILD_OPTIONS)

KBUILD_EXTRA_SYMBOLS += $(M)/../../../sensors/Module.symvers
KBUILD_EXTRA_SYMBOLS += $(M)/../../../mmi_relay/Module.symvers
KBUILD_EXTRA_SYMBOLS += $(M)/../../../../../qcom/display-drivers/msm/Module.symvers
