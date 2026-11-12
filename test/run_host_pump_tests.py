"""Exercise the actual pump task with simulated GPIO, queues and two clocks."""
import os,re,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
idf=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/v5.5.1/esp-idf')))
source=re.sub(r'^#include .*\n','',(root/'components/pump_controller/pump_controller.c').read_text(),flags=re.M)
stubs=r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "irrigation_model.h"
runtime_state_t pump_controller_get_runtime(void);
typedef int esp_err_t;
typedef void *QueueHandle_t;
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
typedef struct {bool ok,bypass,active_high;int gpio,raw_level;} reservoir_status_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_TIMEOUT 3
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define pdPASS 1
#define xTaskCreate(...) pdPASS
static system_settings_t config;
static bool reservoir_ok=true;
static time_t wall=1800000000;
static int64_t monotonic=1000000;
static int pins[64],queued,received,logged;
static unsigned item_size;
static unsigned char queue_items[8][128];
static watering_event_t last_event;
static jmp_buf iteration;
static time_t fake_time(time_t *out){if(out)*out=wall;return wall;}
#define time fake_time
static int64_t esp_timer_get_time(void){return monotonic;}
static void config_store_get_settings(system_settings_t *s){*s=config;}
static bool reservoir_manager_is_ok(void){return reservoir_ok;}
static reservoir_status_t reservoir_manager_get_status(void){return (reservoir_status_t){.ok=reservoir_ok};}
static void history_store_log_watering(const watering_event_t *event){last_event=*event;logged++;}
static void gpio_set_level(int pin,int level){assert(pin>=0&&pin<64);pins[pin]=level;}
static void gpio_set_direction(int pin,int mode){}
static int gpio_config(const gpio_config_t *io){return 0;}
static QueueHandle_t xQueueCreate(int length,unsigned size){assert(size<=128);item_size=size;queued=0;return (void*)1;}
static void vQueueDelete(QueueHandle_t queue){}
static int xQueueReset(QueueHandle_t queue){queued=0;return 1;}
static int xQueueSend(QueueHandle_t queue,const void *item,int wait){if(queued==8)return 0;memcpy(queue_items[queued++],item,item_size);return 1;}
static int xQueueSendToFront(QueueHandle_t queue,const void *item,int wait){if(queued==8)return 0;memmove(queue_items+1,queue_items,queued*128);memcpy(queue_items,item,item_size);queued++;return 1;}
static int xQueueReceive(QueueHandle_t queue,void *item,int wait){
 if(received++)longjmp(iteration,1);
 if(!queued)return 0;
 memcpy(item,queue_items[0],item_size);memmove(queue_items,queue_items+1,--queued*128);return 1;
}
'''
tests=r'''
static void tick(void){received=0;if(!setjmp(iteration))pump_task(NULL);}
static void reset(void){
 model_defaults_settings(&config);config.scheduled_max_run_sec=600;
 reservoir_ok=true;wall=1800000000;monotonic=1000000;logged=0;
 g_running=false;g_runtime=(runtime_state_t){.pump_state=PUMP_STATE_IDLE,.reservoir_ok=true};
 g_cooldown_until_us=0;pump_controller_preinit_safe_off();assert(pins[5]==1);
 assert(pump_controller_start()==ESP_OK&&pins[5]==1);
}
int main(void){
 reset();pump_controller_manual_run(60,"test");tick();assert(g_running&&pins[5]==0);
 monotonic+=30*1000000LL;wall-=86400;tick();assert(g_runtime.current_run_remaining_sec==30);
 monotonic+=30*1000000LL;wall+=2*86400;tick();assert(!g_running&&pins[5]==1&&last_event.duration_sec==60);
 puts("Clock corrections cannot extend or shorten pump runtime passed");
 reset();pump_controller_scheduled_run(60);tick();monotonic+=60*1000000LL;tick();
 assert(g_runtime.pump_state==PUMP_STATE_COOLDOWN);wall-=86400;monotonic+=300*1000000LL;tick();
 assert(g_runtime.pump_state==PUMP_STATE_IDLE);
 puts("Cooldown expires independently of wall clock passed");
 reset();config.manual_max_run_sec=20;pump_controller_manual_run(60,"test");tick();assert(g_runtime.current_run_remaining_sec==20);
 monotonic+=20*1000000LL;tick();assert(!g_running&&last_event.duration_sec==20);
 puts("Fresh runtime limits applied without reboot passed");
 reset();pump_controller_manual_run(60,"test");pump_controller_manual_run(60,"test");
 pump_controller_stop(STOP_MANUAL);assert(queued==1);tick();tick();assert(!g_running&&pins[5]==1);
 puts("Stop clears queued starts passed");
 reset();pump_controller_scheduled_run(120);config.auto_mode_enabled=false;tick();assert(!g_running&&pins[5]==1);
 puts("Automation off prevents a queued scheduled start passed");
 reset();pump_controller_manual_run(60,"test");tick();reservoir_ok=false;tick();
 assert(!g_running&&pins[5]==1&&last_event.stopped_reason==STOP_LOW_RESERVOIR);
 puts("Low reservoir stops the pump passed");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='watering-pump-') as directory:
 temp=Path(directory);(temp/'test.c').write_text(stubs+source+tests)
 cjson=idf/'components/json/cJSON'
 subprocess.run(['gcc','-std=c11','-I'+str(root/'components/irrigation_model/include'),'-I'+str(cjson),str(temp/'test.c'),str(root/'components/irrigation_model/irrigation_model.c'),str(cjson/'cJSON.c'),'-o',str(temp/'test.exe')],check=True)
 subprocess.run([str(temp/'test.exe')],check=True)
