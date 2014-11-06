# main project target
$(BUILD_PATH)/main.o: main.cpp
	$(SILENT_CXX) $(CXX) $(CFLAGS) $(CXXFLAGS) $(LIBRARY_INCLUDES) -o $@ -c $< 

$(BUILD_PATH)/$(BOARDNAME)$(FRAME).elf: $(BUILDDIRS) $(TGT_BIN) $(BUILD_PATH)/main.o
	$(SILENT_LD) $(CXX) $(LDFLAGS) -o $@ $(TGT_BIN) $(BUILD_PATH)/main.o -Wl,-Map,$(BUILD_PATH)/$(BOARDNAME)$(FRAME).map

$(BUILD_PATH)/$(BOARDNAME)$(FRAME).bin: $(BUILD_PATH)/$(BOARDNAME)$(FRAME).elf
	$(SILENT_OBJCOPY) $(OBJCOPY) -v -Obinary $(BUILD_PATH)/$(BOARDNAME)$(FRAME).elf $@ 1>/dev/null
	$(SILENT_OBJCOPY) $(OBJCOPY) -v -Oihex $(BUILD_PATH)/$(BOARDNAME)$(FRAME).elf $(BUILD_PATH)/$(BOARDNAME)$(FRAME).hex 1>/dev/null
	$(SILENT_DISAS) $(DISAS) -d $(BUILD_PATH)/$(BOARDNAME)$(FRAME).elf > $(BUILD_PATH)/$(BOARDNAME)$(FRAME).disas
	
	@echo " "
	@echo "Object file sizes:"
	@find $(BUILD_PATH) -iname *.o | xargs $(SIZE) -t > $(BUILD_PATH)/$(BOARDNAME)$(FRAME).sizes
	@cat $(BUILD_PATH)/$(BOARDNAME)$(FRAME).sizes
	@echo " "
	@echo "Final Size:"
	@$(SIZE) $<
	@echo $(MEMORY_TARGET) > $(BUILD_PATH)/build-type

$(BUILDDIRS):
	@mkdir -p $@
