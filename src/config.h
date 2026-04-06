#ifndef CONFIG_H
#define CONFIG_H

#include "../cjson/cJSON.h"

/* Load JSON config from file. Caller must cJSON_Delete() the result. Returns NULL on error. */
cJSON *config_load(const char *path);

/* Save JSON config to file using atomic write (tmpfile + rename). Returns 0 on success, -1 on error. */
int config_save(const char *path, cJSON *cfg);

/* Convenience getters – return default_val on missing key or wrong type */
int         config_get_int(const cJSON *cfg, const char *section, const char *key, int default_val);
const char *config_get_str(const cJSON *cfg, const char *section, const char *key, const char *default_val);
int         config_get_bool(const cJSON *cfg, const char *section, const char *key, int default_val);

#endif /* CONFIG_H */
