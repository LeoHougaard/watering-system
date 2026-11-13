"""Exercise the real moisture task/getter with ADC and clock fixtures, then UI JS."""
import ast
import os
import json
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'components/moisture_manager/moisture_manager.c').read_text()
source = re.sub(r'^#include .*\n', '', source, flags=re.M)
stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <setjmp.h>
#include "moisture_manager.h"
typedef struct {bool moisture_sensor_enabled; int moisture_gpio; uint32_t moisture_dry_raw,moisture_wet_raw,moisture_average_count,moisture_sample_interval_sec,moisture_history_days,moisture_live_interval_sec;} system_settings_t;
static system_settings_t config={true,1,3000,1000,5,3600,365,60};
typedef void *adc_oneshot_unit_handle_t;
typedef int adc_channel_t;
typedef unsigned TickType_t;
typedef struct {int unit_id;} adc_oneshot_unit_init_cfg_t;
typedef struct {int bitwidth,atten;} adc_oneshot_chan_cfg_t;
#define ADC_CHANNEL_0 0
#define ADC_CHANNEL_1 1
#define ADC_CHANNEL_2 2
#define ADC_CHANNEL_3 3
#define ADC_CHANNEL_4 4
#define ADC_CHANNEL_5 5
#define ADC_CHANNEL_6 6
#define ADC_CHANNEL_7 7
#define ADC_CHANNEL_8 8
#define ADC_CHANNEL_9 9
#define ADC_UNIT_1 1
#define ADC_BITWIDTH_DEFAULT 12
#define ADC_ATTEN_DB_12 12
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdPASS 1
#define xTaskCreate(...) pdPASS
#define pdMS_TO_TICKS(x) (x)
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
static int step,logs,init_fail,cfg_fail;
static int64_t clock_us=1000000;
static jmp_buf done;
static int64_t esp_timer_get_time(void){return clock_us;}
static TickType_t xTaskGetTickCount(void){return clock_us/1000;}
static void config_store_get_settings(system_settings_t *s){*s=config;}
static void history_store_log_diagnostic(const char *s){}
static void history_store_log_moisture(int p,int raw,int gpio){assert(p>=0&&raw>0&&raw<4095);logs++;}
static void history_store_trim_moisture(unsigned days){}
static int adc_oneshot_new_unit(adc_oneshot_unit_init_cfg_t *c,void **u){*u=(void*)1;return init_fail;}
static int adc_oneshot_del_unit(void *u){return 0;}
static int adc_oneshot_config_channel(void *u,int c,adc_oneshot_chan_cfg_t *cfg){return cfg_fail;}
static int adc_oneshot_read(void *u,int c,int *raw){
 *raw=2000;
 if(step==5)*raw=4095;
 if(step==11)return 1;
 if(step==17)*raw=0;
 if(step==24)*raw=-1;
 if(step==29)*raw=4096;
 return 0;
}
static void vTaskDelay(unsigned ticks);
'''
tests = r'''
static void check(moisture_quality_t q){
 moisture_reading_t r=moisture_manager_get_latest();
 assert(r.quality==q);
 assert(r.percent==(q==MOISTURE_VALID?50:-1));
 moisture_latest_get(NULL);
}
static void vTaskDelay(unsigned ticks){
 if(step<5)check(step==4?MOISTURE_VALID:MOISTURE_WARMING);
 if(step==5)check(MOISTURE_SATURATED);
 if(step>5&&step<11)check(step==10?MOISTURE_VALID:MOISTURE_WARMING);
 if(step==11)check(MOISTURE_READ_ERROR);
 if(step>11&&step<17)check(step==16?MOISTURE_VALID:MOISTURE_WARMING);
 if(step==17||step==24||step==29)check(MOISTURE_SATURATED);
 if(step==22){
  check(MOISTURE_VALID);
  clock_us+=301LL*1000000;check(MOISTURE_STALE);
 }
 if(step==23)check(MOISTURE_WARMING);
 if(step==30)check(MOISTURE_WARMING);
 if(step==34){
  check(MOISTURE_VALID);
  config.moisture_dry_raw=1000;check(MOISTURE_INVALID_CONFIG);
 }
 if(step==35){check(MOISTURE_INVALID_CONFIG);config.moisture_sensor_enabled=false;}
 if(step==36){check(MOISTURE_DISABLED);config.moisture_sensor_enabled=true;config.moisture_dry_raw=3000;}
 if(step==37)check(MOISTURE_WARMING);
 if(step==41){check(MOISTURE_VALID);config.moisture_gpio=99;check(MOISTURE_INVALID_CONFIG);}
 if(step==42){check(MOISTURE_INVALID_CONFIG);config.moisture_gpio=2;init_fail=1;}
 if(step==43){check(MOISTURE_READ_ERROR);init_fail=0;cfg_fail=1;}
 if(step==44){check(MOISTURE_READ_ERROR);cfg_fail=0;}
 if(step==45)check(MOISTURE_WARMING);
 if(step==49){check(MOISTURE_VALID);assert(logs>0);longjmp(done,1);}
 step++;clock_us+=60000000;
}
int main(void){if(!setjmp(done))moisture_task(NULL);return 0;}
'''
with tempfile.TemporaryDirectory(prefix='watering-quality-') as directory:
    temp = Path(directory)
    (temp/'esp_err.h').write_text('typedef int esp_err_t;\n')
    api_source = (root/'components/api_server/api_server.c').read_text()
    handler = api_source[api_source.index('static esp_err_t moisture_latest_get('):api_source.index('static esp_err_t diagnostics_get(')]
    bridge = r'''
