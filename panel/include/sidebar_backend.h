#pragma once
#include <glib.h>

typedef struct {
    void (*on_time_updated)(const char *time_str, const char *date_str);
    
    void (*on_calendar_rebuild)(int view_year, int view_month, int today_mday, int today_mon, int today_year, int dprev, int dcur, int sow);
    
    void (*on_weather_current)(const char *temp_str, const char *icon_str, const char *city_str);
    void (*on_weather_forecast_clear)(void);
    void (*on_weather_forecast_day)(const char *day_name, const char *icon, const char *temps_str);
} SidebarCallbacks;

void sidebar_backend_init(SidebarCallbacks *cb);
void sidebar_backend_start_timers(void);
void sidebar_backend_stop_timers(void);
void sidebar_backend_refresh_all(void);

void sidebar_backend_cal_prev_month(void);
void sidebar_backend_cal_next_month(void);
void sidebar_backend_cal_prev_year(void);
void sidebar_backend_cal_next_year(void);

const char* sidebar_backend_get_month_name(int month);
const char* sidebar_backend_get_day_abbr(int day);
