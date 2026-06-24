#ifndef SYSSTATS_H
#define SYSSTATS_H

#include <stdbool.h>

struct sysstats_data {
    double cpu_usage;     // 0-100
    double ram_usage;     // 0-100
    double disk_usage;    // 0-100
    double temperature;   // Celsius
};

void sysstats_init(void);
void sysstats_update(struct sysstats_data *data);

#endif
