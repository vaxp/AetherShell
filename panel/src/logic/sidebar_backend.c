#include "sidebar_backend.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

static SidebarCallbacks s_cb = {0};
static int view_year = 0, view_month = 0;
static guint s_timer_clock = 0;
static guint s_timer_weather = 0;
static SoupSession *weather_soup_session = NULL;
static char *current_weather_location = NULL;

static const char *MONTH_NAMES[] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
};
static const char *DAY_ABBR[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};

const char* sidebar_backend_get_month_name(int month) {
    if (month >= 0 && month < 12) return MONTH_NAMES[month];
    return "";
}

const char* sidebar_backend_get_day_abbr(int day) {
    if (day >= 0 && day < 7) return DAY_ABBR[day];
    return "";
}

/* ── Calendar Logic ── */
static int days_in_month(int y, int m) {
    int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 1 && ((y%4==0&&y%100!=0)||y%400==0)) return 29;
    return d[m];
}

static int first_dow(int y, int m) {
    struct tm t = {0};
    t.tm_year = y - 1900; t.tm_mon = m; t.tm_mday = 1;
    mktime(&t); return t.tm_wday;
}

static void trigger_calendar_rebuild(void) {
    time_t now = time(NULL);
    struct tm *tnow = localtime(&now);
    int today = tnow->tm_mday, tcurm = tnow->tm_mon, tcury = tnow->tm_year+1900;

    int sow   = first_dow(view_year, view_month);
    int dcur  = days_in_month(view_year, view_month);
    int dprev = days_in_month(view_month==0?view_year-1:view_year,
                              view_month==0?11:view_month-1);
                              
    if (s_cb.on_calendar_rebuild) {
        int today_mday = (view_month==tcurm && view_year==tcury) ? today : -1;
        s_cb.on_calendar_rebuild(view_year, view_month, today_mday, tcurm, tcury, dprev, dcur, sow);
    }
}

void sidebar_backend_cal_prev_month(void) {
    if (--view_month < 0) { view_month=11; view_year--; } 
    trigger_calendar_rebuild(); 
}
void sidebar_backend_cal_next_month(void) {
    if (++view_month > 11){ view_month=0;  view_year++; } 
    trigger_calendar_rebuild(); 
}
void sidebar_backend_cal_prev_year (void) { 
    view_year--; 
    trigger_calendar_rebuild(); 
}
void sidebar_backend_cal_next_year (void) { 
    view_year++; 
    trigger_calendar_rebuild(); 
}

/* ── Clock Logic ── */
static gboolean update_clock_tick(gpointer _) {
    (void)_;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[16], ds[64];
    strftime(ts, sizeof(ts), "%H:%M", t);
    strftime(ds, sizeof(ds), "%a, %d %B", t);
    
    if (s_cb.on_time_updated) {
        s_cb.on_time_updated(ts, ds);
    }
    return TRUE;
}

/* ── Weather Logic ── */
static const char* get_weather_icon(int code) {
    if (code == 0) return "☀️";
    if (code == 1 || code == 2) return "⛅";
    if (code == 3) return "☁️";
    if (code == 45 || code == 48) return "🌫️";
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "🌧️";
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "❄️";
    if (code >= 95 && code <= 99) return "⛈️";
    return "🌡️";
}

static char* get_weather_location(void) {
    char *path = g_build_filename(g_get_home_dir(), ".config", "vaxp", "aetherlock", "aetherlock.vaxp", NULL);
    GKeyFile *kf = g_key_file_new();
    char *loc = g_strdup("Baghdad");
    if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
        char *v = g_key_file_get_string(kf, "Weather", "Location", NULL);
        if (v && *v) {
            g_free(loc);
            loc = v;
        } else if (v) {
            g_free(v);
        }
    }
    g_key_file_free(kf);
    g_free(path);
    return loc;
}

