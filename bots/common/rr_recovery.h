/* Shared "get unstuck" behaviour for robots, in the spirit of the TORCS
 * example bots. Call rr_recover() at the top of drive(); while it returns 1 it
 * owns the controls.
 *
 *  - pointing the wrong way: a forward U-turn that swings away from the
 *    nearest barrier, with a short reverse if the nose hits something;
 *  - crawling at an angle (nose in a barrier): back out while steering the
 *    nose round, then hand control back. */
#ifndef RR_RECOVERY_H
#define RR_RECOVERY_H
#include <math.h>

#include "rr/robot_api.h"

enum { RR_REC_NONE = 0, RR_REC_UTURN, RR_REC_REVERSE };

typedef struct RRRecovery {
    int mode;
    int ticks;        /* drive() calls in the current mode */
    int stuck_ticks;  /* drive() calls spent looking stuck */
    float turn;       /* U-turn steering direction, +1 left / -1 right */
} RRRecovery;

static inline float rr_rec_clamp(float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }

static inline int rr_recover(RRRecovery* r, const RRSensors* in, RRControl* out, float max_steer) {
    const float ang = in->angle;
    const float aang = fabsf(ang);
    const int wrong_way = aang > 1.6f;

    if (r->mode == RR_REC_NONE) {
        int stuck = wrong_way || (aang > 0.6f && fabsf(in->speed_x) < 4.0f);
        r->stuck_ticks = stuck ? r->stuck_ticks + 1 : 0;
        if (r->stuck_ticks <= 25) { /* ~0.5 s at 50 Hz */
            out->gear = in->gear < 1 ? 1 : in->gear;
            return 0;
        }
        r->stuck_ticks = 0;
        r->ticks = 0;
        if (wrong_way) {
            /* Turn so that the circle's centre lies towards the middle of the track. */
            float c = -in->track_pos * cosf(ang);
            r->turn = c >= 0 ? 1.0f : -1.0f;
            r->mode = RR_REC_UTURN;
        } else {
            r->turn = 0;
            r->mode = RR_REC_REVERSE;
        }
    }

    r->ticks++;
    if (r->mode == RR_REC_UTURN) {
        if (aang < 0.6f || r->ticks > 600) {
            r->mode = RR_REC_NONE;
            out->gear = 1;
            return 0;
        }
        out->gear = 1;
        out->steer = r->turn;
        if (in->speed_x < -0.5f) { /* still rolling backwards: stop first */
            out->accel = 0.0f;
            out->brake = 1.0f;
            return 1;
        }
        out->accel = 0.35f;
        out->brake = 0.0f;
        if (r->ticks > 50 && fabsf(in->speed_x) < 0.5f) { /* blocked: back up a little */
            r->mode = RR_REC_REVERSE;
            r->ticks = 0;
        }
        return 1;
    }

    /* RR_REC_REVERSE */
    int limit = r->turn != 0 ? 75 : 120;
    if (r->ticks > limit || (r->turn == 0 && aang < 0.35f)) {
        r->mode = (r->turn != 0 && wrong_way) ? RR_REC_UTURN : RR_REC_NONE;
        r->ticks = 0;
        out->gear = 1;
        out->brake = 1.0f;
        return 1;
    }
    out->gear = -1;
    if (in->speed_x > 0.5f) { /* still rolling forward: stop first */
        out->brake = 1.0f;
        out->accel = 0.0f;
    } else {
        out->accel = 0.4f;
        out->brake = 0.0f;
    }
    /* Reversing with the wheels turned towards the error swings the nose back;
     * during a U-turn, reverse with opposite lock to make a three-point turn. */
    out->steer = r->turn != 0 ? -r->turn : rr_rec_clamp(ang / max_steer);
    return 1;
}

#endif
