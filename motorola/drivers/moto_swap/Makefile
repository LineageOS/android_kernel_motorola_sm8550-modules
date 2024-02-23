KBUILD_OPTIONS += CONFIG_HYBRIDSWAP_ZRAM=y
KBUILD_OPTIONS += CONFIG_HYBRIDSWAP=y
KBUILD_OPTIONS += CONFIG_HYBRIDSWAP_SWAPD=y
KBUILD_OPTIONS += CONFIG_HYBRIDSWAP_CORE=y

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules $(KBUILD_OPTIONS)

modules_install:
	$(MAKE) INSTALL_MOD_STRIP=1 -C $(KERNEL_SRC) M=$(M) modules_install

%:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $@ $(KBUILD_OPTIONS)

clean:
	rm -f *.o *.ko *.mod.c *.mod.o *~ .*.cmd Module.symvers
	rm -rf .tmp_versions
