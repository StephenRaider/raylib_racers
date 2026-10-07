/*
 * memcheck: a test robot for the weekend API (ABI 8). It drives gently along
 * the centreline and checks, through the weekend memory, that the host runs
 * its sessions in order and keeps the memory between them:
 *
 *   - every session_end() appends the session to a log in the memory;
 *   - create() refuses the car (returns NULL) when a session saw no turn ids,
 *     sessions came out of order (practice before qualifying), or the race
 *     does not find exactly `expect` sessions before it.
 *
 * params: expect=<sessions before the race>
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rr/robot_api.h"

typedef struct {
    unsigned magic;
    int sessions;
    int kinds[8];
    int saw_turns[8];
} Log;

typedef struct {
    Log* log;
    int session;
    int saw_turn;
    float max_steer;
} Bot;

#define MAGIC 0x52524d43u

static void* create(const RRTrackInfo* track, const RRCarSpec* car, int car_index, const char* params,
                    RRRobotConfig* config) {
    (void)car_index;
    const char* e = params ? strstr(params, "expect=") : NULL;
    const int expect = e ? atoi(e + 7) : 0;
    if (!config->memory || config->memory_size < (int)sizeof(Log) || track->num_turns <= 0) return NULL;
    Log* log = (Log*)config->memory;
    if (log->magic != MAGIC) {
        if (log->sessions != 0) return NULL;  /* the memory must start zeroed */
        log->magic = MAGIC;
    }
    if (config->session == RR_SESSION_RACE && log->sessions != expect) return NULL;
    for (int i = 0; i < log->sessions && i < 8; ++i) {
        if (!log->saw_turns[i]) return NULL;
        if (config->session != RR_SESSION_RACE && log->kinds[i] >= config->session) return NULL;
    }
    Bot* b = (Bot*)calloc(1, sizeof(Bot));
    b->log = log;
    b->session = config->session;
    b->max_steer = car->max_steer;
    config->tire_compound = RR_TIRE_HARD;
    return b;
}

static void drive(void* self, const RRSensors* in, RRControl* out) {
    Bot* b = (Bot*)self;
    if (in->next_turn > 0 && in->session == b->session) b->saw_turn = 1;
    out->steer = (-in->angle - in->track_pos * 0.3f) / b->max_steer;
    if (out->steer > 1) out->steer = 1;
    if (out->steer < -1) out->steer = -1;
    const float target = 18.0f;
    if (in->speed_x < target) out->accel = 0.5f;
    else out->brake = 0.2f;
}

static void session_end(void* self, const RRSessionSummary* s) {
    Bot* b = (Bot*)self;
    Log* log = b->log;
    if (log->sessions < 8) {
        log->kinds[log->sessions] = s->session;
        log->saw_turns[log->sessions] = b->saw_turn;
    }
    log->sessions++;
}

static void destroy(void* self) { free(self); }

static const RRRobotApi api = {RR_ABI_VERSION, "memcheck", "Raylib Racers tests", create, drive, destroy, NULL, session_end};

RR_EXPORT const RRRobotApi* rr_robot_entry(void) { return &api; }
