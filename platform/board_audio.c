/* SPDX-License-Identifier: Apache-2.0
 * Mixed STD/TDM audio configuration adapted from Waveshare's maintained BSP.
 * See vendor/waveshare-reference and upstream-lock.json. Display BSP remains 3.0.1.
 * ES8311 playback is stereo; ES7210 capture is four TDM slots, MIC1 in slot 0.
 */
#include "board_audio.h"
#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "driver/gpio.h"
static i2s_chan_handle_t tx,rx;
static const audio_codec_data_if_t* data_if;
static const audio_codec_gpio_if_t* gpio_if;
static const audio_codec_ctrl_if_t *spk_ctrl,*mic_ctrl;
static const audio_codec_if_t *spk_if,*mic_if;
static esp_codec_dev_handle_t speaker,microphone;
static bool initialized;
#define PINS { .mclk=BSP_I2S_MCLK,.bclk=BSP_I2S_SCLK,.ws=BSP_I2S_LCLK,.dout=BSP_I2S_DOUT,.din=BSP_I2S_DSIN }
static bool channels_enable(bool enabled){
    i2s_chan_handle_t channels[]={rx,tx};bool ok=true;
    for(unsigned i=0;i<2;++i)if(channels[i]){
        i2s_chan_info_t info={0};
        if(i2s_channel_get_info(channels[i],&info)!=ESP_OK){ok=false;continue;}
        if(info.is_enabled!=enabled){esp_err_t err=enabled?i2s_channel_enable(channels[i]):i2s_channel_disable(channels[i]);if(err!=ESP_OK)ok=false;}
    }
    return ok;
}
void board_audio_close(void){if(microphone)esp_codec_dev_close(microphone);if(speaker)esp_codec_dev_close(speaker);channels_enable(false);}
static void cleanup(void){
    board_audio_close();if(microphone){esp_codec_dev_delete(microphone);microphone=NULL;}if(speaker){esp_codec_dev_delete(speaker);speaker=NULL;}
    if(mic_if){audio_codec_delete_codec_if(mic_if);mic_if=NULL;}if(spk_if){audio_codec_delete_codec_if(spk_if);spk_if=NULL;}
    if(mic_ctrl){audio_codec_delete_ctrl_if(mic_ctrl);mic_ctrl=NULL;}if(spk_ctrl){audio_codec_delete_ctrl_if(spk_ctrl);spk_ctrl=NULL;}
    if(gpio_if){audio_codec_delete_gpio_if(gpio_if);gpio_if=NULL;}if(data_if){audio_codec_delete_data_if(data_if);data_if=NULL;}
    if(rx){i2s_del_channel(rx);rx=NULL;}if(tx){i2s_del_channel(tx);tx=NULL;}initialized=false;
}
bool board_audio_init(void){
    if(initialized)return true;
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM,I2S_ROLE_MASTER);channel.auto_clear=true;
    i2s_std_config_t output={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(24000),.slot_cfg=I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),.gpio_cfg=PINS};
    i2s_tdm_config_t input={.clk_cfg=I2S_TDM_CLK_DEFAULT_CONFIG(24000),.slot_cfg=I2S_TDM_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO,I2S_TDM_SLOT0|I2S_TDM_SLOT1|I2S_TDM_SLOT2|I2S_TDM_SLOT3),.gpio_cfg=PINS};
    output.gpio_cfg.din=I2S_GPIO_UNUSED;output.clk_cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;
    input.gpio_cfg.dout=I2S_GPIO_UNUSED;input.clk_cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;input.clk_cfg.bclk_div=8;input.slot_cfg.total_slot=4;
    if(i2s_new_channel(&channel,&tx,&rx)!=ESP_OK||i2s_channel_init_std_mode(tx,&output)!=ESP_OK||i2s_channel_init_tdm_mode(rx,&input)!=ESP_OK||i2s_channel_enable(tx)!=ESP_OK||i2s_channel_enable(rx)!=ESP_OK)goto fail;
    audio_codec_i2s_cfg_t bus={.port=CONFIG_BSP_I2S_NUM,.rx_handle=rx,.tx_handle=tx};data_if=audio_codec_new_i2s_data(&bus);gpio_if=audio_codec_new_gpio();if(!data_if||!gpio_if)goto fail;
    audio_codec_i2c_cfg_t ctrl={.port=BSP_I2C_NUM,.addr=ES8311_CODEC_DEFAULT_ADDR,.bus_handle=bsp_i2c_get_handle()};spk_ctrl=audio_codec_new_i2c_ctrl(&ctrl);
    ctrl.addr=ES7210_CODEC_DEFAULT_ADDR;mic_ctrl=audio_codec_new_i2c_ctrl(&ctrl);if(!spk_ctrl||!mic_ctrl)goto fail;
    es8311_codec_cfg_t spk={.ctrl_if=spk_ctrl,.gpio_if=gpio_if,.codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC,.pa_pin=BSP_POWER_AMP_IO,.pa_reverted=false,.master_mode=false,.use_mclk=true,.digital_mic=false,.hw_gain={.pa_voltage=3.3,.codec_dac_voltage=3.3}};
    es7210_codec_cfg_t mic={.ctrl_if=mic_ctrl,.mic_selected=7};spk_if=es8311_codec_new(&spk);mic_if=es7210_codec_new(&mic);if(!spk_if||!mic_if)goto fail;
    esp_codec_dev_cfg_t out={.dev_type=ESP_CODEC_DEV_TYPE_OUT,.codec_if=spk_if,.data_if=data_if};speaker=esp_codec_dev_new(&out);
    esp_codec_dev_cfg_t in={.dev_type=ESP_CODEC_DEV_TYPE_IN,.codec_if=mic_if,.data_if=data_if};microphone=esp_codec_dev_new(&in);if(!speaker||!microphone)goto fail;
    // Keep the disabled amplifier's low output across automatic light sleep.
    if(gpio_sleep_sel_dis(BSP_POWER_AMP_IO)!=ESP_OK)goto fail;
    initialized=true;return true;
fail:cleanup();ESP_LOGE("assistant_audio","Audio initialization failed");return false;
}
bool board_audio_play_open(int volume){
    if(!board_audio_init())return false;board_audio_close();if(!channels_enable(true)){board_audio_close();return false;}
    esp_codec_dev_sample_info_t out={.bits_per_sample=16,.channel=2,.sample_rate=24000};
    if(esp_codec_dev_open(speaker,&out)!=ESP_OK){board_audio_close();return false;}
    esp_codec_dev_set_out_vol(speaker,volume);esp_codec_dev_set_out_mute(speaker,volume==0);return true;
}
bool board_audio_record_open(void){
    if(!board_audio_play_open(0))return false;
    esp_codec_dev_sample_info_t in={.bits_per_sample=16,.channel=4,.channel_mask=15,.sample_rate=24000};
    if(esp_codec_dev_open(microphone,&in)!=ESP_OK){board_audio_close();return false;}
    esp_codec_dev_set_in_channel_gain(microphone,3,30.0f);return true;
}
bool board_audio_read(int16_t* samples,size_t count){return esp_codec_dev_read(microphone,samples,count*sizeof(int16_t))==ESP_OK;}
bool board_audio_write(int16_t* samples,size_t count){return esp_codec_dev_write(speaker,samples,count*sizeof(int16_t))==ESP_OK;}
void board_audio_volume(int volume){if(speaker){esp_codec_dev_set_out_vol(speaker,volume);esp_codec_dev_set_out_mute(speaker,volume==0);}}