#include "cJSON.h"
#include <stdlib.h>
typedef int httpd_req_t;
static int send_json(httpd_req_t *req,cJSON *root){char *s=cJSON_PrintUnformatted(root);puts(s);free(s);cJSON_Delete(root);return 0;}
'''
    (temp/'test.c').write_text(stubs+source+bridge+handler+tests)
    idf = Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/v5.5.1/esp-idf')))
    cjson = idf/'components/json/cJSON'
    subprocess.run(['gcc','-std=c11','-I'+str(temp),'-I'+str(root/'components/moisture_manager/include'),'-I'+str(cjson),str(temp/'test.c'),str(cjson/'cJSON.c'),'-o',str(temp/'test.exe')],check=True)
    result = subprocess.run([str(temp/'test.exe')],check=True,capture_output=True,text=True)
    fixtures = [json.loads(line) for line in result.stdout.splitlines()]
    # Decode the actual concatenated C strings and syntax-check every embedded script.
    api = (root/'components/api_server/api_server.c').read_text()
    literals = re.findall(r'^"(?:[^"\\]|\\.)*"',api,re.M)
    (temp/'html.c').write_text('static const char html[] =\n'+'\n'.join(literals)+';\n')
    subprocess.run(['gcc','-std=c11','-Werror=trigraphs','-fsyntax-only',str(temp/'html.c')],check=True)
    html = ''.join(ast.literal_eval(s) for s in literals)
    scripts = re.findall(r'<script>(.*?)</script>',html,re.S)
    render = re.search(r'function renderMoisture\(\).*?(?=function renderDash)',html,re.S).group()
    js = 'const assert=require("assert");\n'
    js += '\n'.join('new Function('+json.dumps(s)+');' for s in scripts)
    js += '\nlet connected=true,latest={},moistureValue={},moistureQuality={};\n'+render
    js += '\nfor(const fixture of '+json.dumps(fixtures)+'''){
 latest=fixture;renderMoisture();
 assert.equal(moistureValue.textContent,fixture.quality==='valid'?'50%':fixture.quality==='disabled'?'Off':'--%');
 if(fixture.quality!=='valid')assert(moistureQuality.textContent.length>0);
}
connected=false;latest={quality:'valid',percent:50};renderMoisture();
assert.equal(moistureValue.textContent,'--%');assert(moistureQuality.textContent.includes('offline'));
connected=true;renderMoisture();assert.equal(moistureValue.textContent,'50%');
console.log('ADC -> quality -> UI fixtures and offline/reconnect passed');
'''
    (temp/'test.js').write_text(js)
    subprocess.run(['node',str(temp/'test.js')],check=True)
