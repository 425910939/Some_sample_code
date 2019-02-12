deps_config := \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/app_trace/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/SmartAlarm/sys/audio_manager/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/aws_iot/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/bt/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/driver/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/esp32/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/esp_adc_cal/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/esp_http_client/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/ethernet/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/fatfs/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/freertos/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/heap/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/SmartAlarm/init/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/libsodium/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/log/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/lwip/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/mbedtls/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/nvs_flash/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/openssl/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/pthread/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/spi_flash/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/spiffs/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/tcpip_adapter/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/vfs/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/wear_levelling/Kconfig \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/bootloader/Kconfig.projbuild \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/esptool_py/Kconfig.projbuild \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/components/partition_table/Kconfig.projbuild \
	/mnt/onego2017/dongshaoyu/B300/xiaop/SmartAlarmDevice/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)

ifneq "$(IDF_CMAKE)" "n"
include/config/auto.conf: FORCE
endif

$(deps_config): ;
