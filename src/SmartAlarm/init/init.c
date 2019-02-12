#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "rom/rtc.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "display.h"
#include "poll.h"
#include "keyevent.h"
#include "connect_manager.h"
#include "download_server.h"
#include "audio_manager.h"
#include "story_push.h"
#include "story_player.h"
#include "wechat.h"
#include "keypad.h"
#include "remote_command.h"
#include "system_manager.h"
#include "upload_server.h"
#include "alarms.h"
#include "esp_system.h"
#include "terminalcontrolservice.h"
#include "gc.h"
#include "extern_gpio.h"
#include "get_voltage.h"
#include "led.h"
#include "ota.h"
#include "log.h"
#include "tm1628.h"
#include "sc7a20.h"
#include "DeepBrainService.h"
#include "nv_interface.h"
#include "version.h"
#include "MediaHal.h"
#include "restore.h"
#include "power_manager.h"

#define LOG_TAG		"init"
bool log_flag_debug = true;
bool log_flag_info = true;
bool log_flag_error = true;

static void pre_system_check_and_init(void)
{
	upgrade_step_info_t step_info = {0};

	running_partition();
	LOG_INFO("My software is %s\n",SOFTWARE_VERSION);
	if(nv_init() == -1){
		delete_all_upgrade_file();
	}
	poll_task_init();
	garbage_cleanup_init();
	vTaskDelay(1000 / portTICK_PERIOD_MS);
	if(get_upgrade_step(&step_info) != 0){
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		set_upgrade_file(&step_info);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		esp_restart();
	}
	LOG_INFO("startup %d,before status %d,retry %d,file_path: %s,file_size: %d\n",
									step_info.upgrade_step,
									step_info.before_step_status,step_info.retry,
									step_info.upgrade_file_name,step_info.upgrade_file_size);

	if(step_info.upgrade_step == FACTORY_MODE_STARTUP){
		set_factory_mode(true);
	}else{
		set_factory_mode(false);
		doing_ota_upgrade(&step_info);
	}	
}

static void bsp_init(void)
{
	offline_log_init();

	//sleep_task_init(); //实现休眠唤醒机制，投票机制，预测系统休眠时长，进入睡眠或者保持wfi，可禁止休眠

	//poll_task_init();  //处理所有轮询驱动事件，回调机制，统一心跳，注册回调方式，休眠下无效

	tm1628_init();

	get_voltage_init(); //使用adc1 channel3 注册定时读取电池电量

	ext_gpio_init();

	display_task_init();//驱动层任务，CS模式，每个显示区域(时间/温度/湿度等)由唯一的应用负责，由应用直接调用
}

static void system_component_init(bool mute_on)
{
	audio_manager_init(mute_on);//音频管理任务，CS模式，维护play list和rec list，应用获取句柄后进行播放，
						  //管理任务通过回调知会应用结果（finish, abort, pause等）	

	play_tone_sync(NOTIFY_AUDIO_WELCOME);

    smart_connect_init();

	keyevent_dispatch_init();//按键事件处理，完成虚拟按键和物理按键的接收及转发，各应用注册需要的按键事件(长按，短按等)，
                          //回调知会各应用响应消息，允许多应用监听同一按键

	if(get_factory_mode() == false){
		
		download_server_init();//后台下载服务，CS模式，维护唯一的download list，按照各应用指定的期望延迟排序进行下载，完成时
							//知会各应用，未完成时，各应用可以查询状态，包括下载进度和异常原因等
		upload_server_init();//后台上传服务，CS模式，维护唯一的upload list，按照各应用指定的期望延迟排序进行上传，完成时
							//知会各应用，未完成时，各应用可以查询状态，包括上传进度和异常原因等
		alarm_init();

		ota_net_monitor_init();
	}else{
		#ifdef CONFIG_ENABLE_SHELL_SERVICE
		start_terminalctl_srv();//负责工厂模式下的at命令解析，维护at cmd table，通过函数列表完成调用，理论上所有模块测试不应执行在at
		#endif						//进程空间中，因由各模块单独执行，或者由fatcory at模块统一执行
	}

	acc_init();
}

static void app_init(void)
{
	if(get_factory_mode() == false){
		system_manager_init(); //负责维持与服务器的心跳，上传系统状态(电量，信号强度等)

		story_player_init(); //故事机应用，负责实时播放，sd卡故事管理，线上故事下载

		wechat_init(); //微信应用，负责微信语聊交互

		story_push_init(); //在线闹钟应用，负责在线闹钟的设置，更新，下载，播放
		remote_command_init();//负责与服务器的连接交互，实现链路层协议，剥离出裸数据传递给各应用，发送时，负责链路层封装，并通过
							//回调知会各应用发送结果(finsih, abort, timeout等)
							//当服务器断链时，通过回调广播模式知会各应用连接异常，并拒绝接受新请求，
							//当服务器重连时，丢弃所有残留数据，并知会各应用连接恢复
		deep_brain_service_init();//百度语音识别
	}
	
	led_init();//must init after wechat, for keyregister order
	restore_init();
	#ifdef CONFIG_ENABLE_POWER_MANAGER
	power_manager_init();
	#endif
}

void app_main()
{ 
	RESET_REASON rst_reas[2];
	nv_item_t nv;
	bool mute_on = false;

	gpio_config_t io_conf;

    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (uint64_t)(((uint64_t)1)<<PA_IO_NUM);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
	PA_DISABLE();

	LOG_ERR("before system start heap size (%d) (%d)\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), 
		heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

	pre_system_check_and_init();

    rst_reas[0] = rtc_get_reset_reason(0);
    rst_reas[1] = rtc_get_reset_reason(1);
    if(rst_reas[0] == RTCWDT_SYS_RESET || rst_reas[0] == TG0WDT_SYS_RESET
        || rst_reas[1] == RTCWDT_SYS_RESET || rst_reas[1] == TG0WDT_SYS_RESET){
		mute_on = true;
		nv.name = NV_ITEM_SYSTEM_PANIC_COUNT;
		if(ESP_OK == get_nv_item(&nv)){
			nv.value++;
			set_nv_item(&nv);
			LOG_INFO("system restart from panic, total count %lld, keep silent...\n", nv.value);
		}
	}

    bsp_init();
    system_component_init(mute_on);
	keypad_init();
	app_init();
	
	if(mute_on){
        nv.name = NV_ITEM_AUDIO_VOLUME;
        if(ESP_OK != get_nv_item(&nv))
            MediaHalSetVolume(DEFAULT_VOLUME);
        else
            MediaHalSetVolume((int)nv.value);
    }

	LOG_ERR("after system start heap size (%d) (%d)\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL), 
		heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
