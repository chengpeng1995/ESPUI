/* SPDX-License-Identifier: Apache-2.0
 * CO5300 initialization and pin mapping adapted from Waveshare BSP 3.0.1.
 * Dirty-region rendering + GPIO13 TE synchronization. Persistent internal DMA
 * staging avoids allocating a full-frame SPI bounce buffer from internal RAM.
 * BSP 3.0.1 continues to own touch, I2C, locks and the LVGL worker task.
 */
#include "board_display.h"
#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "esp_lv_adapter.h"
#include "esp_lv_adapter_input.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdatomic.h>

#define LCD_ROWS 20
#define LCD_TE GPIO_NUM_13
#define LCD_CLOCK_HZ (80 * 1000 * 1000)
static const char* TAG="assistant_display";
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t panel_io;
static esp_lcd_touch_handle_t touch_device;
static SemaphoreHandle_t te_signal, dma_done, frame_done, io_mutex, phase_ready;
static esp_timer_handle_t phase_timer;
typedef struct {lv_area_t area;uint8_t* pixels;uint32_t stride;bool last;} frame_job_t;
static QueueHandle_t frame_queue;
static uint16_t* dma_buffers[2];
static volatile uint32_t te_edges,te_period_us,te_timeouts,dma_errors,frames,transfer_us;
static uint32_t dirty_pixels,last_pixels,early_frames,full_frames;
static atomic_uint queued_regions;
static uint32_t irq_to_read_us,irq_to_read_max_us,touch_read_us;
static atomic_int touch_irq_at;
static int64_t last_te;
static atomic_bool wake_armed,consume_touch;
static void (*touch_wake_callback)(void*);
static void* touch_wake_context;
static uint32_t touch_reads,touch_acks,touch_ack_errors;
static assistant_touch_sample_t last_touch_sample;
static esp_err_t read_acknowledged_touch(esp_lcd_touch_handle_t touch){
    // Read D000 without the BSP's unconditional 2 ms delay or reset-on-error.
    // The CST92xx upstream uses a write/read transaction followed by this ACK.
    const uint8_t reg=0;
    memset(&last_touch_sample,0,sizeof(last_touch_sample));
    esp_err_t err=esp_lcd_panel_io_tx_param(touch->io,0xD0,&reg,1);
    if(err==ESP_OK)err=esp_lcd_panel_io_rx_param(touch->io,-1,last_touch_sample.raw,sizeof(last_touch_sample.raw));
    ++touch_reads;
    // CST92xx report handshake, matching SensorLib TouchDrvCST92xx::getTouchPoints:
    // D0 00 AB acknowledges the packet. Without it an old down report can be
    // read again during sleep preparation and mistaken for a new contact.
    const uint8_t ack[]={0x00,0xAB};
    const esp_err_t acknowledged=esp_lcd_panel_io_tx_param(touch->io,0xD0,ack,sizeof(ack));
    if(acknowledged==ESP_OK)++touch_acks;
    else {++touch_ack_errors;ESP_LOGW(TAG,"Touch report ACK failed: %s",esp_err_to_name(acknowledged));}
    last_touch_sample.irq=gpio_get_level(GPIO_NUM_11);
    if(err==ESP_OK&&acknowledged==ESP_OK)last_touch_sample.packet=touch_packet_decode(last_touch_sample.raw,466,466);
    else if(err==ESP_OK)err=acknowledged;
    const touch_packet_t p=last_touch_sample.packet;
    // Consumed packets do not invent a release while a pointer is held.
    if(p.kind!=TOUCH_PACKET_CONSUMED){
        portENTER_CRITICAL(&touch->data.lock);touch->data.points=0;
        if(p.kind==TOUCH_PACKET_DOWN){touch->data.points=1;touch->data.coords[0].x=p.x;touch->data.coords[0].y=p.y;touch->data.coords[0].strength=1;}
        portEXIT_CRITICAL(&touch->data.lock);
    }
    if(err!=ESP_OK)return err;
    return p.kind==TOUCH_PACKET_INVALID?ESP_ERR_INVALID_RESPONSE:ESP_OK;
}
static void IRAM_ATTR touch_interrupt(esp_lcd_touch_handle_t touch,void* ctx){
    (void)touch;(void)ctx;
    atomic_store(&touch_irq_at,(uint32_t)esp_timer_get_time());
    if(atomic_exchange(&wake_armed,false)&&touch_wake_callback){
        // GPIO wake changes the normal interrupt to level-triggered. Mask it
        // on the first edge so a held touch cannot starve tasks/USB in an ISR storm.
        gpio_intr_disable(GPIO_NUM_11);touch_wake_callback(touch_wake_context);
    }
}
static esp_err_t read_touch(esp_lcd_touch_handle_t touch,esp_lcd_touch_point_data_t* points,uint8_t* count,uint8_t max,void* ctx){
    (void)ctx;const uint32_t started=(uint32_t)esp_timer_get_time();
    const uint32_t irq=atomic_exchange(&touch_irq_at,0);
    if(irq){irq_to_read_us=started-irq;if(irq_to_read_us>irq_to_read_max_us)irq_to_read_max_us=irq_to_read_us;}
    esp_err_t err=read_acknowledged_touch(touch);*count=0;
    if(err==ESP_OK)err=esp_lcd_touch_get_data(touch,points,count,max);
    if(atomic_load(&consume_touch)){
        if(err==ESP_OK&&*count==0&&!atomic_load(&wake_armed))atomic_store(&consume_touch,false);
        *count=0;
    }
    touch_read_us=(uint32_t)esp_timer_get_time()-started;return err;
}
void assistant_display_touch_wake_callback(void (*callback)(void*),void* context){touch_wake_callback=callback;touch_wake_context=context;}
void assistant_display_touch_guard(bool armed,bool consume){atomic_store(&consume_touch,consume);atomic_store(&wake_armed,armed);}
esp_err_t assistant_display_touch_wake_read(bool* pressed){
    if(!pressed||!touch_device)return ESP_ERR_INVALID_ARG;
    *pressed=false;esp_lcd_touch_point_data_t points[1];uint8_t count=0;
    esp_err_t err=read_acknowledged_touch(touch_device);
    if(err==ESP_OK)err=esp_lcd_touch_get_data(touch_device,points,&count,1);
    if(err==ESP_OK){*pressed=count>0&&last_touch_sample.packet.kind==TOUCH_PACKET_DOWN;if(*pressed)ESP_LOGI(TAG,"TOUCH_WAKE_REPORT x=%u y=%u irq=%d",points[0].x,points[0].y,gpio_get_level(GPIO_NUM_11));}
    return err;
}
esp_err_t assistant_display_touch_wake_sample(assistant_touch_sample_t* sample){
    if(!sample||!touch_device)return ESP_ERR_INVALID_ARG;
    const esp_err_t err=read_acknowledged_touch(touch_device);*sample=last_touch_sample;return err;
}
esp_err_t assistant_display_touch_wake_arm(void){
    gpio_intr_disable(GPIO_NUM_11);
    // CST9217 asserts LOW for a fresh report. Never arm HIGH on a residual LOW:
    // that would treat an ACK/release transition as a new wake request.
    gpio_set_pull_mode(GPIO_NUM_11,GPIO_PULLUP_ONLY);
    gpio_sleep_set_direction(GPIO_NUM_11,GPIO_MODE_INPUT);
    gpio_sleep_set_pull_mode(GPIO_NUM_11,GPIO_PULLUP_ONLY);
    gpio_sleep_sel_en(GPIO_NUM_11);
    assistant_display_touch_guard(true,true);
    esp_err_t err=gpio_wakeup_enable(GPIO_NUM_11,GPIO_INTR_LOW_LEVEL);
    if(err==ESP_OK)err=gpio_intr_enable(GPIO_NUM_11);
    return err;
}
uint32_t assistant_display_te_edges(void){return te_edges;}
esp_err_t assistant_display_sleep(void){
    esp_err_t idle=assistant_display_wait_idle();if(idle!=ESP_OK)return idle;
    if(xSemaphoreTake(io_mutex,pdMS_TO_TICKS(1000))!=pdTRUE)return ESP_ERR_TIMEOUT;
    gpio_intr_disable(LCD_TE);
    esp_err_t err=esp_lcd_panel_io_tx_param(panel_io,0x02002800,NULL,0);
    if(err==ESP_OK)err=esp_lcd_panel_io_tx_param(panel_io,0x02001000,NULL,0);
    if(err==ESP_OK)vTaskDelay(pdMS_TO_TICKS(120));
    xSemaphoreGive(io_mutex);return err;
}
esp_err_t assistant_display_wake(void){
    if(xSemaphoreTake(io_mutex,pdMS_TO_TICKS(1000))!=pdTRUE)return ESP_ERR_TIMEOUT;
    // Standard Sleep Out retains registers. Do not use the driver's Deep Standby path.
    esp_err_t err=esp_lcd_panel_io_tx_param(panel_io,0x02001100,NULL,0);
    if(err==ESP_OK)vTaskDelay(pdMS_TO_TICKS(120));
    if(err==ESP_OK)err=esp_lcd_panel_io_tx_param(panel_io,0x02002900,NULL,0);
    xSemaphoreTake(te_signal,0);last_te=0;gpio_intr_enable(LCD_TE);
    xSemaphoreGive(io_mutex);return err;
}

