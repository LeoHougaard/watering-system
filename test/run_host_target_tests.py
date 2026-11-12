"""Run the real scheduler against simulated time, moisture, pump, and NVS state."""
import os,re,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
idf=Path(os.environ.get('IDF_PATH',str(Path.home()/'esp/v5.5.1/esp-idf')))
source=(root/'components/scheduler/scheduler.c').read_text()
source=re.sub(r'^#include .*\n','',source,flags=re.M)
stubs=r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "irrigation_model.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define xTaskCreate(...) pdPASS
#include "moisture_manager.h"
static system_settings_t config;
static runtime_state_t runtime;
static moisture_reading_t reading;
static int target=60,calls,skips,claim_error;
static int daily_minute;
static stop_reason_t skip_reason;
static unsigned commanded[16];
static time_t now=1788703200,claimed;
static jmp_buf loop;
static time_t fake_time(time_t *out){if(out)*out=now;return now;}
static struct tm *fake_localtime_r(const time_t *value, struct tm *out){*out=*localtime(value);return out;}
#define localtime_r fake_localtime_r
#define time fake_time
static void vTaskDelay(unsigned ticks){longjmp(loop,1);}
static void config_store_get_settings(system_settings_t *s){*s=config;}
static int config_store_moisture_target(void){return target;}
static int config_store_target_time(void){return daily_minute;}
static int config_store_claim_target_cycle(time_t minute){if(claim_error||minute<=claimed)return 1;claimed=minute;return 0;}
static runtime_state_t pump_controller_get_runtime(void){return runtime;}
moisture_reading_t moisture_manager_get_latest(void){return reading;}
static int pump_controller_scheduled_run(unsigned seconds){assert(calls<16);commanded[calls++]=seconds;return 0;}
static void history_store_log_diagnostic(const char *msg){}
static void history_store_log_watering(const watering_event_t *e){skips++;skip_reason=e->stopped_reason;}
'''
tests=r'''
static void reset(void){
 model_defaults_settings(&config);config.moisture_sensor_enabled=true;config.scheduled_max_run_sec=600;
 memset(config.schedule_slots,0,sizeof(config.schedule_slots));
 struct tm *tm=localtime(&now);strftime(config.schedule_slots[0].start,6,"%H:%M",tm);
 daily_minute=tm->tm_hour*60+tm->tm_min;
 config.schedule_slots[0].enabled=true;config.schedule_slots[0].days[tm->tm_wday]=true;config.schedule_slots[0].duration_sec=600;
 runtime=(runtime_state_t){.pump_state=PUMP_STATE_IDLE,.reservoir_ok=true};
 reading=(moisture_reading_t){.quality=MOISTURE_VALID,.enabled=true,.percent=35,.samples_collected=5,.updated_at=now};
 memset(&g_target_cycle,0,sizeof(g_target_cycle));g_cancel_target=false;target=60;calls=skips=claim_error=0;claimed=0;
 for(int i=0;i<MAX_SCHEDULE_SLOTS;i++){g_last_run_yday[i]=-1;g_last_run_minute[i]=-1;}
}
static void tick(void){if(!setjmp(loop))scheduler_task(NULL);}
static void finish_pulse(int percent){
 now=g_target_cycle.check_at;reading.percent=percent;reading.updated_at=now;
 runtime.pump_state=PUMP_STATE_IDLE;runtime.last_run_at=now-1800;
 strcpy(runtime.last_run_reason,"timer_complete");advance_target_cycle(&config,now);
}
int main(void){
 reset();tick();assert(calls==1&&commanded[0]==120&&g_target_cycle.remaining==480);
 now+=30;runtime.pump_state=PUMP_STATE_SCHEDULED_RUNNING;runtime.last_run_at=g_target_cycle.started_at-100;
 strcpy(runtime.last_run_reason,"scheduled");advance_target_cycle(&config,now);
 assert(g_target_cycle.active&&calls==1);
 finish_pulse(40);assert(calls==2&&commanded[1]==120);
 now+=30;runtime.pump_state=PUMP_STATE_SCHEDULED_RUNNING;
 strcpy(runtime.last_run_reason,"scheduled");advance_target_cycle(&config,now);
 assert(g_target_cycle.active&&calls==2);
 finish_pulse(60);assert(!g_target_cycle.active&&calls==2);
 puts("Pulse, soak and target stop passed");

 reset();reading.percent=56;tick();assert(calls==0&&skips==1&&skip_reason==STOP_MOISTURE_TARGET);
 reset();reading.percent=54;tick();assert(calls==1&&commanded[0]==120);
 puts("Five-point target band passed");

 reset();reading.updated_at=now-301;tick();assert(calls==0&&!g_target_cycle.active);
 reset();reading.samples_collected=1;tick();assert(calls==0);
 reset();target=0;tick();assert(calls==1&&commanded[0]==600);
 puts("Unavailable and warming blocked; timed mode unchanged passed");

 reset();tick();finish_pulse(36);assert(calls==2&&commanded[1]==480&&!g_target_cycle.active);
 reset();tick();for(int i=0;i<5;i++)finish_pulse(39+i*4);
 assert(calls==5&&!g_target_cycle.active);unsigned total=0;for(int i=0;i<calls;i++)total+=commanded[i];assert(total==600);
 puts("No-response fallback and total watering allowance passed");

 reset();tick();scheduler_cancel_target_cycle();now+=180;tick();assert(!g_target_cycle.active&&calls==1);
 reset();tick();target=50;advance_target_cycle(&config,now);assert(!g_target_cycle.active&&calls==1);
 reset();tick();config.auto_mode_enabled=false;advance_target_cycle(&config,now);assert(!g_target_cycle.active);
 reset();tick();runtime.pump_state=PUMP_STATE_MANUAL_RUNNING;advance_target_cycle(&config,now);assert(!g_target_cycle.active);
 puts("Stop, target change, schedule pause and manual override passed");

 reset();tick();now+=4*3600;advance_target_cycle(&config,now);assert(!g_target_cycle.active&&calls==1);
 puts("Expired session cancellation passed");

 reset();tick();g_target_cycle.active=false;
 for(int i=0;i<MAX_SCHEDULE_SLOTS;i++){g_last_run_yday[i]=-1;g_last_run_minute[i]=-1;}
 tick();assert(calls==1);
 reset();claim_error=1;tick();assert(calls==0);
 puts("Persistent restart duplicate guard and storage-failure lockout passed");

 for(int day=0;day<7;day++){
  now+=86400;now-=now%60;reset();
  memset(config.schedule_slots[0].days,0,sizeof(config.schedule_slots[0].days));
  now-=60;reading.updated_at=now;tick();assert(calls==0);
  now+=60;reading.updated_at=now;tick();assert(calls==1&&commanded[0]==120);
  g_target_cycle.active=false;now+=30;tick();assert(calls==1);
  now+=60;tick();assert(calls==1);
 }
 puts("Daily time across all seven weekdays, no early or duplicate run passed");

 reset();target=0;memset(config.schedule_slots[0].days,0,sizeof(config.schedule_slots[0].days));
 tick();assert(calls==0);
 reset();daily_minute=(daily_minute+60)%1440;tick();assert(calls==0);
 scheduler_refresh_next_run();char expected[6];snprintf(expected,6,"%02d:%02d",daily_minute/60,daily_minute%60);
 assert(strncmp(g_next_run,expected,5)==0);
 reset();tick();daily_minute=(daily_minute+60)%1440;advance_target_cycle(&config,now);
 assert(!g_target_cycle.active);
 puts("Schedule-only weekdays, changed daily time and pending cycle cancellation passed");

 reset();reading.samples_collected=0;tick();assert(calls==0);
 for(int i=0;i<MAX_SCHEDULE_SLOTS;i++){g_last_run_yday[i]=-1;g_last_run_minute[i]=-1;}
 tick();assert(calls==0);
 puts("Probe skip also protected against same-minute reboot passed");

 reset();daily_minute=360;
 config.legacy_weather_adjust_enabled=true;config.legacy_weather_last_multiplier=0;
 config.legacy_rain_delay_enabled=true;config.legacy_rain_delay_until=now+30*86400;
 config.legacy_seasonal_multiplier=0;
 struct tm calendar=*localtime(&now);calendar.tm_hour=0;calendar.tm_min=0;calendar.tm_sec=0;
 time_t first_day=mktime(&calendar);
 for(int day=0;day<14;day++){
  calls=skips=0;
  for(int minute=0;minute<1440;minute++){
   now=first_day+day*86400+minute*60;
   reading.updated_at=now;reading.percent=day%3==0?65:35;
   reading.samples_collected=day%3==2?0:5;
   int previous_calls=calls;tick();
   if(calls>previous_calls&&previous_calls==0)assert(minute==360);
  }
  unsigned total=0;for(int i=0;i<calls;i++)total+=commanded[i];
  if(day%3==0||day%3==2){assert(calls==0&&skips==1);}else{assert(total==600);}
  assert(!g_target_cycle.active);
 }
 puts("Fourteen continuous days: correct daily time, wet-soil skips, missing probe blocked, bounded valid-response fallback, no weather dependency passed");
 for(int q=MOISTURE_DISABLED;q<=MOISTURE_STALE;q++){
  if(q==MOISTURE_VALID)continue;
  reset();reading.quality=q;tick();assert(calls==0);
  reset();tick();reading.quality=q;finish_pulse(35);assert(calls==1&&!g_target_cycle.active);
 }
 puts("All invalid qualities block initial and soak watering passed");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='watering-target-') as directory:
 temp=Path(directory);(temp/'esp_err.h').write_text('typedef int esp_err_t;\n');(temp/'test.c').write_text(stubs+source+tests)
 cjson=idf/'components/json/cJSON'
 subprocess.run(['gcc','-std=c11','-I'+str(temp),'-I'+str(root/'components/moisture_manager/include'),'-I'+str(root/'components/irrigation_model/include'),'-I'+str(cjson),str(temp/'test.c'),str(root/'components/irrigation_model/irrigation_model.c'),str(cjson/'cJSON.c'),'-o',str(temp/'test.exe')],check=True)
 subprocess.run([str(temp/'test.exe')],check=True)
