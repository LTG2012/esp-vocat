#ifndef MAGNETIC_MONITOR_H
#define MAGNETIC_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *magnetic_monitor_create_with_parent(lv_obj_t *parent);
void magnetic_monitor_show(void);
void magnetic_monitor_hide(void);
bool magnetic_monitor_is_active(void);
void magnetic_monitor_handle_sample(int16_t x, int16_t y, int16_t z,
                                    int16_t filtered_value, int16_t delta,
                                    uint8_t position);
void magnetic_monitor_record_event(uint16_t event);

#ifdef __cplusplus
}
#endif

#endif // MAGNETIC_MONITOR_H
