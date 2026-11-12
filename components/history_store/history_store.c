#include "history_store.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "history_store";

#define STORAGE_ROOT "/spiffs"
#define MOISTURE_LEGACY_PATH "/spiffs/moisture.jsonl"
#define MAX_MOISTURE_FILES 24
#define HISTORY_LINE_BYTES 512
#define MOISTURE_JSON_BYTES_PER_POINT 96

typedef struct {
    time_t timestamp;
    int percent;
    int raw;
    int gpio;
} moisture_point_t;

typedef struct {
    int64_t timestamp_sum;
    int64_t percent_sum;
    int64_t raw_sum;
    uint32_t count;
    int gpio;
} moisture_bucket_t;

static SemaphoreHandle_t g_history_mutex;

static void lock_history(void)
{
    if (g_history_mutex) xSemaphoreTake(g_history_mutex, portMAX_DELAY);
}

static void unlock_history(void)
{
    if (g_history_mutex) xSemaphoreGive(g_history_mutex);
}

static const char *path_for_kind(const char *kind)
{
    if (strcmp(kind, "watering") == 0) return "/spiffs/watering.jsonl";
    if (strcmp(kind, "reservoir") == 0) return "/spiffs/reservoir.jsonl";
    if (strcmp(kind, "moisture") == 0) return MOISTURE_LEGACY_PATH;
    if (strcmp(kind, "diagnostics") == 0) return "/spiffs/diagnostics.jsonl";
    return "/spiffs/watering.jsonl";
}

static size_t budget_for_kind(const char *kind)
{
    if (strcmp(kind, "diagnostics") == 0) return 32 * 1024;
    if (strcmp(kind, "watering") == 0) return 40 * 1024;
    if (strcmp(kind, "reservoir") == 0) return 16 * 1024;
    return 0;
}

static bool safe_replace(const char *path, const char *tmp_path)
{
    char backup[96];
    snprintf(backup, sizeof(backup), "%s.bak", path);
    remove(backup);
    if (rename(path, backup) != 0) return false;
    if (rename(tmp_path, path) != 0) {
        rename(backup, path);
        return false;
    }
    remove(backup);
    return true;
}

static void trim_file_to_budget(const char *path, size_t budget)
{
    if (budget == 0) return;
    struct stat st;
    if (stat(path, &st) != 0 || (size_t)st.st_size <= budget) return;

    char tmp_path[96];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    FILE *in = fopen(path, "r");
    FILE *out = fopen(tmp_path, "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        remove(tmp_path);
        return;
    }

    size_t target = budget * 3 / 4;
    long start = st.st_size > (off_t)target ? st.st_size - (long)target : 0;
    if (start > 0) {
        fseek(in, start, SEEK_SET);
        char partial[HISTORY_LINE_BYTES];
        fgets(partial, sizeof(partial), in);
    }
    char line[HISTORY_LINE_BYTES];
    while (fgets(line, sizeof(line), in)) fputs(line, out);
    bool ok = !ferror(in) && !ferror(out) && fflush(out) == 0;
    if (ok) fsync(fileno(out));
    fclose(in);
    fclose(out);
    if (!ok || !safe_replace(path, tmp_path)) {
        remove(tmp_path);
        ESP_LOGW(TAG, "could not rotate %s", path);
    }
}

static bool moisture_segment_path(time_t timestamp, char path[64])
{
    if (timestamp < 1700000000) return false;
    struct tm utc;
    gmtime_r(&timestamp, &utc);
    snprintf(path, 64, "/spiffs/moisture-%04d-%02d.jsonl", utc.tm_year + 1900, utc.tm_mon + 1);
    return true;
}

static bool is_moisture_segment(const char *name)
{
    int year = 0;
    int month = 0;
    char extra = '\0';
    return sscanf(name, "moisture-%4d-%2d.jsonl%c", &year, &month, &extra) == 2 &&
           year >= 2023 && month >= 1 && month <= 12;
}

