# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/player/include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/player $(PROJECT_PATH)/player/lib/libplayer.a
COMPONENT_LINKER_DEPS += $(PROJECT_PATH)/player/lib/libplayer.a
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += player
component-player-build: 
