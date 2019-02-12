#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "tcpip_adapter.h"
#include "TerminalService.h"
#include "boardconfig.h"
#include "audiomanagerservice.h"
#include "gc.h"
#include "keyevent.h"
#include "log.h"
#include "nvs.h"
#include "MediaHal.h"
#include "nv_interface.h"
#include "audio_manager.h"
#include "restore.h"
#include "MediaControl.h"
#include "led.h"
#include "userconfig.h"
#include "EspAudio.h"
#include "InterruptionSal.h"

#include "fdkaac_decoder.h"
#include "mp3_decoder.h"
#include "ogg_decoder.h"
#include "flac_decoder.h"
#include "wav_decoder.h"
#include "opus_decoder.h"
#include "aac_decoder.h"
#include "amr_decoder.h"
#include "pcm_decoder.h"
#include "pcm_encoder.h"
#include "amrnb_encoder.h"
#include "amrwb_encoder.h"
#include "opus_encoder.h"
#include "apus_encoder.h"
#include "apus_decoder.h"

#include "ES8388_interface.h"

#define LOG_TAG		"aman"
#define APP_TAG "AUDIO_MAIN"

const PlayerConfig playerConfig = {
    .bufCfg = {
#if (CONFIG_SPIRAM_BOOT_INIT || CONFIG_MEMMAP_SPIRAM_ENABLE_MALLOC)
        .inputBufferLength = 1 * 1024 * 1024, //input buffer size in bytes, this buffer is used to store the data downloaded from http or fetched from TF-Card or MIC
#else
        .inputBufferLength = 20 * 1024, //input buffer size in bytes, this buffer is used to store the data downloaded from http or fetched from TF-Card or MIC
#endif
        .outputBufferLength = 20 * 1024,//output buffer size in bytes, is used to store the data before play or write into TF-Card
        .processBufferLength = 10
    },
    //comment any of below streams to disable corresponding streams in order to save RAM.
    .inputStreamType = IN_STREAM_I2S    //this is recording stream that records data from mic
    | IN_STREAM_HTTP   //http stream can support fetching data from a website and play it
    | IN_STREAM_LIVE   //supports m3u8 stream playing.
    | IN_STREAM_FLASH  //support flash music playing, refer to [Flash Music] section in "docs/ESP-Audio_Programming_Guide.md" for detail
    | IN_STREAM_SDCARD //support TF-Card playing. Refer to "SDCard***.c" for more information
    | IN_STREAM_ARRAY  //for tone stored in array. Refre to 'EspAudioTonePlay()' for details
    ,
    .outputStreamType = OUT_STREAM_I2S      //output the music via i2s protocol, see i2s configuration in "MediaHal.c"
    | OUT_STREAM_SDCARD  //write the music data into TF-Card, usually for recording
    ,
    .playListEn = ESP_AUDIO_DISABLE,
    .toneEn = ESP_AUDIO_DISABLE,
    .playMode = MEDIA_PLAY_ONE_SONG,
};


static long vol_touch_abs(long a, long b)
{
    if(a > b){
        return (a-b);
    }else{
        return (b-a);
    }
}

static keyprocess_t audio_manager_keyprocess(keyevent_t *event)
{
	static struct timeval timestamp[2] = {{0,0}, {0,0}};
    long threshold;
	
    LOG_DBG("ams recevice key event %d type %d\n", event->code, event->type);

    if(get_factory_mode() == true && ((event->code == KEY_CODE_VOL_UP) || (event->code == KEY_CODE_VOL_DOWN))){
        LOG_DBG("In Factory mode not handler vol up and down \n");
        return KEY_PROCESS_PUBLIC;
    }
 
    switch(event->code){
        case KEY_CODE_VOL_UP:
			if(event->type == KEY_EVNET_PRESS){
				memcpy(&timestamp[0], &event->timestamp, sizeof(struct timeval));
            }else if(event->type == KEY_EVNET_RELEASE){
            	threshold = vol_touch_abs((long)(timestamp[0].tv_sec*1000+timestamp[0].tv_usec/1000),
                    (long)(timestamp[1].tv_sec*1000+timestamp[1].tv_usec/1000));
            	if(threshold > 200){
                	volume_up();
            	}
            }else{
                //ignore other type
            }
            break;
        case KEY_CODE_VOL_DOWN:
			if(event->type == KEY_EVNET_PRESS){
				memcpy(&timestamp[1], &event->timestamp, sizeof(struct timeval));
            }else if(event->type == KEY_EVNET_RELEASE){
            	threshold = vol_touch_abs((long)(timestamp[0].tv_sec*1000+timestamp[0].tv_usec/1000),
                    (long)(timestamp[1].tv_sec*1000+timestamp[1].tv_usec/1000));
            	if(threshold>200){
					volume_down();
				}	
            }else{
                //ignore other type
            }
            break;
        default:
            LOG_ERR("receive unexpect key (%d) event (%d)\n", event->code, event->type);
            break;
    }
    return KEY_PROCESS_PUBLIC;
}

