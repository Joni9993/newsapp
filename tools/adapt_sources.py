from pathlib import Path
p=Path('main/sources.c');s=p.read_text(encoding='utf-8')
s=s.replace('#include "gfx.h"','#include "gfx.h"\n#include "config.h"\n#include "modules.h"\n#include "calendar.h"')
a=s.index('#if __has_include');b=s.index('static const char *TAG',a);s=s[:a]+s[b:]
s=s.replace('net_get(', 'source_get(')
a=s.index('static bool feed_json')
s=s[:a]+'''static bool fetch_failed;
static int source_get(const char *url,net_data_cb cb,void *ctx) { int code=net_get(url,cb,ctx); if(code!=200)fetch_failed=true; return code; }
'''+s[a:]
a=s.index('static bool fetch_location');b=s.index('/* ---------------------------------------------------------------- Wetter */',a)
s=s[:a]+'''static bool fetch_location(void)
{
 board_config_t cfg;config_get(&cfg);region_id=cfg.region;snprintf(state_code,sizeof state_code,"%s",STATES[region_id-1].code);
 city[0]=0;if(!cfg.city[0])return false;
 char url[360],q[192];size_t o=0;
 for(const unsigned char *p=(const unsigned char*)cfg.city;*p && o+4<sizeof q;p++) { if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9'))q[o++]=*p;else{o+=snprintf(q+o,sizeof q-o,"%%%02X",*p);} }q[o]=0;
 snprintf(url,sizeof url,"https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=de&countryCode=DE",q);
 int depth=3;json_t j;json_init(&j,geo_cb,&depth);int code=source_get(url,feed_json,&j);
 region_id=cfg.region;for(size_t i=0;i<sizeof STATES/sizeof *STATES;i++)if(STATES[i].region==region_id)snprintf(state_code,sizeof state_code,"%s",STATES[i].code);
 return code==200&&city[0];
}

'''+s[b:]
# Reuse proven feed parsers with lower bounded buffers.
s=s.replace('card_t list[26]','card_t list[16]').replace('n < 26','n < 16').replace('26 - n','16 - n').replace('card_t list[20]','card_t list[8]')
s=s.replace('static card_t facts[10];','static card_t facts[1];\nstatic int fact_module;').replace('if (nfacts >= 10)','if (nfacts >= 1)').replace('CARD_FACT, SRC_FAKT, head','CARD_FACT, fact_module+1, head')
a=s.index('static void fetch_facts');b=s.index('/* ---------------------------------------------------------------- Ablauf */',a)
old=s[a:b]
sections=[]
starts=[old.index('    memset(&g, 0, sizeof g);',old.index('fact_calendar();'))]
for _ in range(4):starts.append(old.index('    memset(&g, 0, sizeof g);',starts[-1]+1))
starts.append(old.index('    json_init(&j, num_cb, &g); net_get') if '    json_init(&j, num_cb, &g); net_get' in old else old.index('    json_init(&j, num_cb, &g); source_get("https://api.coingecko'))
# energy, air, quakes, holidays, space blocks.
for i,m in enumerate(['MOD_ENERGY','MOD_AIR','MOD_QUAKE','MOD_HOLIDAY','MOD_SPACE']):
 sections.append('    if(m=='+m+') {\n'+old[starts[i]:starts[i+1]]+'    }\n')
