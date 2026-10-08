// Data shared between the network task and the UI.
#pragma once
#include <stdint.h>
#include <time.h>

#define MAX_DEPARTURES  6
#define MAX_DISRUPTIONS 8

enum TrainStatus : uint8_t {
    TRAIN_ON_TIME,
    TRAIN_DELAYED,
    TRAIN_AT_STOP,
    TRAIN_CANCELLED,
};

struct Departure {
    time_t   when;          // expected departure (UTC epoch)
    int16_t  delayMin;      // expected - aimed, minutes
    TrainStatus status;
    bool     longTrain;
    char     mission[8];    // e.g. "NELY"
    char     destination[48];
};

enum Severity : uint8_t {
    SEV_INFO,       // information, works announced, ...
    SEV_DISRUPTED,  // delays, reduced service, detour
    SEV_BLOCKING,   // no service
};

struct Disruption {
    Severity severity;
    bool     active;        // false = planned later today
    char     title[112];
    char     body[440];
};

enum FetchState : uint8_t {
    FETCH_NEVER,
    FETCH_OK,
    FETCH_ERROR,
};

struct BoardData {
    Departure  departures[MAX_DEPARTURES];
    uint8_t    departureCount = 0;
    FetchState departuresState = FETCH_NEVER;
    time_t     departuresAt = 0;      // last successful update

    Disruption disruptions[MAX_DISRUPTIONS];
    uint8_t    disruptionCount = 0;
    FetchState disruptionsState = FETCH_NEVER;

    char       lastError[64] = "";
};
