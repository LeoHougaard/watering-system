"""Exercise the actual reservoir task with simulated GPIO and time, without hardware."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root/'components/reservoir_manager/reservoir_manager.c').read_text()
source = re.sub(r'^#include .*\n', '', source, flags=re.M)
header = (root/'components/reservoir_manager/include/reservoir_manager.h').read_text()
header = re.sub(r'^#(?:include|pragma).*\n', '', header, flags=re.M)
stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
typedef int esp_err_t;
typedef unsigned TickType_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define ESP_LOGW(...) ((void)0)
#define xTaskCreate(...) pdPASS
typedef struct {bool reservoir_sensor_bypass, reservoir_sensor_enabled, reservoir_active_high;
int reservoir_gpio, reservoir_debounce_ms;} system_settings_t;
typedef struct {uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
static system_settings_t config={false,false,true,4,750};
static unsigned tick,step,configured,reads,logged;
static int raw=1,fail_config;
static jmp_buf finish;
static void config_store_get_settings(system_settings_t *out){*out=config;}
static int gpio_config(const gpio_config_t *io){assert(io->pin_bit_mask==(1ULL<<config.reservoir_gpio));configured++;return fail_config;}
static int gpio_get_level(int gpio){assert(gpio==config.reservoir_gpio);reads++;return raw;}
static TickType_t xTaskGetTickCount(void){return tick;}
static void history_store_log_reservoir(bool value){logged++;}
static void vTaskDelay(TickType_t duration);
'''
checks = r'''
static void vTaskDelay(TickType_t duration){
 tick+=duration;
 switch(step++) {
 case 0: assert(configured==0 && reads==0 && reservoir_manager_is_ok());
         assert(reservoir_manager_get_status().raw_level==-1);config.reservoir_sensor_enabled=true;break;
 case 1: assert(configured==1 && reads==1 && reservoir_manager_is_ok());
         config.reservoir_sensor_enabled=false;break;
 case 2: assert(reads==1 && reservoir_manager_get_status().raw_level==-1);
         config.reservoir_sensor_enabled=true;raw=0;break;
 case 3: assert(!reservoir_manager_is_ok());config.reservoir_gpio=7;break;
 case 4: assert(configured==2 && !reservoir_manager_is_ok());
         config.reservoir_sensor_bypass=true;break;
 case 5: assert(reservoir_manager_is_ok() && reservoir_manager_get_status().raw_level==-1);
         config.reservoir_sensor_bypass=false;config.reservoir_active_high=false;break;
 case 6: assert(reservoir_manager_is_ok());raw=1;break;
 case 7: assert(reservoir_manager_is_ok());break;
 case 8: assert(reservoir_manager_is_ok());tick+=800;break;
 case 9: assert(!reservoir_manager_is_ok() && logged==1);
         config.reservoir_gpio=8;fail_config=1;break;
 case 10:assert(!reservoir_manager_is_ok() && reservoir_manager_get_status().raw_level==-1);
         longjmp(finish,1);
 default:assert(0);
 }
}
int main(void){if(!setjmp(finish))reservoir_task(NULL);
 puts("Reservoir task: enable, disable, GPIO change, bypass, polarity, debounce and configuration failure passed");return 0;}
'''
with tempfile.TemporaryDirectory(prefix='watering-reservoir-') as directory:
    temp=Path(directory)
    (temp/'test.c').write_text(stubs+header+source+checks)
    subprocess.run(['gcc','-std=c11',str(temp/'test.c'),'-o',str(temp/'test.exe')],check=True)
    subprocess.run([str(temp/'test.exe')],check=True)