void Init_MediaCodec()
{
    int ret = 0;
#if (defined CONFIG_CODEC_CHIP_IS_ES8388)
    Es8388Config  Es8388Conf =  AUDIO_CODEC_ES8388_DEFAULT();
    ret = MediaHalInit(&Es8388Conf);
    if (ret) {
        ESP_AUDIO_LOGE(APP_TAG, "MediaHal init failed, line:%d", __LINE__);
    }
    ESP_AUDIO_LOGI(APP_TAG, "CONFIG_CODEC_CHIP_IS_ES8388");
#elif (defined CONFIG_CODEC_CHIP_IS_ES8374)
    Es8374Config  Es8374Conf =  AUDIO_CODEC_ES8374_DEFAULT();
    ret = MediaHalInit(&Es8374Conf);
    if (ret) {
        ESP_AUDIO_LOGI(APP_TAG, "MediaHal init failed, line:%d", __LINE__);
    }
    ESP_AUDIO_LOGI(APP_TAG, "CONFIG_CODEC_CHIP_IS_ES8374");
#endif
}
void audio_manager_init(bool mute_on)
{
    MediaControl *player;
    int err;
    nv_item_t nv_volume;
	Init_MediaCodec();
    if (EspAudioInit(&playerConfig, &player) != AUDIO_ERR_NO_ERROR)
        return;
		
	EspAudioSamplingSetup(48000);

    /*---------------------------------------------------------
    |    Soft codec libraries supporting corresponding music   |
    |    Any library can be commented out to reduce bin size   |
    ----------------------------------------------------------*/
    /* decoder */
	
    ESP_LOGI(APP_TAG, "Manually select audio lib");

    EspAudioCodecLibAdd(CODEC_DECODER, 5 * 1024, "mp4", aac_decoder_open, aac_decoder_process, aac_decoder_close, aac_decoder_trigger_stop, aac_decoder_get_pos);
    EspAudioCodecLibAdd(CODEC_DECODER, 5 * 1024, "m4a", aac_decoder_open, aac_decoder_process, aac_decoder_close, aac_decoder_trigger_stop, aac_decoder_get_pos);
    EspAudioCodecLibAdd(CODEC_DECODER, 5 * 1024, "aac", aac_decoder_open, aac_decoder_process, aac_decoder_close, aac_decoder_trigger_stop, aac_decoder_get_pos);
	EspAudioCodecLibAdd(CODEC_DECODER, 5 * 1024, "amr", amr_decoder_open, amr_decoder_process, amr_decoder_close, NULL, amr_decoder_get_pos);
   	EspAudioCodecLibAdd(CODEC_DECODER, 5 * 1024, "Wamr", amr_decoder_open, amr_decoder_process, amr_decoder_close, NULL, amr_decoder_get_pos);
    EspAudioCodecLibAdd(CODEC_DECODER, 5 * 1024, "mp3", mp3_decoder_open, mp3_decoder_process, mp3_decoder_close, mp3_decoder_trigger_stop, mp3_decoder_get_pos);
    EspAudioCodecLibAdd(CODEC_DECODER, 5 * 1024, "mp3", mp3_decoder_open, mp3_decoder_process, mp3_decoder_close, mp3_decoder_trigger_stop, mp3_decoder_get_pos);
    EspAudioCodecLibAdd(CODEC_DECODER, 3 * 1024, "wav", wav_decoder_open, wav_decoder_process, wav_decoder_close, NULL, wav_decoder_get_pos);
    EspAudioCodecLibAdd(CODEC_DECODER, 3 * 1024, "pcm", pcm_decoder_open, pcm_decoder_process, pcm_decoder_close, NULL, pcm_decoder_get_pos);

    /* encoder */
    EspAudioCodecLibAdd(CODEC_ENCODER, 8 * 1024, "amr", amrnb_encoder_open, amrnb_encoder_process, amrnb_encoder_close, amrnb_encoder_trigger_stop, NULL); ///according to my test, 6 will stack overflow. 7 is ok. Here use 8
    EspAudioCodecLibAdd(CODEC_ENCODER, 15 * 1024, "Wamr", amrwb_encoder_open, amrwb_encoder_process, amrwb_encoder_close, amrwb_encoder_trigger_stop, NULL);
    EspAudioCodecLibAdd(CODEC_ENCODER, 35 * 1024, "apus", apus_encoder_open, apus_encoder_process, apus_encoder_close, NULL, NULL);
    EspAudioCodecLibAdd(CODEC_ENCODER, 50 * 1024, "opus", opus_encoder_open, opus_encoder_process, opus_encoder_close, NULL, NULL); ///for 1 channe, 35k is OK, for 2 ch, need more stack 50k
    EspAudioCodecLibAdd(CODEC_ENCODER, 2 * 1024, "pcm", pcm_encoder_open, pcm_encoder_process, pcm_encoder_close, NULL, NULL);

    /* Create Touch control service object, to control Player via Terminal, and add it ot MediaPlayer */
    AudioManagerService *audio = AudioManagerCreate();
    player->addService(player, (MediaService *)audio);
	register_sdcard_notify(player);
    /* Create Terminal control service object, to control Player via Terminal, and add it to MediaPlayer */
    TerminalControlService *term = TerminalControlCreate();
    player->addService(player, (MediaService *) term);

    /* Start Services and Device Controller*/
    player->activeServices(player);
    if(mute_on){
         MediaHalSetVolume(DEFAULT_VOLUME);
    }else{
        if(get_factory_mode() == false){
            nv_volume.name = NV_ITEM_AUDIO_VOLUME;
            err = get_nv_item(&nv_volume);
            if(err != ESP_OK)
                MediaHalSetVolume(DEFAULT_VOLUME);
            else
                MediaHalSetVolume((int)nv_volume.value);
        }else{
            MediaHalSetVolume(DEFAULT_VOLUME + 20);
        }
    }

    keyevent_register_listener(KEY_CODE_VOL_UP_MASK|KEY_CODE_VOL_DOWN_MASK, audio_manager_keyprocess);
}