static int compare_paths(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static size_t collect_moisture_files(char paths[MAX_MOISTURE_FILES][64])
{
    size_t count = 0;
    struct stat st;
    if (stat(MOISTURE_LEGACY_PATH, &st) == 0 && st.st_size > 0) {
        snprintf(paths[count++], 64, "%s", MOISTURE_LEGACY_PATH);
    }
    size_t segment_start = count;
    DIR *dir = opendir(STORAGE_ROOT);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && count < MAX_MOISTURE_FILES) {
            if (!is_moisture_segment(entry->d_name)) continue;
            int year = 0;
            int month = 0;
            sscanf(entry->d_name, "moisture-%4d-%2d.jsonl", &year, &month);
            snprintf(paths[count++], 64, "/spiffs/moisture-%04d-%02d.jsonl", year, month);
        }
        closedir(dir);
    }
    if (count > segment_start + 1) {
        qsort(paths + segment_start, count - segment_start, sizeof(paths[0]), compare_paths);
    }
    return count;
}

static bool parse_moisture_point(const char *line, moisture_point_t *point)
{
    cJSON *item = cJSON_Parse(line);
    if (!item) return false;
    const cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(item, "timestamp");
    const cJSON *percent = cJSON_GetObjectItemCaseSensitive(item, "percent");
    const cJSON *raw = cJSON_GetObjectItemCaseSensitive(item, "raw");
    const cJSON *gpio = cJSON_GetObjectItemCaseSensitive(item, "gpio");
    bool valid = cJSON_IsNumber(timestamp) && cJSON_IsNumber(percent) && cJSON_IsNumber(raw);
    if (valid) {
        point->timestamp = (time_t)timestamp->valuedouble;
        point->percent = percent->valueint;
        point->raw = raw->valueint;
        point->gpio = cJSON_IsNumber(gpio) ? gpio->valueint : -1;
    }
    cJSON_Delete(item);
    return valid;
}

static esp_err_t append_json(const char *kind, cJSON *json, time_t timestamp)
{
    char *line = cJSON_PrintUnformatted(json);
    if (!line) return ESP_ERR_NO_MEM;
    char moisture_path[64];
    const char *path = path_for_kind(kind);
    if (strcmp(kind, "moisture") == 0 && moisture_segment_path(timestamp, moisture_path)) {
        path = moisture_path;
    }

    lock_history();
    FILE *f = fopen(path, "a");
    esp_err_t err = ESP_FAIL;
    if (f) {
        bool wrote = fprintf(f, "%s\n", line) > 0;
        bool closed = fclose(f) == 0;
        err = wrote && closed ? ESP_OK : ESP_FAIL;
        if (err == ESP_OK && strcmp(kind, "moisture") != 0) {
            trim_file_to_budget(path, budget_for_kind(kind));
        }
    }
    unlock_history();
    free(line);
    return err;
}

esp_err_t history_store_init(void)
{
    g_history_mutex = xSemaphoreCreateMutex();
    if (!g_history_mutex) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "segmented flash history ready");
    return ESP_OK;
}

esp_err_t history_store_log_watering(const watering_event_t *e)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp", (double)e->timestamp);
    cJSON_AddStringToObject(root, "type", event_type_to_string(e->type));
    cJSON_AddNumberToObject(root, "duration_sec", e->duration_sec);
    cJSON_AddBoolToObject(root, "reservoir_ok_at_start", e->reservoir_ok_at_start);
    cJSON_AddBoolToObject(root, "completed", e->completed);
    cJSON_AddStringToObject(root, "stopped_reason", stop_reason_to_string(e->stopped_reason));
    esp_err_t err = append_json("watering", root, e->timestamp);
    cJSON_Delete(root);
    return err;
}

esp_err_t history_store_log_reservoir(bool reservoir_ok)
{
    time_t now = time(NULL);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp", (double)now);
    cJSON_AddBoolToObject(root, "reservoir_ok", reservoir_ok);
    esp_err_t err = append_json("reservoir", root, now);
    cJSON_Delete(root);
    return err;
}

