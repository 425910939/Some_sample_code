#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := smartAlarm

SRCDIRS := init bsp app sys  esp-codec-lib player MediaHal userconfig SystemSal

COMPONENT_ADD_INCLUDEDIRS := components/include

COMPONENT_ADD_LDFLAGS = -Wl,--Map=map.txt

include $(IDF_PATH)/make/project.mk

