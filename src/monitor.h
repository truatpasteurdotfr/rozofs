#ifndef MONITOR_H_
#define MONITOR_H_

#include "volume.h"
#include "export.h"

int monitor_initialize();

void monitor_release();

int monitor_volume(volume_t *volume);

int monitor_export(export_t *export);

#endif /* MONITOR_H_ */
