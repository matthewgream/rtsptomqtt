
// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------

/**
 * RTSP to MQTT: capture RTSP stream periodically and publish to MQTT
 */

// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------

#define CONFIG_FILE_DEFAULT "rtsptomqtt.cfg"

#define RTSP_URL_DEFAULT ""

#define INTERVAL_DEFAULT 30

#define MQTT_SERVER_DEFAULT "mqtt://localhost"
#define MQTT_CLIENT_DEFAULT "rtsptomqtt"
#define MQTT_TOPIC_DEFAULT "snapshots"

#define FFMPEG_COMMAND_EXE "ffmpeg"
#define FFMPEG_COMMAND_OPT_BASE "-loglevel quiet"
#define FFMPEG_COMMAND_OPT_RTSP "-rtsp_transport tcp"
#define FFMPEG_COMMAND_OPT_IMAGE "-vframes 1 -f image2pipe -"
#define FFMPEG_COMMAND_OPT_EXTRA_DEFAULT "-q:v 6 -pix_fmt yuvj420p -chroma_sample_location center"

#ifndef MAX_BUFFER_SIZE
#define MAX_BUFFER_SIZE 5 * 1024 * 1024 // 5MB
#endif

// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------

#include "include/config_linux.h"

#define MQTT_CONNECT_TIMEOUT 60
#define MQTT_PUBLISH_QOS 0
#define MQTT_PUBLISH_RETAIN false

#include "include/mqtt_linux.h"

#include "include/exec_linux.h"

// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------

const struct option config_options[] = {{"config", required_argument, 0, 0},      // config
                                        {"mqtt-client", required_argument, 0, 0}, // mqtt
                                        {"mqtt-server", required_argument, 0, 0},
                                        {"rtsp-url", required_argument, 0, 0},   // rtsp
                                        {"ffmpeg-opt", required_argument, 0, 0}, // ffmpeg
                                        {"interval", required_argument, 0, 0},   // interval
                                        {"debug", required_argument, 0, 0},      // debug
                                        {0, 0, 0, 0}};

MqttConfig mqtt_config;
const char *mqtt_topic;
const char *ffmpeg_opt;

bool config(const int argc, const char *argv[]) {
    if (!config_load(CONFIG_FILE_DEFAULT, argc, argv, config_options))
        return false;
    mqtt_config.server = config_get_string("mqtt-server", MQTT_SERVER_DEFAULT);
    mqtt_config.client = config_get_string("mqtt-client", MQTT_CLIENT_DEFAULT);
    mqtt_config.debug = config_get_bool("debug", false);
    mqtt_topic = config_get_string("mqtt-topic", MQTT_TOPIC_DEFAULT);
    return true;
}

// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------

unsigned char buffer[MAX_BUFFER_SIZE];
int snapshot_skipped = 0;

bool capture(const char *rtsp_url, const char *ffmpeg_opt) {

    const time_t time_entry = time(NULL);

    char ffmpeg_command[512];
    snprintf(ffmpeg_command, sizeof(ffmpeg_command), "%s %s -i %s %s %s %s", FFMPEG_COMMAND_EXE,
             FFMPEG_COMMAND_OPT_BASE, rtsp_url, FFMPEG_COMMAND_OPT_RTSP, ffmpeg_opt, FFMPEG_COMMAND_OPT_IMAGE);
    const size_t total_bytes = exec(ffmpeg_command, buffer, sizeof(buffer));
    if (total_bytes == 0)
        return false;

    const time_t total_time = time(NULL) - time_entry;
    char timestamp[15 + 1];
    strftime(timestamp, sizeof(timestamp) - 1, "%Y%m%d%H%M%S", localtime(&time_entry));
    char metadata[256];
    snprintf(metadata, sizeof(metadata), "{\"time\":\"%s\",\"size\":%zu}", timestamp, total_bytes);

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/imagedata", mqtt_topic);
    if (!mqtt_send(topic, buffer, total_bytes))
        return false;
    snprintf(topic, sizeof(topic), "%s/metadata", mqtt_topic);
    if (!mqtt_send(topic, (unsigned char *)metadata, strlen(metadata)))
        return false;

    printf("published '%s' (%zu bytes) [%ld seconds]\n", timestamp, total_bytes, total_time);
    return true;
}

void execute(volatile bool *running) {
    const int interval = config_get_integer("interval", INTERVAL_DEFAULT);
    const char *rtsp_url = config_get_string("rtsp-url", RTSP_URL_DEFAULT);
    const char *ffmpeg_opt = config_get_string("ffmpeg-opt", FFMPEG_COMMAND_OPT_EXTRA_DEFAULT);
    printf("executing (interval=%d seconds)\n", interval);
    while (*running) {
        const time_t time_entry = time(NULL);
        if (!capture(rtsp_url, ffmpeg_opt))
            fprintf(stderr, "capture error, will retry\n");
        const time_t time_leave = time(NULL);
        time_t next = time_entry + interval;
        int skipped = 0;
        while (next < time_leave) {
            skipped++;
            next += interval;
        }
        if (skipped) {
            snapshot_skipped += skipped;
            printf("capture skipped (%d now / %d all)\n", skipped, snapshot_skipped);
        }
        while (*running && time(NULL) < next)
            sleep(1);
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------

volatile bool running = true;

void signal_handler(int sig __attribute__((unused))) {
    if (running) {
        printf("stopping\n");
        running = false;
    }
}

int main(int argc, const char **argv) {
    setbuf(stdout, NULL);
    printf("starting\n");
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    if (!config(argc, argv)) {
        fprintf(stderr, "failed to load config\n");
        return EXIT_FAILURE;
    }
    if (!mqtt_begin(&mqtt_config)) {
        fprintf(stderr, "failed to connect mqtt\n");
        return EXIT_FAILURE;
    }
    execute(&running);
    mqtt_end();
    printf("stopped\n");
    return EXIT_SUCCESS;
}

// -----------------------------------------------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------------------------