esp_err_t history_store_log_moisture(int percent, int raw, int gpio)
{
    time_t now = time(NULL);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp", (double)now);
    cJSON_AddNumberToObject(root, "percent", percent);
    cJSON_AddNumberToObject(root, "raw", raw);
    cJSON_AddNumberToObject(root, "gpio", gpio);
    esp_err_t err = append_json("moisture", root, now);
    cJSON_Delete(root);
    return err;
}

esp_err_t history_store_log_diagnostic(const char *message)
{
    time_t now = time(NULL);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp", (double)now);
    cJSON_AddStringToObject(root, "message", message);
    esp_err_t err = append_json("diagnostics", root, now);
    cJSON_Delete(root);
    return err;
}

esp_err_t history_store_trim_moisture(uint32_t days_to_keep)
{
    if (days_to_keep < 1) days_to_keep = 1;
    if (days_to_keep > 365) days_to_keep = 365;
    time_t now = time(NULL);
    if (now < 1700000000) return ESP_OK;
    time_t cutoff = now - (time_t)days_to_keep * 86400;
    struct tm cutoff_utc;
    gmtime_r(&cutoff, &cutoff_utc);
    int cutoff_month = (cutoff_utc.tm_year + 1900) * 12 + cutoff_utc.tm_mon;

    lock_history();
    DIR *dir = opendir(STORAGE_ROOT);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            int year = 0;
            int month = 0;
            if (sscanf(entry->d_name, "moisture-%4d-%2d.jsonl", &year, &month) != 2) continue;
            int segment_month = year * 12 + month - 1;
            if (segment_month < cutoff_month) {
                char path[64];
                snprintf(path, sizeof(path), "/spiffs/moisture-%04d-%02d.jsonl", year, month);
                remove(path);
            }
        }
        closedir(dir);
    }

    FILE *legacy = fopen(MOISTURE_LEGACY_PATH, "r");
    if (legacy) {
        bool has_recent_or_unknown = false;
        char line[HISTORY_LINE_BYTES];
        while (fgets(line, sizeof(line), legacy)) {
            moisture_point_t point;
            if (!parse_moisture_point(line, &point) || point.timestamp >= cutoff) {
                has_recent_or_unknown = true;
                break;
            }
        }
        fclose(legacy);
        if (!has_recent_or_unknown) remove(MOISTURE_LEGACY_PATH);
    }
    unlock_history();
    return ESP_OK;
}

static bool point_in_range(const moisture_point_t *point, time_t cutoff, time_t now)
{
    return point->timestamp >= cutoff && point->timestamp <= now + 300 &&
           point->percent >= 0 && point->percent <= 100;
}

static size_t count_moisture_points(char paths[MAX_MOISTURE_FILES][64], size_t file_count,
                                    time_t cutoff, time_t now)
{
    size_t count = 0;
    char line[HISTORY_LINE_BYTES];
    for (size_t i = 0; i < file_count; i++) {
        FILE *f = fopen(paths[i], "r");
        if (!f) continue;
        while (fgets(line, sizeof(line), f)) {
            moisture_point_t point;
            if (parse_moisture_point(line, &point) && point_in_range(&point, cutoff, now)) count++;
        }
        fclose(f);
    }
    return count;
}

static size_t append_point_json(char *out, size_t used, size_t capacity,
                                const moisture_point_t *point, bool first)
{
    int written = snprintf(out + used, capacity - used,
                           "%s{\"timestamp\":%lld,\"percent\":%d,\"raw\":%d,\"gpio\":%d}",
                           first ? "" : ",", (long long)point->timestamp,
                           point->percent, point->raw, point->gpio);
    return written > 0 && (size_t)written < capacity - used ? used + (size_t)written : used;
}

