#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

COMPONENT_ADD_INCLUDEDIRS := ./../include audio_manager/include deepbrain/include

WIFI_CONNECT_IDRS :=  smart_connect 

COMPONENT_SRCDIRS := . \
					audio_manager \
					download_server \
					remote_command \
					net_common \
					upload_server \
					terminalservice \
					$(WIFI_CONNECT_IDRS) \


#COMPONENT_PRIV_INCLUDEDIRS := include

#LIBS := esp-opus esp-mad esp-flac esp-faad2 esp-tremor esp-fdk esp-ogg-container player


#COMPONENT_ADD_LDFLAGS :=  -L$(COMPONENT_PATH)/audio_manager/lib \
                           $(addprefix -l,$(LIBS)) \

#COMPONENT_ADD_LINKER_DEPS := $(patsubst %,$(COMPONENT_PATH)/audio_manager/lib/lib%.a,$(LIBS))

#ALL_LIB_FILES := $(patsubst %,$(COMPONENT_PATH)/audio_manager/lib/lib%.a,$(LIBS))


