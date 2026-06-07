#include "user_observation_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "cJSON.h"
#include "history_store.h"

esp_err_t user_observation_store_init(void)
{
    return ESP_OK;
}

esp_err_t user_observation_store_add(const user_observation_t *o)
{
    FILE *f = fopen("/spiffs/observations.jsonl", "a");
    if (!f) return ESP_FAIL;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp", (double)(o->timestamp > 0 ? o->timestamp : time(NULL)));
    cJSON_AddNumberToObject(root, "planter_id", o->planter_id);
    cJSON_AddStringToObject(root, "condition", o->condition);
    cJSON_AddStringToObject(root, "severity", o->severity);
    cJSON_AddStringToObject(root, "note", o->note);
    char *line = cJSON_PrintUnformatted(root);
    fprintf(f, "%s\n", line);
    fclose(f);
    free(line);
    cJSON_Delete(root);
    return ESP_OK;
}

char *user_observation_store_read_json(void)
{
    return history_store_read_json_array("observations");
}
