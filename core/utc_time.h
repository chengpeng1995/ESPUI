#pragma once
#include <ctime>
namespace assistant {
// UTC calendar conversion shared with newlib, which does not provide timegm().
inline time_t utc_epoch(tm* calendar){
    const auto leap=[](int year){return year%4==0&&(year%100!=0||year%400==0);};
    const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int year=calendar->tm_year+1900;int64_t days=0;
    for(int y=1970;y<year;++y)days+=leap(y)?366:365;
    for(int m=0;m<calendar->tm_mon;++m)days+=lengths[m]+(m==1&&leap(year));
    days+=calendar->tm_mday-1;
    time_t epoch=days*86400+calendar->tm_hour*3600+calendar->tm_min*60+calendar->tm_sec;
    gmtime_r(&epoch,calendar);return epoch;
}
}