static const co5300_lcd_init_cmd_t init_commands[]={
    {0xFE,(uint8_t[]){0x20},1,0},{0x19,(uint8_t[]){0x10},1,0},{0x1C,(uint8_t[]){0xA0},1,0},
    {0xFE,(uint8_t[]){0x00},1,0},{0xC4,(uint8_t[]){0x80},1,0},{0x3A,(uint8_t[]){0x55},1,0},
    {0x35,(uint8_t[]){0x00},1,0},{0x53,(uint8_t[]){0x20},1,0},{0x51,(uint8_t[]){0xFF},1,0},
    {0x63,(uint8_t[]){0xFF},1,0},{0x2A,(uint8_t[]){0x00,0x06,0x01,0xD7},4,0},
    {0x2B,(uint8_t[]){0x00,0x00,0x01,0xD1},4,600},
    {0x11,NULL,0,600},{0x29,NULL,0,0},
};
static void IRAM_ATTR te_interrupt(void* unused){
    (void)unused;
    int64_t now=esp_timer_get_time();if(last_te)te_period_us=(uint32_t)(now-last_te);last_te=now;++te_edges;
    BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(te_signal,&wake);if(wake)portYIELD_FROM_ISR();
}
static bool IRAM_ATTR transfer_complete(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t* event,void* ctx){
    (void)io;(void)event;(void)ctx;BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(dma_done,&wake);return wake==pdTRUE;
}
static bool wait_dma(void){
    if(xSemaphoreTake(dma_done,pdMS_TO_TICKS(1000))==pdTRUE)return true;
    ++dma_errors;ESP_LOGE(TAG,"DMA completion timeout");return false;
}
static void phase_elapsed(void* unused){(void)unused;xSemaphoreGive(phase_ready);}
static void transmit_frame(const lv_area_t* area,uint8_t* color_map,uint32_t stride,bool last){
    const int width=lv_area_get_width(area),height=lv_area_get_height(area);
    // Packed dirty rectangle; synchronize once per region, never per DMA chunk.
    xSemaphoreTake(te_signal,0);
    if(xSemaphoreTake(te_signal,pdMS_TO_TICKS(100))!=pdTRUE){++te_timeouts;ESP_LOGW(TAG,"TE timeout; frame sent without synchronization");}
    else {
        uint32_t period=te_period_us;if(period<12000||period>25000)period=16900;
        // Central rectangles can finish ahead of scan. Budget at 24 ms/full
        // frame and retain a 1 ms margin. Large/top-edge updates keep half-scan.
        const uint32_t budget=(uint64_t)width*height*24000/(466*466);
        const uint32_t first_scan=(uint64_t)area->y1*period/466;
        const uint32_t last_scan=(uint64_t)(area->y2+1)*period/466;
        const uint32_t age=(uint32_t)(esp_timer_get_time()-last_te);
        if(age+1000<first_scan&&age+budget+1000<last_scan){++early_frames;}
        else {ESP_ERROR_CHECK(esp_timer_start_once(phase_timer,period/2));xSemaphoreTake(phase_ready,portMAX_DELAY);}
    }
    const int64_t started=esp_timer_get_time();
    bool pending=false;int slot=0;
    const int chunk_rows=466*LCD_ROWS/width;
    for(int row=0;row<height;row+=chunk_rows){
        int rows=height-row;if(rows>chunk_rows)rows=chunk_rows;
        uint16_t* target=dma_buffers[slot];
        if(stride==(uint32_t)width*2)memcpy(target,color_map+row*stride,width*rows*2);
        else for(int y=0;y<rows;++y)memcpy(target+y*width,color_map+(row+y)*stride,width*2);
        lv_draw_sw_rgb565_swap(target,width*rows);
        if(pending&&!wait_dma())return;
        esp_err_t result=esp_lcd_panel_draw_bitmap(panel,area->x1,area->y1+row,area->x2+1,area->y1+row+rows,target);
        if(result!=ESP_OK){++dma_errors;ESP_LOGE(TAG,"Display transfer failed: %s",esp_err_to_name(result));return;}
        pending=true;slot^=1;
    }
    if(pending&&!wait_dma())return;
    transfer_us=(uint32_t)(esp_timer_get_time()-started);last_pixels=width*height;
    dirty_pixels+=last_pixels;if(last_pixels==466*466)++full_frames;if(last)++frames;
}
static void transfer_worker(void* unused){
    (void)unused;frame_job_t job;
    while(true){
        if(xQueueReceive(frame_queue,&job,portMAX_DELAY)!=pdTRUE)continue;
        xSemaphoreTake(io_mutex,portMAX_DELAY);transmit_frame(&job.area,job.pixels,job.stride,job.last);xSemaphoreGive(io_mutex);
        atomic_fetch_sub(&queued_regions,1);
        // LVGL's flush-wait callback releases the frame on the UI thread.
        xSemaphoreGive(frame_done);
    }
}
static void flush_frame(lv_display_t* display,const lv_area_t* area,uint8_t* pixels){
    // PARTIAL buffers are packed at the dirty rectangle's width. Two complete
    // scratch buffers let LVGL draw while DMA owns the previous immutable area.
    frame_job_t job={.area=*area,.pixels=pixels,.stride=lv_draw_buf_width_to_stride(lv_area_get_width(area),LV_COLOR_FORMAT_RGB565),.last=lv_display_flush_is_last(display)};
    atomic_fetch_add(&queued_regions,1);
    xQueueSend(frame_queue,&job,portMAX_DELAY);
}
static void flush_wait(lv_display_t* display){(void)display;xSemaphoreTake(frame_done,portMAX_DELAY);}
esp_err_t assistant_display_wait_idle(void){
    const int64_t until=esp_timer_get_time()+1000000;
    while(atomic_load(&queued_regions)){
        if(esp_timer_get_time()>=until)return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}
esp_err_t assistant_display_brightness_set(int brightness){
    if(!panel_io||brightness<0||brightness>100)return ESP_ERR_INVALID_ARG;
    uint8_t value=(uint8_t)(brightness*255/100);
    xSemaphoreTake(io_mutex,portMAX_DELAY);
    esp_err_t result=esp_lcd_panel_io_tx_param(panel_io,0x02005100,&value,1);
    xSemaphoreGive(io_mutex);return result;
}
uint32_t assistant_display_completed_frames(void){return frames;}
void assistant_display_log_stats(void){
    ESP_LOGI(TAG,"DISPLAY te_edges=%lu te_period_us=%lu timeouts=%lu dma_errors=%lu frames=%lu transfer_us=%lu qspi_mhz=80 phase=adaptive",
             (unsigned long)te_edges,(unsigned long)te_period_us,(unsigned long)te_timeouts,(unsigned long)dma_errors,(unsigned long)frames,(unsigned long)transfer_us);
    ESP_LOGI(TAG,"TOUCH reads=%lu acks=%lu ack_errors=%lu",(unsigned long)touch_reads,(unsigned long)touch_acks,(unsigned long)touch_ack_errors);
    ESP_LOGI(TAG,"DIRTY pixels=%lu last_pixels=%lu early_frames=%lu full_frames=%lu mode=partial",(unsigned long)dirty_pixels,(unsigned long)last_pixels,(unsigned long)early_frames,(unsigned long)full_frames);
    ESP_LOGI(TAG,"INPUT irq_to_read_us=%lu max_us=%lu read_us=%lu scope=irq_to_driver_not_photon",(unsigned long)irq_to_read_us,(unsigned long)irq_to_read_max_us,(unsigned long)touch_read_us);
}
lv_display_t* assistant_display_start(void){
    te_signal=xSemaphoreCreateBinary();dma_done=xSemaphoreCreateBinary();assert(te_signal&&dma_done);
    frame_done=xSemaphoreCreateBinary();io_mutex=xSemaphoreCreateMutex();frame_queue=xQueueCreate(1,sizeof(frame_job_t));assert(frame_done&&io_mutex&&frame_queue);
    phase_ready=xSemaphoreCreateBinary();assert(phase_ready);
    const esp_timer_create_args_t timer={.callback=phase_elapsed,.name="display_phase"};ESP_ERROR_CHECK(esp_timer_create(&timer,&phase_timer));
    for(int i=0;i<2;++i){dma_buffers[i]=heap_caps_aligned_alloc(4,466*LCD_ROWS*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);assert(dma_buffers[i]);}
    esp_lv_adapter_config_t adapter=ESP_LV_ADAPTER_DEFAULT_CONFIG();adapter.task_core_id=1;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter));
    const spi_bus_config_t bus=CO5300_PANEL_BUS_QSPI_CONFIG(BSP_LCD_PCLK,BSP_LCD_DATA0,BSP_LCD_DATA1,BSP_LCD_DATA2,BSP_LCD_DATA3,466*LCD_ROWS*2);
    ESP_ERROR_CHECK(spi_bus_initialize(BSP_LCD_SPI_NUM,&bus,SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io=CO5300_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS,NULL,NULL);
    io.pclk_hz=LCD_CLOCK_HZ;io.trans_queue_depth=2;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM,&io,&panel_io));
    co5300_vendor_config_t vendor={.init_cmds=init_commands,.init_cmds_size=sizeof(init_commands)/sizeof(init_commands[0]),.flags={.use_qspi_interface=1}};
    esp_lcd_panel_dev_config_t config={.reset_gpio_num=BSP_LCD_RST,.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16,.vendor_config=&vendor};
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(panel_io,&config,&panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel,6,0));ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
    esp_lv_adapter_display_config_t display={.panel=panel,.panel_io=panel_io,
        .profile={.interface=ESP_LV_ADAPTER_PANEL_IF_OTHER,.rotation=ESP_LV_ADAPTER_ROTATE_0,.hor_res=466,.ver_res=466,
                  .buffer_height=466,.use_psram=true,.enable_ppa_accel=false,.require_double_buffer=true},
        .tear_avoid_mode=ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE};
    lv_display_t* result=esp_lv_adapter_register_display(&display);if(!result)return NULL;
    // The adapter owns the worker and input. Our flush owns the TE and DMA
    // completion protocol instead of the adapter's unsynchronized SPI path.
    lv_display_set_render_mode(result,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(result,flush_frame);lv_display_set_flush_wait_cb(result,flush_wait);
    const esp_lcd_panel_io_callbacks_t callbacks={.on_color_trans_done=transfer_complete};
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(panel_io,&callbacks,NULL));
    bsp_display_cfg_t board={.touch_flags={.swap_xy=0,.mirror_x=1,.mirror_y=1}};
    esp_lcd_touch_handle_t touch=NULL;ESP_ERROR_CHECK(bsp_touch_new(&board,&touch));
    touch_device=touch;
    // IDF isolates GPIO during automatic light sleep. These active-low
    // control pins must keep their existing output state, never float and
    // inadvertently reset the retained panel/touch controller.
    ESP_ERROR_CHECK(gpio_sleep_sel_dis(BSP_LCD_RST));
    ESP_ERROR_CHECK(gpio_sleep_sel_dis(BSP_LCD_TOUCH_RST));
    ESP_ERROR_CHECK(gpio_sleep_sel_dis(BSP_LCD_CS));
    esp_lv_adapter_touch_config_t input=ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(result,touch);
    lv_indev_t* indev=esp_lv_adapter_register_touch(&input);if(!indev)return NULL;
    lv_timer_set_period(lv_indev_get_read_timer(indev),8);
    lv_indev_set_scroll_limit(indev,6);
    lv_indev_set_scroll_throw(indev,18);
    const esp_lv_adapter_touch_callbacks_t touch_callbacks={.on_interrupt=touch_interrupt,.custom_touch_read=read_touch};
    ESP_ERROR_CHECK(esp_lv_adapter_set_touch_callbacks(indev,&touch_callbacks));
    gpio_config_t te={.pin_bit_mask=1ULL<<LCD_TE,.mode=GPIO_MODE_INPUT,.intr_type=GPIO_INTR_NEGEDGE};
    ESP_ERROR_CHECK(gpio_config(&te));esp_err_t isr=gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if(isr!=ESP_ERR_INVALID_STATE)ESP_ERROR_CHECK(isr);
    ESP_ERROR_CHECK(gpio_isr_handler_add(LCD_TE,te_interrupt,NULL));
    ESP_ERROR_CHECK(assistant_display_brightness_set(70));
    assert(xTaskCreatePinnedToCore(transfer_worker,"display_transfer",4096,NULL,7,NULL,0)==pdPASS);
    ESP_LOGI(TAG,"TE synchronized dirty-region display: GPIO13, 80 MHz QSPI, 2 x 20-row DMA staging");
    return result;
}
