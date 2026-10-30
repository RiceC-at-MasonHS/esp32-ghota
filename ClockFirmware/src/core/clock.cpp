#include "clock.h"
#include "display.h"
#include <time.h>

void clock_tick()
{
    static int lastMinute = -1;
    static bool synced = false;

    time_t now = time(nullptr);
    if (now < 1700000000)
    {
        if (lastMinute != -2)
        {
            display_showNoTime();
            lastMinute = -2;
        }
        return;
    }

    struct tm lt;
    localtime_r(&now, &lt);
    if (!synced || lt.tm_min != lastMinute)
    {
        synced = true;
        lastMinute = lt.tm_min;
        display_showTime(lt.tm_hour, lt.tm_min);
    }
}
