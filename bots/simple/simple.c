/*
 * simple: the classic SCR "simple driver", written in plain C.
 *
 * Uses only the SCR sensors: steers to line up with the track axis and stay
 * near the centre, and picks a target speed from the free distance straight
 * ahead.
 *
 * params: speed=<scale, default 1.0>
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../common/rr_params.h"
#include "../common/rr_recovery.h"
#include "rr/robot_api.h"

typedef struct {
    float speed_scale;
    float max_steer;
    float tc; /* traction-control throttle limit */
    RRRecovery recovery;
} Simple;

static void* create(const RRTrackInfo* track, const RRCarSpec* car, int car_index, const char* params,
                    RRRobotConfig* config) {
    (void)track; (void)car_index; (void)config;
    Simple* s = (Simple*)calloc(1, sizeof(Simple));
    s->speed_scale = rr_param(params, "speed", 1.0f);
    s->max_steer = car->max_steer;
    s->tc = 1.0f;
    return s;
}

static void drive(void* self, const RRSensors* in, RRControl* out) {
    Simple* s = (Simple*)self;
    if (rr_recover(&s->recovery, in, out, s->max_steer)) {
        snprintf(out->status, sizeof out->status, "recovering");
        return;
    }

    /* Steer: cancel the heading error and pull towards the centreline. */
    out->steer = rr_clamp((-in->angle - in->track_pos * 0.5f) / s->max_steer, -1.0f, 1.0f);

    /* Speed: what we could brake down from within the free distance ahead. */
    float front = in->track[9];
    float target;
    if (front < 0) {
        target = 12.0f; /* off the tarmac: crawl back */
    } else {
        float d = front - 15.0f;
        target = 14.0f + sqrtf(2.0f * 6.0f * (d > 0 ? d : 0));
    }
    target *= s->speed_scale;

    float err = target - in->speed_x;
    if (err > 0) {
        out->accel = rr_clamp(0.3f + err * 0.25f, 0.0f, 1.0f);
        /* traction control: cut fast when the rear tyres give up, restore slowly */
        if (in->wheel_spin > 0.02f) s->tc = rr_clamp(s->tc - 2.0f * in->wheel_spin, 0.1f, 1.0f);
        else s->tc = rr_clamp(s->tc + 0.05f, 0.1f, 1.0f);
        out->accel *= s->tc;
    } else {
        out->brake = rr_clamp(-err * 0.15f, 0.0f, 1.0f);
    }

    snprintf(out->status, sizeof out->status, "target %.0f km/h", target * 3.6f);
    out->debug[0] = target;
}

static void destroy(void* self) { free(self); }

static const RRRobotApi api = {RR_ABI_VERSION, "simple", "Raylib Racers examples", create, drive, destroy, NULL};

RR_EXPORT const RRRobotApi* rr_robot_entry(void) { return &api; }
