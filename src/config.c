#include "config.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ─────────────────────────────────────────────── */

static int ji(cJSON *obj, const char *key, int def)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return (v && cJSON_IsNumber(v)) ? (int)v->valuedouble : def;
}

static const char *js(cJSON *obj, const char *key, const char *def)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : def;
}

static int jb(cJSON *obj, const char *key, int def)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!v) return def;
    if (cJSON_IsTrue(v))  return 1;
    if (cJSON_IsFalse(v)) return 0;
    return def;
}

static void load_mode(cJSON *modes, const char *name, ModeConfig *mc,
                      int def_current, int def_voltage)
{
    cJSON *m = cJSON_GetObjectItem(modes, name);
    mc->max_current_ma = ji(m, "max_current_ma", def_current);
    mc->max_voltage_mv = ji(m, "max_voltage_mv", def_voltage);
    const char *d = js(m, "description", "");
    snprintf(mc->description, sizeof(mc->description), "%s", d);
}

/* ── public API ──────────────────────────────────────────── */

void config_defaults(ChargeConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->version, sizeof(cfg->version), "1.0.1");

    cfg->max_limit             = 80;
    cfg->min_limit             = 20;
    snprintf(cfg->mode, sizeof(cfg->mode), "normal");
    cfg->fast_charge_enabled   = 1;
    cfg->temperature_threshold = 40;
    cfg->temperature_critical  = 45;

    cfg->mode_normal.max_current_ma      = 2000;
    cfg->mode_normal.max_voltage_mv      = 4350;
    cfg->mode_fast.max_current_ma        = 4000;
    cfg->mode_fast.max_voltage_mv        = 4400;
    cfg->mode_trickle.max_current_ma     = 500;
    cfg->mode_trickle.max_voltage_mv     = 4200;
    cfg->mode_power_saving.max_current_ma = 1000;
    cfg->mode_power_saving.max_voltage_mv = 4300;
    cfg->mode_super_saver.max_current_ma  = 300;
    cfg->mode_super_saver.max_voltage_mv  = 4100;

    snprintf(cfg->server_host, sizeof(cfg->server_host), "0.0.0.0");
    cfg->server_port  = 8080;
    cfg->server_debug = 0;

    cfg->stats_enabled       = 1;
    cfg->stats_retention_days = 90;

    cfg->notif_enabled            = 1;
    cfg->notif_charge_complete    = 1;
    cfg->notif_temperature_warning = 1;
}

int config_load(const char *path, ChargeConfig *cfg)
{
    config_defaults(cfg);

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz <= 0) { fclose(fp); return -1; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;

    const char *ver = js(root, "version", "1.0.0");
    snprintf(cfg->version, sizeof(cfg->version), "%s", ver);

    cJSON *charging = cJSON_GetObjectItem(root, "charging");
    if (charging) {
        cfg->max_limit             = ji(charging, "max_limit",             cfg->max_limit);
        cfg->min_limit             = ji(charging, "min_limit",             cfg->min_limit);
        const char *m = js(charging, "mode", cfg->mode);
        snprintf(cfg->mode, sizeof(cfg->mode), "%s", m);
        cfg->fast_charge_enabled   = jb(charging, "fast_charge_enabled",   cfg->fast_charge_enabled);
        cfg->temperature_threshold = ji(charging, "temperature_threshold", cfg->temperature_threshold);
        cfg->temperature_critical  = ji(charging, "temperature_critical",  cfg->temperature_critical);
    }

    cJSON *modes = cJSON_GetObjectItem(root, "modes");
    if (modes) {
        load_mode(modes, "normal",       &cfg->mode_normal,       2000, 4350);
        load_mode(modes, "fast",         &cfg->mode_fast,         4000, 4400);
        load_mode(modes, "trickle",      &cfg->mode_trickle,       500, 4200);
        load_mode(modes, "power_saving", &cfg->mode_power_saving, 1000, 4300);
        load_mode(modes, "super_saver",  &cfg->mode_super_saver,   300, 4100);
    }

    cJSON *server = cJSON_GetObjectItem(root, "server");
    if (server) {
        const char *h = js(server, "host", cfg->server_host);
        snprintf(cfg->server_host, sizeof(cfg->server_host), "%s", h);
        cfg->server_port  = ji(server, "port",  cfg->server_port);
        cfg->server_debug = jb(server, "debug", cfg->server_debug);
    }

    cJSON *stats = cJSON_GetObjectItem(root, "stats");
    if (stats) {
        cfg->stats_enabled        = jb(stats, "enabled",        cfg->stats_enabled);
        cfg->stats_retention_days = ji(stats, "retention_days", cfg->stats_retention_days);
    }

    cJSON *notif = cJSON_GetObjectItem(root, "notifications");
    if (notif) {
        cfg->notif_enabled              = jb(notif, "enabled",              cfg->notif_enabled);
        cfg->notif_charge_complete      = jb(notif, "charge_complete",      cfg->notif_charge_complete);
        cfg->notif_temperature_warning  = jb(notif, "temperature_warning",  cfg->notif_temperature_warning);
    }

    cJSON_Delete(root);
    return 0;
}

