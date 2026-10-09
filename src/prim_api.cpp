#include "prim_api.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <memory>
#include <string.h>

#include "certs.h"
#include "config.h"
#include "text_utils.h"

static const char *kDeparturesUrl =
    "https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring"
    "?MonitoringRef=STIF:StopArea:SP:" STOP_AREA_ID ":"
    "&LineRef=STIF:Line::" LINE_ID ":";

static const char *kDisruptionsUrlBase =
    "https://prim.iledefrance-mobilites.fr/marketplace/v2/navitia/line_reports/lines/"
    "line%3AIDFM%3A" LINE_ID "/line_reports?disable_geojson=true&count=20";

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

namespace {

struct Request {
    WiFiClientSecure tls;
    HTTPClient http;

    // Opens the request. Returns HTTP status (<0 = transport error).
    int get(const char *url) {
        tls.setCACert(PRIM_ROOT_CA);
        tls.setHandshakeTimeout(15);
        http.useHTTP10(true);  // no chunked encoding -> we can stream-parse the body
        http.setConnectTimeout(10000);
        http.setTimeout(HTTP_TIMEOUT_MS);
        http.setReuse(false);
        if (!http.begin(tls, url)) return HTTPC_ERROR_CONNECTION_REFUSED;
        http.addHeader("apikey", PRIM_API_KEY);
        http.addHeader("Accept", "application/json");
        return http.GET();
    }

