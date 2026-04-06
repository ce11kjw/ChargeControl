#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Load JSON config from path. Returns cJSON* on success, NULL on error. */
cJSON *config_load(const char *path)
{
    FILE *f = NULL;
    long len = 0;
    char *buf = NULL;
    cJSON *cfg = NULL;

    if (!path) return NULL;

    f = fopen(path, "r");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);

    buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }

    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf); fclose(f); return NULL;
    }
    buf[len] = '\0';
    fclose(f);

    cfg = cJSON_Parse(buf);
    free(buf);
    return cfg;
}

/* Save JSON config atomically (write to tmp file, then rename). */
int config_save(const char *path, cJSON *cfg)
{
    char tmp_path[4096];
    FILE *f = NULL;
    char *text = NULL;
    size_t text_len = 0;
    size_t written = 0;
    int ret = 0;

    if (!path || !cfg) return -1;

    /* Build tmp path: same directory, different name */
    if ((size_t)snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) >= sizeof(tmp_path)) return -1;

    text = cJSON_Print(cfg);
    if (!text) return -1;

    text_len = strlen(text);

    f = fopen(tmp_path, "w");
    if (!f) { free(text); return -1; }

    written = fwrite(text, 1, text_len, f);
    free(text);

    if (written != text_len) {
        fclose(f);
        remove(tmp_path);
        return -1;
    }

    if (fflush(f) != 0) {
        fclose(f);
        remove(tmp_path);
        return -1;
    }
    fclose(f);

    /* Atomic rename */
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return -1;
    }

    return ret;
}

/* Helper: get item from optional section->key or top-level key */
static const cJSON *get_item(const cJSON *cfg, const char *section, const char *key)
{
    if (!cfg || !key) return NULL;
    if (section) {
        const cJSON *sec = cJSON_GetObjectItem(cfg, section);
        if (sec) return cJSON_GetObjectItem(sec, key);
        return NULL;
    }
    return cJSON_GetObjectItem(cfg, key);
}

int config_get_int(const cJSON *cfg, const char *section, const char *key, int default_val)
{
    const cJSON *item = get_item(cfg, section, key);
    if (cJSON_IsNumber(item)) return item->valueint;
    return default_val;
}

const char *config_get_str(const cJSON *cfg, const char *section, const char *key, const char *default_val)
{
    const cJSON *item = get_item(cfg, section, key);
    if (cJSON_IsString(item)) return item->valuestring;
    return default_val;
}

int config_get_bool(const cJSON *cfg, const char *section, const char *key, int default_val)
{
    const cJSON *item = get_item(cfg, section, key);
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item) ? 1 : 0;
    if (cJSON_IsNumber(item)) return item->valueint != 0;
    return default_val;
}