static void on_forecast_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)user_data;
    SoupSession *session = SOUP_SESSION(source);
    GError *error = NULL;
    GBytes *body = soup_session_send_and_read_finish(session, res, &error);
    if (error) { g_error_free(error); return; }
    
    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, g_bytes_get_data(body, NULL), g_bytes_get_size(body), NULL)) {
        JsonNode *root_node = json_parser_get_root(parser);
        if (root_node && JSON_NODE_HOLDS_OBJECT(root_node)) {
            JsonObject *root = json_node_get_object(root_node);
            JsonObject *current = json_object_get_object_member(root, "current");
            JsonObject *daily = json_object_get_object_member(root, "daily");
            
            if (current && daily) {
                double temp = json_object_get_double_member(current, "temperature_2m");
                int code = json_object_get_int_member(current, "weather_code");
                
                char tbuf[32];
                snprintf(tbuf, sizeof(tbuf), "%.0f°", temp);
                
                if (s_cb.on_weather_current) {
                    s_cb.on_weather_current(tbuf, get_weather_icon(code), current_weather_location ? current_weather_location : "Unknown");
                }
                
                if (s_cb.on_weather_forecast_clear) {
                    s_cb.on_weather_forecast_clear();
                }
                
                JsonArray *times = json_object_get_array_member(daily, "time");
                JsonArray *codes = json_object_get_array_member(daily, "weather_code");
                JsonArray *maxs = json_object_get_array_member(daily, "temperature_2m_max");
                JsonArray *mins = json_object_get_array_member(daily, "temperature_2m_min");
                
                if (times && codes && maxs && mins && s_cb.on_weather_forecast_day) {
                    guint len = json_array_get_length(times);
                    for (guint i = 0; i < len && i < 7; i++) {
                        const char *date_str = json_array_get_string_element(times, i);
                        int ccode = json_array_get_int_element(codes, i);
                        double cmax = json_array_get_double_element(maxs, i);
                        double cmin = json_array_get_double_element(mins, i);
                        
                        int y, m, d;
                        char day_name[16] = "Day";
                        if (sscanf(date_str, "%d-%d-%d", &y, &m, &d) == 3) {
                            struct tm t = {0};
                            t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
                            mktime(&t);
                            strftime(day_name, sizeof(day_name), "%a", &t);
                        }
                        
                        char mm[32];
                        snprintf(mm, sizeof(mm), "%.0f°\n%.0f°", cmax, cmin);
                        
                        s_cb.on_weather_forecast_day(day_name, get_weather_icon(ccode), mm);
                    }
                }
            }
        }
    }
    g_object_unref(parser);
    g_bytes_unref(body);
}

static void on_geocoding_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)user_data;
    SoupSession *session = SOUP_SESSION(source);
    GError *error = NULL;
    GBytes *body = soup_session_send_and_read_finish(session, res, &error);
    if (error) { g_error_free(error); return; }

    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, g_bytes_get_data(body, NULL), g_bytes_get_size(body), NULL)) {
        JsonNode *root_node = json_parser_get_root(parser);
        if (root_node && JSON_NODE_HOLDS_OBJECT(root_node)) {
            JsonObject *root = json_node_get_object(root_node);
            JsonArray *results = json_object_get_array_member(root, "results");
            if (results && json_array_get_length(results) > 0) {
                JsonObject *first = json_array_get_object_element(results, 0);
                double lat = json_object_get_double_member(first, "latitude");
                double lon = json_object_get_double_member(first, "longitude");
                
                char *url = g_strdup_printf("https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f&current=temperature_2m,weather_code&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto", lat, lon);
                SoupMessage *msg = soup_message_new("GET", url);
                soup_session_send_and_read_async(session, msg, G_PRIORITY_DEFAULT, NULL, on_forecast_ready, NULL);
                g_object_unref(msg);
                g_free(url);
            }
        }
    }
    g_object_unref(parser);
    g_bytes_unref(body);
}

static gboolean update_weather_tick(gpointer _) {
    (void)_;
    if (!weather_soup_session) weather_soup_session = soup_session_new();
    
    char *loc = get_weather_location();
    if (current_weather_location) g_free(current_weather_location);
    current_weather_location = g_strdup(loc);
    
    char *loc_enc = g_uri_escape_string(loc, NULL, TRUE);
    char *url = g_strdup_printf("https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1", loc_enc);
    SoupMessage *msg = soup_message_new("GET", url);
    
    soup_session_send_and_read_async(weather_soup_session, msg, G_PRIORITY_DEFAULT, NULL, on_geocoding_ready, NULL);
    
    g_object_unref(msg);
    g_free(url);
    g_free(loc_enc);
    g_free(loc);
    return TRUE;
}

/* ── Init / Timers ── */
void sidebar_backend_init(SidebarCallbacks *cb) {
    if (cb) {
        s_cb = *cb;
    }
    
    time_t now0 = time(NULL); 
    struct tm *t0 = localtime(&now0);
    view_year = t0->tm_year + 1900; 
    view_month = t0->tm_mon;
}

void sidebar_backend_start_timers(void) {
    if (!s_timer_clock)   s_timer_clock   = g_timeout_add(1000, update_clock_tick, NULL);
    if (!s_timer_weather) s_timer_weather = g_timeout_add_seconds(1800, update_weather_tick, NULL);
}

void sidebar_backend_stop_timers(void) {
    /* Lightweight timers keep running. Optionally pause weather here if desired. */
}

void sidebar_backend_refresh_all(void) {
    update_clock_tick(NULL);
    update_weather_tick(NULL);
    trigger_calendar_rebuild();
}