char *history_store_read_moisture_json(uint32_t days, size_t max_points)
{
    if (days < 1) days = 1;
    if (days > 365) days = 365;
    if (max_points < 32) max_points = 32;
    if (max_points > 1000) max_points = 1000;
    time_t now = time(NULL);
    time_t cutoff = now >= 1700000000 ? now - (time_t)days * 86400 : 0;
    char paths[MAX_MOISTURE_FILES][64];

    lock_history();
    size_t file_count = collect_moisture_files(paths);
    size_t point_count = count_moisture_points(paths, file_count, cutoff, now);
    if (point_count == 0) {
        unlock_history();
        char *empty = malloc(3);
        if (empty) memcpy(empty, "[]", 3);
        return empty;
    }

    size_t output_points = point_count < max_points ? point_count : max_points;
    size_t capacity = output_points * MOISTURE_JSON_BYTES_PER_POINT + 3;
    char *out = malloc(capacity);
    if (!out) {
        unlock_history();
        return NULL;
    }
    size_t used = 1;
    out[0] = '[';
    bool first = true;
    char line[HISTORY_LINE_BYTES];

    if (point_count <= max_points) {
        for (size_t i = 0; i < file_count; i++) {
            FILE *f = fopen(paths[i], "r");
            if (!f) continue;
            while (fgets(line, sizeof(line), f)) {
                moisture_point_t point;
                if (!parse_moisture_point(line, &point) || !point_in_range(&point, cutoff, now)) continue;
                used = append_point_json(out, used, capacity, &point, first);
                first = false;
            }
            fclose(f);
        }
    } else {
        moisture_bucket_t *buckets = calloc(max_points, sizeof(*buckets));
        if (!buckets) {
            free(out);
            unlock_history();
            return NULL;
        }
        int64_t range_seconds = (int64_t)days * 86400;
        int64_t bucket_seconds = (range_seconds + (int64_t)max_points - 1) / (int64_t)max_points;
        for (size_t i = 0; i < file_count; i++) {
            FILE *f = fopen(paths[i], "r");
            if (!f) continue;
            while (fgets(line, sizeof(line), f)) {
                moisture_point_t point;
                if (!parse_moisture_point(line, &point) || !point_in_range(&point, cutoff, now)) continue;
                size_t index = (size_t)((point.timestamp - cutoff) / bucket_seconds);
                if (index >= max_points) index = max_points - 1;
                buckets[index].timestamp_sum += point.timestamp;
                buckets[index].percent_sum += point.percent;
                buckets[index].raw_sum += point.raw;
                buckets[index].gpio = point.gpio;
                buckets[index].count++;
            }
            fclose(f);
        }
        for (size_t i = 0; i < max_points; i++) {
            if (!buckets[i].count) continue;
            moisture_point_t point = {
                .timestamp = (time_t)(buckets[i].timestamp_sum / buckets[i].count),
                .percent = (int)((buckets[i].percent_sum + buckets[i].count / 2) / buckets[i].count),
                .raw = (int)((buckets[i].raw_sum + buckets[i].count / 2) / buckets[i].count),
                .gpio = buckets[i].gpio,
            };
            used = append_point_json(out, used, capacity, &point, first);
            first = false;
        }
        free(buckets);
    }
    out[used++] = ']';
    out[used] = '\0';
    unlock_history();
    return out;
}

char *history_store_read_json_array(const char *kind)
{
    if (strcmp(kind, "moisture") == 0) return history_store_read_moisture_json(365, 1000);
    lock_history();
    FILE *f = fopen(path_for_kind(kind), "r");
    cJSON *array = cJSON_CreateArray();
    if (f) {
        char line[HISTORY_LINE_BYTES];
        while (fgets(line, sizeof(line), f)) {
            cJSON *item = cJSON_Parse(line);
            if (item) cJSON_AddItemToArray(array, item);
        }
        fclose(f);
    }
    char *out = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);
    unlock_history();
    return out;
}

esp_err_t history_store_storage_info(size_t *total_bytes, size_t *used_bytes)
{
    return esp_spiffs_info("storage", total_bytes, used_bytes);
}
