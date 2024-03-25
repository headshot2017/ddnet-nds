#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

export TARGET		:=	$(shell basename $(CURDIR))
export TOPDIR		:=	$(CURDIR)
GENERATED	:=	arm9/build/game/generated


.PHONY: arm7/$(TARGET).elf arm9/$(TARGET).elf

#---------------------------------------------------------------------------------
TEXT1		=	Teeworlds NDS
TEXT2		=	Headshotnoby
#TEXT3		=	""
ICON		=  	$(DEVKITPRO)/libnds/icon.bmp
NITRODIR	:= -d nitrofiles
#---------------------------------------------------------------------------------

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all: target

#---------------------------------------------------------------------------------
$(TARGET).nds	:	arm7/$(TARGET).elf arm9/$(TARGET).elf
	@echo Compiling ARM7 and ARM9
	ndstool -c $(TARGET).nds -7 arm7/$(TARGET).elf -9 arm9/$(TARGET).elf -b $(ICON) "$(TEXT1);$(TEXT2)";

#---------------------------------------------------------------------------------
arm7/$(TARGET).elf:
	@echo Compiling ARM7
	@$(MAKE) -C arm7

#---------------------------------------------------------------------------------
arm9/$(TARGET).elf: $(GENERATED)
	@echo Compiling ARM9
	@$(MAKE) -C arm9

#---------------------------------------------------------------------------------
clean:
	@$(MAKE) -C arm9 clean
	@$(MAKE) -C arm7 clean
	@rm -f $(TARGET).nds arm7/$(TARGET).elf arm9/$(TARGET).elf

#---------------------------------------------------------------------------------
$(GENERATED):
	@[ -d $@ ] || mkdir -p $@
	python datasrc/compile.py network_source > $(GENERATED)/protocol.cpp
	python datasrc/compile.py network_header > $(GENERATED)/protocol.h
	python datasrc/compile.py client_content_source > $(GENERATED)/client_data.cpp
	python datasrc/compile.py client_content_header > $(GENERATED)/client_data.h

target: $(TARGET).nds