static cJSON *mode_to_json(const ModeConfig *mc)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;
    cJSON_AddNumberToObject(obj, "max_current_ma", mc->max_current_ma);
    cJSON_AddNumberToObject(obj, "max_voltage_mv", mc->max_voltage_mv);
    cJSON_AddStringToObject(obj, "description",    mc->description);
    return obj;
}

int config_save(const char *path, const ChargeConfig *cfg)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON_AddStringToObject(root, "version", cfg->version);

    cJSON *charging = cJSON_AddObjectToObject(root, "charging");
    cJSON_AddNumberToObject(charging, "max_limit",             cfg->max_limit);
    cJSON_AddNumberToObject(charging, "min_limit",             cfg->min_limit);
    cJSON_AddStringToObject(charging, "mode",                  cfg->mode);
    cJSON_AddBoolToObject  (charging, "fast_charge_enabled",   cfg->fast_charge_enabled);
    cJSON_AddNumberToObject(charging, "temperature_threshold", cfg->temperature_threshold);
    cJSON_AddNumberToObject(charging, "temperature_critical",  cfg->temperature_critical);

    cJSON *modes = cJSON_AddObjectToObject(root, "modes");
    cJSON_AddItemToObject(modes, "normal",       mode_to_json(&cfg->mode_normal));
    cJSON_AddItemToObject(modes, "fast",         mode_to_json(&cfg->mode_fast));
    cJSON_AddItemToObject(modes, "trickle",      mode_to_json(&cfg->mode_trickle));
    cJSON_AddItemToObject(modes, "power_saving", mode_to_json(&cfg->mode_power_saving));
    cJSON_AddItemToObject(modes, "super_saver",  mode_to_json(&cfg->mode_super_saver));

    cJSON *sched = cJSON_AddObjectToObject(root, "schedule");
    cJSON_AddBoolToObject(sched, "enabled", 0);
    cJSON_AddItemToObject(sched, "rules", cJSON_CreateArray());

    cJSON *notif = cJSON_AddObjectToObject(root, "notifications");
    cJSON_AddBoolToObject(notif, "enabled",              cfg->notif_enabled);
    cJSON_AddBoolToObject(notif, "charge_complete",      cfg->notif_charge_complete);
    cJSON_AddBoolToObject(notif, "temperature_warning",  cfg->notif_temperature_warning);

    cJSON *server = cJSON_AddObjectToObject(root, "server");
    cJSON_AddStringToObject(server, "host",  cfg->server_host);
    cJSON_AddNumberToObject(server, "port",  cfg->server_port);
    cJSON_AddBoolToObject  (server, "debug", cfg->server_debug);

    cJSON *stats = cJSON_AddObjectToObject(root, "stats");
    cJSON_AddBoolToObject  (stats, "enabled",        cfg->stats_enabled);
    cJSON_AddNumberToObject(stats, "retention_days", cfg->stats_retention_days);

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) return -1;

    FILE *fp = fopen(path, "w");
    if (!fp) { free(text); return -1; }
    fputs(text, fp);
    fclose(fp);
    free(text);
    return 0;
}
