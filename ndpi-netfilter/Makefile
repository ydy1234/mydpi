all:
ifeq ($(strip $(NDPI_PATH)),)
	$(error NDPI_PATH required)
endif
	$(MAKE) -C ipt
	$(MAKE) -C src
	$(MAKE) -C userApp
modules_install:
	$(MAKE) -C src modules_install
clean:
	$(MAKE) -C src clean
	$(MAKE) -C ipt clean
	$(MAKE) -C userApp clean
