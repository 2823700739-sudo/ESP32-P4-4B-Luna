// Same rails and rest accounting as docs/design/preview/model.mjs.
#include "luna_ui_motion.h"
luna_pet_position_t luna_pet_motion(int64_t ms)
{
    if (ms<0) ms=0;
    static const int rails[12][2]={{90,85},{600,85},{660,85},{660,150},{660,530},{660,596},
                                 {600,596},{90,596},{8,596},{8,530},{8,150},{8,85}};
    int64_t phase=ms%16000,travel=ms/16000*13000+(phase<13000?phase:13000);
    unsigned i=(travel/6500)%12,j=(i+1)%12; float t=(travel%6500)/6500.0f;
    return (luna_pet_position_t){.x=rails[i][0]+(rails[j][0]-rails[i][0])*t,
        .y=rails[i][1]+(rails[j][1]-rails[i][1])*t,.left=rails[j][0]<rails[i][0],.resting=phase>=13000};
}
unsigned luna_ui_next_card(unsigned current,int delta)
{
    int n=(int)(current%5)+delta%5; return (unsigned)((n+5)%5);
}