    ~Request() { http.end(); }
};

void describe_http_error(int code, char *err, size_t n) {
    if (code == 401 || code == 403) snprintf(err, n, "Cl\xC3\xA9 API refus\xC3\xA9" "e (%d)", code);
    else if (code == 429) snprintf(err, n, "Quota API d\xC3\xA9pass\xC3\xA9");
    else if (code > 0) snprintf(err, n, "Erreur serveur HTTP %d", code);
    else snprintf(err, n, "R\xC3\xA9seau : %s", HTTPClient::errorToString(code).c_str());
}

bool contains_eastbound(const char *destination) {
    static const char *kEast[] = EASTBOUND_DESTINATIONS;
    char folded[64];
    fold_ascii_lower(folded, destination, sizeof(folded));
    for (const char *e : kEast)
        if (strstr(folded, e)) return true;
    return false;
}

const char *first_value(JsonVariantConst arr) {
    // SIRI-lite wraps most strings as [{"value": "..."}] or {"value": "..."}
    if (arr.is<JsonArrayConst>()) return arr[0]["value"] | (const char *)nullptr;
    return arr["value"] | (const char *)nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// Departures (SIRI Lite stop-monitoring)
// ---------------------------------------------------------------------------

bool prim_fetch_departures(Departure *out, uint8_t *count, char *err, size_t errLen) {
    *count = 0;
    Request req;
    int code = req.get(kDeparturesUrl);
    if (code != 200) {
        describe_http_error(code, err, errLen);
        return false;
    }

    JsonDocument filter;
    JsonObject visit =
        filter["Siri"]["ServiceDelivery"]["StopMonitoringDelivery"][0]["MonitoredStopVisit"][0]
            .to<JsonObject>();
    JsonObject journey = visit["MonitoredVehicleJourney"].to<JsonObject>();
    journey["DestinationName"][0]["value"] = true;
    journey["DirectionName"][0]["value"] = true;
    journey["JourneyNote"][0]["value"] = true;
    journey["VehicleFeatureRef"] = true;
    JsonObject call = journey["MonitoredCall"].to<JsonObject>();
    call["ExpectedDepartureTime"] = true;
    call["AimedDepartureTime"] = true;
    call["ExpectedArrivalTime"] = true;
    call["AimedArrivalTime"] = true;
    call["DepartureStatus"] = true;
    call["VehicleAtStop"] = true;
    call["DestinationDisplay"][0]["value"] = true;

    JsonDocument doc;
    DeserializationError jerr = deserializeJson(doc, req.http.getStream(),
                                                DeserializationOption::Filter(filter),
                                                DeserializationOption::NestingLimit(32));
    if (jerr) {
        snprintf(err, errLen, "R\xC3\xA9ponse illisible (%s)", jerr.c_str());
        return false;
    }

    const time_t now = time(nullptr);
    Departure tmp[20];
    uint8_t n = 0;

    JsonArrayConst visits =
        doc["Siri"]["ServiceDelivery"]["StopMonitoringDelivery"][0]["MonitoredStopVisit"];
    for (JsonObjectConst v : visits) {
        if (n >= 20) break;
        JsonObjectConst j = v["MonitoredVehicleJourney"];
        JsonObjectConst c = j["MonitoredCall"];

        const char *dest = first_value(j["DestinationName"]);
        if (!dest) dest = first_value(c["DestinationDisplay"]);
        if (!dest) dest = first_value(j["DirectionName"]);
        if (!dest || contains_eastbound(dest)) continue;

        time_t when = parse_iso8601(c["ExpectedDepartureTime"] | (const char *)nullptr);
        time_t aimed = parse_iso8601(c["AimedDepartureTime"] | (const char *)nullptr);
        if (!when) when = aimed;
        if (!when) when = parse_iso8601(c["ExpectedArrivalTime"] | (const char *)nullptr);
        if (!when) when = parse_iso8601(c["AimedArrivalTime"] | (const char *)nullptr);
        if (!when || when < now - 30) continue;

        Departure &d = tmp[n++];
        memset(&d, 0, sizeof(d));
        d.when = when;
        d.delayMin = aimed ? (int16_t)((when - aimed + 30) / 60) : 0;

        const char *status = c["DepartureStatus"] | "";
        if (strcasecmp(status, "cancelled") == 0) d.status = TRAIN_CANCELLED;
        else if (c["VehicleAtStop"] | false) d.status = TRAIN_AT_STOP;
        else if (d.delayMin >= 1 || strcasecmp(status, "delayed") == 0) d.status = TRAIN_DELAYED;
        else d.status = TRAIN_ON_TIME;

        for (JsonVariantConst f : j["VehicleFeatureRef"].as<JsonArrayConst>())
            if (strcmp(f | "", "longTrain") == 0) d.longTrain = true;

        char buf[64];
        utf8_copy_trunc(buf, dest, sizeof(buf));
        tidy_text(buf);
        // "Saint-Germain-en-Laye (Yvelines)" -> drop the parenthesised part
        if (char *paren = strstr(buf, " (")) *paren = '\0';
        utf8_copy_trunc(d.destination, buf, sizeof(d.destination));

        const char *mission = first_value(j["JourneyNote"]);
        if (mission) utf8_copy_trunc(d.mission, mission, sizeof(d.mission));
    }

    std::sort(tmp, tmp + n, [](const Departure &a, const Departure &b) { return a.when < b.when; });
    *count = std::min<uint8_t>(n, MAX_DEPARTURES);
    memcpy(out, tmp, *count * sizeof(Departure));
    return true;
}

// ---------------------------------------------------------------------------
// Disruptions (Navitia line_reports)
// ---------------------------------------------------------------------------

namespace {

Severity severity_from_effect(const char *effect) {
    if (!effect) return SEV_INFO;
    if (!strcmp(effect, "NO_SERVICE")) return SEV_BLOCKING;
    if (!strcmp(effect, "SIGNIFICANT_DELAYS") || !strcmp(effect, "REDUCED_SERVICE") ||
        !strcmp(effect, "DETOUR") || !strcmp(effect, "MODIFIED_SERVICE") ||
        !strcmp(effect, "STOP_MOVED"))
        return SEV_DISRUPTED;
    return SEV_INFO;
}

bool has_type(JsonVariantConst types, const char *t) {
    for (JsonVariantConst v : types.as<JsonArrayConst>())
        if (strcmp(v | "", t) == 0) return true;
    return false;
}

// "RER A : Trafic perturbé" -> "Trafic perturbé"
void strip_line_prefix(char *s) {
    char folded[12];
    fold_ascii_lower(folded, s, sizeof(folded));
    if (strncmp(folded, "rer a", 5) != 0 && strncmp(folded, "ligne a", 7) != 0) return;
    char *p = s + (folded[0] == 'r' ? 5 : 7);
    while (*p == ' ') p++;
    if (*p != ':' && *p != '-') return;
    p++;
    while (*p == ' ') p++;
    if (*p) memmove(s, p, strlen(p) + 1);
}

bool skip_ws_peek(Stream &s, char expected) {
    uint32_t start = millis();
    while (millis() - start < HTTP_TIMEOUT_MS) {
        int c = s.peek();
        if (c < 0) { delay(5); continue; }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { s.read(); continue; }
        return c == expected;
    }
    return false;
}

// Moves the stream just past the '[' of the top-level "disruptions" array.
bool seek_disruptions_array(Stream &s) {
    while (s.find("\"disruptions\"")) {
        if (!skip_ws_peek(s, ':')) continue;
        s.read();
        if (!skip_ws_peek(s, '[')) continue;
        s.read();
        return true;
    }
    return false;
}

}  // namespace

bool prim_fetch_disruptions(Disruption *out, uint8_t *count, char *err, size_t errLen) {
    *count = 0;

    // Only what applies today (local time).
    char url[256];
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    char day[16];
    strftime(day, sizeof(day), "%Y%m%d", &lt);
    snprintf(url, sizeof(url), "%s&since=%sT000000&until=%sT235959", kDisruptionsUrlBase, day, day);

    Request *req = new Request();
    int code = req->get(url);
    if (code == 400) {  // date filter refused: ask for everything instead
        delete req;
        req = new Request();
        code = req->get(kDisruptionsUrlBase);
    }
    std::unique_ptr<Request> owner(req);
    if (code != 200) {
        describe_http_error(code, err, errLen);
        return false;
    }

    Stream &stream = req->http.getStream();
    stream.setTimeout(HTTP_TIMEOUT_MS);
    if (!seek_disruptions_array(stream)) return true;  // no disruptions at all

    JsonDocument filter;
    filter["status"] = true;
    filter["severity"]["effect"] = true;
    filter["messages"][0]["text"] = true;
    filter["messages"][0]["channel"]["types"] = true;

    // Disruptions are parsed one by one so the (large) response never sits in RAM.
    constexpr uint8_t kMaxParsed = 16;
    Disruption *tmp = (Disruption *)malloc(sizeof(Disruption) * kMaxParsed);
    char *text = (char *)malloc(1024);
    if (!tmp || !text) {
        free(tmp);
        free(text);
        snprintf(err, errLen, "M\xC3\xA9moire insuffisante");
        return false;
    }
    uint8_t n = 0;

    while (n < kMaxParsed) {
        if (skip_ws_peek(stream, ']')) break;
        JsonDocument doc;
        DeserializationError jerr = deserializeJson(doc, stream, DeserializationOption::Filter(filter),
                                                    DeserializationOption::NestingLimit(32));
        if (jerr) {
            if (n == 0) snprintf(err, errLen, "Info trafic illisible (%s)", jerr.c_str());
            break;
        }

        const char *status = doc["status"] | "active";
        bool keep = true;
        if (!strcmp(status, "past")) keep = false;

        const char *title = nullptr;
        const char *body = nullptr;
        size_t bodyLen = 0;
        for (JsonObjectConst m : doc["messages"].as<JsonArrayConst>()) {
            const char *t = m["text"] | (const char *)nullptr;
            if (!t) continue;
            JsonVariantConst types = m["channel"]["types"];
            if (!title && has_type(types, "title")) {
                title = t;
            } else if (strlen(t) > bodyLen) {  // keep the most detailed message
                body = t;
                bodyLen = strlen(t);
            }
        }
        if (!title) title = body;
        if (!title) keep = false;

        if (keep) {
            Disruption &d = tmp[n];
            memset(&d, 0, sizeof(d));
            d.active = strcmp(status, "future") != 0;
            d.severity = severity_from_effect(doc["severity"]["effect"] | (const char *)nullptr);

            html_to_text(text, title, 1024);
            strip_line_prefix(text);
            utf8_copy_trunc(d.title, text, sizeof(d.title));
            if (body && body != title) {
                html_to_text(text, body, 1024);
                utf8_copy_trunc(d.body, text, sizeof(d.body));
            }

            char folded[sizeof(d.title)];
            fold_ascii_lower(folded, d.title, sizeof(folded));
            bool elevator = strstr(folded, "ascenseur") || strstr(folded, "escalier") ||
                            strstr(folded, "escalator");
            bool duplicate = false;
            for (uint8_t i = 0; i < n; i++)
                if (!strcmp(tmp[i].title, d.title)) duplicate = true;
            if (!duplicate && (SHOW_ELEVATOR_OUTAGES || !elevator)) n++;
        }

        if (!stream.findUntil((char *)",", (char *)"]")) break;
    }
    free(text);

    // Ongoing first, then the most severe.
    std::stable_sort(tmp, tmp + n, [](const Disruption &a, const Disruption &b) {
        if (a.active != b.active) return a.active;
        return a.severity > b.severity;
    });
    *count = std::min<uint8_t>(n, MAX_DISRUPTIONS);
    memcpy(out, tmp, *count * sizeof(Disruption));
    free(tmp);
    return true;
}
