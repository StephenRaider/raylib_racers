/*
 * evil: a test robot that misbehaves on purpose, to check the sandbox
 * (--sandbox) and the CPU cap (--cpu-cap). It drives gently along the
 * centreline and, depending on params:
 *
 *   write=PATH  in create(), tries to create PATH; refuses the car (NULL) if it can
 *   crash=1     dereferences NULL in drive() after 2 s
 *   hang=1      loops forever in drive() after 2 s
 *   cpu=MS      burns MS milliseconds of CPU in every drive() call
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rr/robot_api.h"

typedef struct {
    int crash, hang;
    double cpu_ms;
    float max_steer;
} Evil;

static const char* param(const char* params, const char* key) {
    const char* p = params ? strstr(params, key) : NULL;
    return p ? p + strlen(key) : NULL;
}

static void* create(const RRTrackInfo* track, const RRCarSpec* car, int car_index, const char* params,
                    RRRobotConfig* config) {
    (void)track; (void)car_index; (void)config;
    const char* path = param(params, "write=");
    if (path) {
        char file[512];
        size_t n = strcspn(path, ",");
        if (n >= sizeof file) n = sizeof file - 1;
        memcpy(file, path, n);
        file[n] = 0;
        FILE* f = fopen(file, "w");
        if (f) {
            fputs("escaped\n", f);
            fclose(f);
            return NULL;
        }
    }
    Evil* e = (Evil*)calloc(1, sizeof(Evil));
    e->crash = param(params, "crash=1") != NULL;
    e->hang = param(params, "hang=1") != NULL;
    e->cpu_ms = param(params, "cpu=") ? atof(param(params, "cpu=")) : 0;
    e->max_steer = car->max_steer;
    return e;
}

static void drive(void* self, const RRSensors* in, RRControl* out) {
    Evil* e = (Evil*)self;
    if (e->cpu_ms > 0) {
        const clock_t until = clock() + (clock_t)(e->cpu_ms * CLOCKS_PER_SEC / 1000.0);
        volatile double x = 0;
        while (clock() < until) x += 1.0;
    }
    if (in->time > 2.0 && e->crash) {
        volatile int* p = NULL;
        *p = 1;
    }
    if (in->time > 2.0 && e->hang) {
        volatile int spin = 1;
        while (spin) {}
    }
    out->steer = (-in->angle - in->track_pos * 0.3f) / e->max_steer;
    if (out->steer > 1) out->steer = 1;
    if (out->steer < -1) out->steer = -1;
    if (in->speed_x < 15.0f) out->accel = 0.5f;
}

static void destroy(void* self) { free(self); }

static const RRRobotApi api = {RR_ABI_VERSION, "evil", "Raylib Racers tests", create, drive, destroy, NULL, NULL};

RR_EXPORT const RRRobotApi* rr_robot_entry(void) { return &api; }
