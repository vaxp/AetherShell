#include "sysinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <pwd.h>

void fetch_sysinfo(struct sysinfo_data *info) {
    // Default values
    strcpy(info->os_name, "Unknown OS");
    strcpy(info->de_name, "Unknown DE");
    strcpy(info->user_name, "user");
    strcpy(info->uptime, "0 minutes");

    // Get OS Name from /etc/os-release
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                char *start = line + 12;
                if (start[0] == '"') start++;
                char *end = strchr(start, '\n');
                if (end) *end = '\0';
                if (start[strlen(start) - 1] == '"') start[strlen(start) - 1] = '\0';
                snprintf(info->os_name, sizeof(info->os_name), "%s", start);
                break;
            }
        }
        fclose(fp);
    }

    // Get DE Name (Session Name)
    const char *desktop_session = getenv("DESKTOP_SESSION");
    const char *xdg_session = getenv("XDG_SESSION_DESKTOP");
    const char *xdg_current = getenv("XDG_CURRENT_DESKTOP");
    
    if (desktop_session && strlen(desktop_session) > 0) {
        snprintf(info->de_name, sizeof(info->de_name), "%s", desktop_session);
    } else if (xdg_session && strlen(xdg_session) > 0) {
        snprintf(info->de_name, sizeof(info->de_name), "%s", xdg_session);
    } else if (xdg_current && strlen(xdg_current) > 0) {
        snprintf(info->de_name, sizeof(info->de_name), "%s", xdg_current);
    } else {
        const char *wayland_display = getenv("WAYLAND_DISPLAY");
        if (wayland_display) {
            snprintf(info->de_name, sizeof(info->de_name), "Wayland (%s)", wayland_display);
        }
    }

    // Get Username
    const char *user = getenv("USER");
    if (user) {
        snprintf(info->user_name, sizeof(info->user_name), "%s", user);
    } else {
        struct passwd *pw = getpwuid(getuid());
        if (pw) {
            snprintf(info->user_name, sizeof(info->user_name), "%s", pw->pw_name);
        }
    }

    // Get Uptime
    struct sysinfo s_info;
    if (sysinfo(&s_info) == 0) {
        long uptime_secs = s_info.uptime;
        long days = uptime_secs / 86400;
        long hours = (uptime_secs % 86400) / 3600;
        long mins = (uptime_secs % 3600) / 60;

        if (days > 0) {
            snprintf(info->uptime, sizeof(info->uptime), "%ld days, %ld hours", days, hours);
        } else if (hours > 0) {
            snprintf(info->uptime, sizeof(info->uptime), "%ld hours, %ld mins", hours, mins);
        } else {
            snprintf(info->uptime, sizeof(info->uptime), "%ld mins", mins);
        }
    }
}
