#ifndef SYSINFO_H
#define SYSINFO_H

struct sysinfo_data {
    char os_name[64];
    char de_name[64];
    char user_name[64];
    char uptime[64];
};

void fetch_sysinfo(struct sysinfo_data *info);

#endif
