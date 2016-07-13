# Standard things
sp := $(sp).x
dirstack_$(sp) := $(d)
d := $(dir)
BUILDDIRS += $(BUILD_PATH)/$(d)

# Local flags
CFLAGS_$(d) := 

# Local rules and targets
cSRCS_$(d) :=

cppSRCS_$(d) :=
cppSRCS_$(d) += AP_BoardLED.cpp
cppSRCS_$(d) += AP_Notify.cpp
cppSRCS_$(d) += Buzzer.cpp
cppSRCS_$(d) += ExternalLED.cpp
cppSRCS_$(d) += ToshibaLED_I2C.cpp
cppSRCS_$(d) += ToshibaLED_PX4.cpp
cppSRCS_$(d) += ToshibaLED.cpp
cppSRCS_$(d) += ToneAlarm_PX4.cpp
cppSRCS_$(d) += RGBLed.cpp
cppSRCS_$(d) += VRBRAIN_LED.cpp

cFILES_$(d) := $(cSRCS_$(d):%=$(d)/%)
cppFILES_$(d) := $(cppSRCS_$(d):%=$(d)/%)

OBJS_$(d) := $(cFILES_$(d):%.c=$(BUILD_PATH)/%.o) \
             $(cppFILES_$(d):%.cpp=$(BUILD_PATH)/%.o)
DEPS_$(d) := $(OBJS_$(d):%.o=%.d)

$(OBJS_$(d)): TGT_CFLAGS := $(CFLAGS_$(d))

TGT_BIN += $(OBJS_$(d))

# Standard things
-include $(DEPS_$(d))
d := $(dirstack_$(sp))
sp := $(basename $(sp))