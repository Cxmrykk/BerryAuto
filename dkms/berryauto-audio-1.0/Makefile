obj-m += snd-berryauto.o
snd-berryauto-objs := berryauto_audio.o

KERNEL_VERSION ?= $(shell uname -r)
KERNEL_DIR ?= /lib/modules/$(KERNEL_VERSION)/build

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	