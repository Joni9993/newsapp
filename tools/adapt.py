from pathlib import Path
p=Path('main/cards.c')
s=p.read_text(encoding='utf-8')
s=s.replace('#include "text.h"','#include "text.h"\n#include "config.h"')
start=s.index('static const uint8_t CAP')
end=s.index('void cards_init')
s=s[:start]+'''static const uint8_t CAP[SRC_COUNT] = {1,16,1,8,4,4,1,1,1,1,1,1,1,1,1,1,20,1};
#define TOTAL 65
static card_t pool[TOTAL];
static card_t *slot[SRC_COUNT];
static uint8_t count[SRC_COUNT],cursor[SRC_COUNT];
static SemaphoreHandle_t mtx;
static int plan_pos;
static card_t history[8];
static int history_count,history_pos=-1;
static bool replay;

'''+s[end:]
start=s.index('static uint32_t last_seen')
end=s.index('void card_begin')
s=s[:start]+'''void cards_previous(void) { if(history_pos>0) history_pos--; replay=true; }
void cards_replay(void) { replay=true; }
void cards_filter(void) {
 board_config_t cfg;config_get(&cfg);xSemaphoreTake(mtx,portMAX_DELAY);
 for(int i=1;i<SRC_COUNT;i++)if(!cfg.enabled[i-1])count[i]=0;
 history_count=0;history_pos=-1;replay=false;
 xSemaphoreGive(mtx);
}
bool cards_next(card_t *out) {
 if(replay && history_pos>=0) { *out=history[history_pos]; replay=false; return true; }
 replay=false;
 if(history_pos+1<history_count){*out=history[++history_pos];return true;}
 board_config_t cfg;config_get(&cfg);bool ok=false;
 xSemaphoreTake(mtx,portMAX_DELAY);
 for(int i=0;i<MODULE_COUNT;i++) {
 int m=cfg.order[plan_pos];plan_pos=(plan_pos+1)%MODULE_COUNT;int src=m+1;
 if(!cfg.enabled[m]||!count[src])continue;
 *out=slot[src][cursor[src]%count[src]];cursor[src]=(cursor[src]+1)%count[src];ok=true;break;
 }
 xSemaphoreGive(mtx);
 if(ok){if(history_count==8){memmove(history,history+1,7*sizeof(card_t));history_count=7;}history[history_count++]=*out;history_pos=history_count-1;}
 return ok;
}

'''+s[end:]
p.write_text(s,encoding='utf-8')
p=Path('main/courier.c');s=p.read_text(encoding='utf-8')
s=s.replace('#include "net.h"','#include "net.h"\n#include "config.h"')
s=s.replace('static int read_time(const block_t *b) { return READ_BASE_T + (int)(strlen(b->l.txt) * READ_CHAR_T); }','static int read_time(const block_t *b) { board_config_t c; config_get(&c); return (READ_BASE_T + (int)(strlen(b->l.txt) * READ_CHAR_T)) * c.duration / 20; }')
s += '''
void courier_reset(void) {
 memset(R,0,sizeof R);memset(GONE,0,sizeof GONE);memset(&T,0,sizeof T);memset(&carried,0,sizeof carried);
 nrows=qn=qhead=0;panel_waiting=false;phase=F_FILL;t=0;courier_init();
}
'''
p.write_text(s,encoding='utf-8')
p=Path('main/courier.h');s=p.read_text(encoding='utf-8')+'\nvoid courier_reset(void);\n';p.write_text(s,encoding='utf-8')
