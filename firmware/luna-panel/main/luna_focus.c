#include "luna_focus.h"

void luna_focus_init(luna_focus_t *focus)
{
    *focus=(luna_focus_t){.duration_seconds=1500,.remaining_us=1500000000LL};
}
void luna_focus_end(luna_focus_t *focus)
{
    focus->state=LUNA_FOCUS_IDLE;focus->notice=false;focus->deadline_us=0;
    focus->remaining_us=(int64_t)focus->duration_seconds*1000000;
}
bool luna_focus_select(luna_focus_t *focus,unsigned minutes)
{
    if((minutes<1||minutes>LUNA_FOCUS_MAX_MINUTES)||
       focus->state==LUNA_FOCUS_RUNNING||focus->state==LUNA_FOCUS_PAUSED)return false;
    focus->duration_seconds=minutes*60;luna_focus_end(focus);return true;
}
bool luna_focus_start(luna_focus_t *focus,int64_t now)
{
    if(focus->state==LUNA_FOCUS_RUNNING)return false;
    if(focus->state!=LUNA_FOCUS_PAUSED)focus->remaining_us=(int64_t)focus->duration_seconds*1000000;
    focus->deadline_us=now+focus->remaining_us;focus->state=LUNA_FOCUS_RUNNING;focus->notice=false;
    return true;
}
bool luna_focus_tick(luna_focus_t *focus,int64_t now)
{
    if(focus->state!=LUNA_FOCUS_RUNNING||now<focus->deadline_us)return false;
    focus->remaining_us=0;focus->state=LUNA_FOCUS_DONE;focus->notice=true;return true;
}
bool luna_focus_pause(luna_focus_t *focus,int64_t now)
{
    luna_focus_tick(focus,now);
    if(focus->state!=LUNA_FOCUS_RUNNING)return false;
    focus->remaining_us=focus->deadline_us-now;focus->state=LUNA_FOCUS_PAUSED;return true;
}
unsigned luna_focus_remaining(const luna_focus_t *focus,int64_t now)
{
    int64_t left=focus->state==LUNA_FOCUS_RUNNING?focus->deadline_us-now:focus->remaining_us;
    if(left<=0)return 0;
    unsigned seconds=(unsigned)((left+999999)/1000000);
    return seconds>focus->duration_seconds?focus->duration_seconds:seconds;
}
void luna_focus_ack(luna_focus_t *focus) { focus->notice=false; }
