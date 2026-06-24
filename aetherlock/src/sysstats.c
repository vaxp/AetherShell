#include "sysstats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <limits.h>

static unsigned long long prev_cpu_total = 0;
static unsigned long long prev_cpu_idle = 0;
static char temp_path[PATH_MAX] = {0};

void sysstats_init(void) {
    // Attempt to find a valid temperature path
    // First, check thermal zones
    DIR *dir = opendir("/sys/class/thermal");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, "thermal_zone", 12) == 0) {
                char path[PATH_MAX];
                snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", ent->d_name);
                FILE *f = fopen(path, "r");
                if (f) {
                    long temp;
                    if (fscanf(f, "%ld", &temp) == 1 && temp > 0) {
                        strncpy(temp_path, path, sizeof(temp_path));
                        fclose(f);
                        closedir(dir);
                        return;
                    }
                    fclose(f);
                }
            }
        }
        closedir(dir);
    }
    
    // Fallback: check hwmon
    dir = opendir("/sys/class/hwmon");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strncmp(ent->d_name, "hwmon", 5) == 0) {
                char path[PATH_MAX];
                snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp1_input", ent->d_name);
                FILE *f = fopen(path, "r");
                if (f) {
                    long temp;
                    if (fscanf(f, "%ld", &temp) == 1 && temp > 0) {
                        strncpy(temp_path, path, sizeof(temp_path));
                        fclose(f);
                        closedir(dir);
                        return;
                    }
                    fclose(f);
                }
            }
        }
        closedir(dir);
    }
}

void sysstats_update(struct sysstats_data *data) {
    // RAM
    FILE *mf = fopen("/proc/meminfo", "r");
    if (mf) {
        char line[256];
        unsigned long long mem_total = 0;
        unsigned long long mem_avail = 0;
        while (fgets(line, sizeof(line), mf)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                sscanf(line, "MemTotal: %llu", &mem_total);
            } else if (strncmp(line, "MemAvailable:", 13) == 0) {
                sscanf(line, "MemAvailable: %llu", &mem_avail);
            }
        }
        fclose(mf);
        if (mem_total > 0 && mem_avail > 0) {
            data->ram_usage = ((double)(mem_total - mem_avail) / mem_total) * 100.0;
        } else {
            data->ram_usage = 0.0;
        }
    } else {
        data->ram_usage = 0.0;
    }

    // Disk
    struct statvfs vfs;
    if (statvfs("/", &vfs) == 0) {
        unsigned long long total = vfs.f_blocks;
        unsigned long long avail = vfs.f_bavail;
        data->disk_usage = total > 0 ? ((double)(total - avail) / total) * 100.0 : 0.0;
    } else {
        data->disk_usage = 0.0;
    }

    // CPU
    FILE *f = fopen("/proc/stat", "r");
    if (f) {
        unsigned long long user=0, nice=0, system=0, idle=0, iowait=0, irq=0, softirq=0, steal=0, guest=0, guest_nice=0;
        char cpu[16];
        if (fscanf(f, "%s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   cpu, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice) >= 5) {
            
            unsigned long long total_idle = idle + iowait;
            unsigned long long non_idle = user + nice + system + irq + softirq + steal;
            unsigned long long total = total_idle + non_idle;
            
            unsigned long long total_diff = total - prev_cpu_total;
            unsigned long long idle_diff = total_idle - prev_cpu_idle;
            
            if (total_diff > 0 && prev_cpu_total > 0) {
                data->cpu_usage = ((double)(total_diff - idle_diff) / total_diff) * 100.0;
            } else {
                data->cpu_usage = 0.0;
            }
            
            prev_cpu_total = total;
            prev_cpu_idle = total_idle;
        }
        fclose(f);
    } else {
        data->cpu_usage = 0.0;
    }

    // Temperature
    data->temperature = 0.0;
    if (temp_path[0] != '\0') {
        f = fopen(temp_path, "r");
        if (f) {
            long temp;
            if (fscanf(f, "%ld", &temp) == 1) {
                data->temperature = temp / 1000.0;
            }
            fclose(f);
        }
    }
}