s=s[:a]+'''static void fetch_fact(int m) {
 fact_module=m;nfacts=0;gen_t g={0};json_t j;char big[12],l1[80],l2[96];
 if(m==MOD_SUN)fact_sun();if(m==MOD_MOON)fact_moon();if(m==MOD_DATE)fact_calendar();
'''+''.join(sections)+'''
 if(m==MOD_BITCOIN) {json_init(&j,num_cb,&g);source_get("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=eur",feed_json,&j);if(g.v[1]>0){snprintf(big,sizeof big,"%.0fK",g.v[1]/1000);snprintf(l1,sizeof l1,"1 Bitcoin = %.0f Euro",g.v[1]);fact(ICON_COIN,"BITCOIN",big,l1,NULL,"KLING!");}}
 if(m==MOD_EXCHANGE) {json_init(&j,num_cb,&g);source_get("https://api.frankfurter.app/latest?from=EUR&to=USD",feed_json,&j);if(g.v[2]>0){snprintf(big,sizeof big,"%.3f",g.v[2]);fact(ICON_COIN,"EURO / DOLLAR",big,"US-Dollar fuer einen Euro",NULL,"OK!");}}
 if(nfacts)cards_set(m+1,facts,nfacts);else fetch_failed=true;
}

'''+s[b:]
a=s.index('static void task(void *arg)');s=s[:a]+'''static void clock_card(void){
 struct tm tm=today();char t[20],d[32];strftime(t,sizeof t,"%H:%M",&tm);strftime(d,sizeof d,"%d.%m.%Y",&tm);
 card_t c;card_begin(&c,CARD_FACT,SRC_CLOCK,"UHRZEIT");snprintf(c.big,sizeof c.big,"%s",t);card_part(&c,d,T_TITLE);card_finish(&c);cards_set(SRC_CLOCK,&c,1);
}
static void task(void *arg) {
 uint32_t rev=0;int64_t due[MODULE_COUNT]={0};bool location=false;char last_city[64]="";
 for(;;){
 calendar_process_check();board_config_t cfg;config_get(&cfg);uint32_t nowrev=config_revision();
 if(rev!=nowrev){memset(due,0,sizeof due);rev=nowrev;if(strcmp(last_city,cfg.city)){location=false;weather.valid=false;snprintf(last_city,sizeof last_city,"%s",cfg.city);}}
 for(int order=0;order<MODULE_COUNT;order++){
 int m=cfg.order[order];int64_t now=esp_timer_get_time()/1000000;
 if(!cfg.enabled[m]||now<due[m])continue;
 if(!net_time_valid()){module_end(m,"Warte auf WLAN und Uhrzeit");continue;}
 bool local=m==MOD_MOON||m==MOD_DATE||m==MOD_CLOCK;
 if(!local&&!net_connected()){module_end(m,"WLAN nicht verbunden; letzte Daten bleiben sichtbar");continue;}
 module_begin(m);status=modules[m].id;fetch_failed=false;
 bool needs_location=m==MOD_WEATHER||m==MOD_AIR||m==MOD_SUN;
 if(needs_location&&!location)location=fetch_location();
 if(needs_location&&!location){module_end(m,"Ort fehlt oder wurde nicht gefunden");due[m]=now+120;continue;}
 region_id=cfg.region;for(size_t k=0;k<sizeof STATES/sizeof *STATES;k++)if(STATES[k].region==region_id)snprintf(state_code,sizeof state_code,"%s",STATES[k].code);
 switch(m){
 case MOD_NEWS:fetch_news();break;case MOD_WEATHER:fetch_weather();break;
 case MOD_HISTORY:fetch_onthisday("events",SRC_WIKI_HEUTE);break;case MOD_BIRTHDAYS:fetch_onthisday("births",SRC_WIKI_GEBURT);break;
 case MOD_RANDOM:fetch_random_article();break;case MOD_CALENDAR:calendar_fetch();break;case MOD_CLOCK:clock_card();break;
 default:if(m==MOD_SUN&&!weather.valid)fetch_weather();fetch_fact(m);break;
 }
 module_end(m,fetch_failed?"Quelle derzeit nicht erreichbar oder keine gültigen Daten":NULL);
 due[m]=now+(fetch_failed?120:modules[m].interval);if(config_revision()!=rev)break;
 }
 status="wartet";vTaskDelay(pdMS_TO_TICKS(1000));
 }
}
void sources_start(void){cards_init();xTaskCreate(task,"quellen",24576,NULL,1,NULL);}
'''
s=s.replace('#include "esp_random.h"','#include "esp_random.h"\n#include "esp_timer.h"')
p.write_text(s,encoding='utf-8')
