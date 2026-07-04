/**
 * @file engine.c
 * @brief Generic rules engine — implementation.
 *
 * Registries are flat static arrays indexed by position.
 * Rules are evaluated in a simple loop with support for:
 *   CONTINUOUS, ONE_SHOT, EDGE_RISING, EDGE_FALLING,
 *   DEBOUNCED, COOLDOWN.
 *
 * NVS persistence: rules and param values survive reboots.
 */

#include "engine.h"
#include "app_config.h"
#include "app_logging.h"
#include "utils/nvs_utils.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ENGINE";

/* ── NVS namespace ───────────────────────────────────────────────────── */
#define NVS_NS_ENGINE    "ctrl_eng"

/* ── Eval defaults ───────────────────────────────────────────────────── */
#ifndef ENGINE_EVAL_INTERVAL_MS
#define ENGINE_EVAL_INTERVAL_MS  100
#endif
#ifndef ENGINE_DEBOUNCE_DEFAULT
#define ENGINE_DEBOUNCE_DEFAULT  5
#endif
#ifndef ENGINE_COOLDOWN_DEFAULT
#define ENGINE_COOLDOWN_DEFAULT  50
#endif

/* ── Internal state ──────────────────────────────────────────────────── */
static engine_source_t  s_sources[ENGINE_MAX_SOURCES];
static uint8_t          s_source_count = 0;

static engine_target_t  s_targets[ENGINE_MAX_TARGETS];
static uint8_t          s_target_count = 0;

static engine_param_t   s_params[ENGINE_MAX_PARAMS];
static uint8_t          s_param_count = 0;

static engine_rule_t    s_rules[ENGINE_MAX_RULES];
static uint8_t          s_rule_count = 0;

static SemaphoreHandle_t s_mutex = NULL;
static void             *s_nvs   = NULL;
static bool              s_inited = false;

/* ──────────────────────────────────────────────────────────────────────
 *  Helpers: find by ID
 * ────────────────────────────────────────────────────────────────────── */

static engine_source_t *find_source(uint8_t id)
{
    for (uint8_t i = 0; i < s_source_count; i++)
        if (s_sources[i].id == id) return &s_sources[i];
    return NULL;
}

static engine_target_t *find_target(uint8_t id)
{
    for (uint8_t i = 0; i < s_target_count; i++)
        if (s_targets[i].id == id) return &s_targets[i];
    return NULL;
}

static engine_param_t *find_param(uint8_t id)
{
    for (uint8_t i = 0; i < s_param_count; i++)
        if (s_params[i].id == id) return &s_params[i];
    return NULL;
}

static engine_rule_t *find_rule(uint8_t id)
{
    for (uint8_t i = 0; i < s_rule_count; i++)
        if (s_rules[i].id == id) return &s_rules[i];
    return NULL;
}



/* ──────────────────────────────────────────────────────────────────────
 *  Registration
 * ────────────────────────────────────────────────────────────────────── */

esp_err_t engine_register_source(uint8_t id, const char *name,
                                  engine_source_read_fn_t read_fn,
                                  void *user_data)
{
    if (!read_fn || !name) return ESP_ERR_INVALID_ARG;
    if (find_source(id)) return ESP_ERR_INVALID_STATE;
    if (s_source_count >= ENGINE_MAX_SOURCES) return ESP_ERR_NO_MEM;

    engine_source_t *s = &s_sources[s_source_count++];
    s->id       = id;
    s->read     = read_fn;
    s->user_data = user_data;
    s->cached   = 0;
    s->last_read_ms = 0;
    strncpy(s->name, name, ENGINE_NAME_LEN - 1);
    s->name[ENGINE_NAME_LEN - 1] = '\0';

    APP_LOGI(TAG, "Registered source [%u] '%s'", id, s->name);
    return ESP_OK;
}

esp_err_t engine_register_target(uint8_t id, const char *name,
                                  engine_target_write_fn_t write_fn,
                                  void *user_data)
{
    if (!write_fn || !name) return ESP_ERR_INVALID_ARG;
    if (find_target(id)) return ESP_ERR_INVALID_STATE;
    if (s_target_count >= ENGINE_MAX_TARGETS) return ESP_ERR_NO_MEM;

    engine_target_t *t = &s_targets[s_target_count++];
    t->id        = id;
    t->write     = write_fn;
    t->user_data = user_data;
    t->last_value = 0;
    strncpy(t->name, name, ENGINE_NAME_LEN - 1);
    t->name[ENGINE_NAME_LEN - 1] = '\0';

    APP_LOGI(TAG, "Registered target [%u] '%s'", id, t->name);
    return ESP_OK;
}

esp_err_t engine_register_param(uint8_t id, const char *name,
                                 float default_val, float min_val,
                                 float max_val)
{
    if (!name) return ESP_ERR_INVALID_ARG;
    if (find_param(id)) return ESP_ERR_INVALID_STATE;
    if (s_param_count >= ENGINE_MAX_PARAMS) return ESP_ERR_NO_MEM;

    engine_param_t *p = &s_params[s_param_count++];
    p->id            = id;
    p->default_value = default_val;
    p->min_value     = min_val;
    p->max_value     = max_val;
    p->value         = default_val; /* will be overridden by NVS load */
    strncpy(p->name, name, ENGINE_NAME_LEN - 1);
    p->name[ENGINE_NAME_LEN - 1] = '\0';

    APP_LOGI(TAG, "Registered param [%u] '%s'", id, p->name);
    return ESP_OK;
}

/* ──────────────────────────────────────────────────────────────────────
 *  Runtime value access
 * ────────────────────────────────────────────────────────────────────── */

float engine_read_source(uint8_t id)
{
    engine_source_t *s = find_source(id);
    if (!s) return 0;
    s->cached = s->read(s->user_data);
    s->last_read_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    return s->cached;
}

esp_err_t engine_write_target(uint8_t id, float value)
{
    engine_target_t *t = find_target(id);
    if (!t) return ESP_ERR_NOT_FOUND;
    esp_err_t err = t->write(value, t->user_data);
    if (err == ESP_OK) t->last_value = value;
    return err;
}

float engine_get_param(uint8_t id)
{
    engine_param_t *p = find_param(id);
    return p ? p->value : 0;
}

esp_err_t engine_set_param(uint8_t id, float value)
{
    engine_param_t *p = find_param(id);
    if (!p) return ESP_ERR_NOT_FOUND;

    /* Clamp */
    if (value < p->min_value) value = p->min_value;
    if (value > p->max_value) value = p->max_value;

    p->value = value;
    nvs_util_save_param_value(s_nvs, p->id, p->value);
    return ESP_OK;
}

/* ──────────────────────────────────────────────────────────────────────
 *  Condition evaluation
 * ────────────────────────────────────────────────────────────────────── */

static float resolve_source(const engine_condition_t *c)
{
    if (c->from == ENGINE_COND_SOURCE) {
        engine_source_t *s = find_source(c->src_id);
        return s ? s->cached : 0;
    } else {
        engine_param_t *p = find_param(c->src_id);
        return p ? p->value : 0;
    }
}

static bool eval_condition(const engine_condition_t *c)
{
    float v = resolve_source(c);
    switch (c->op) {
    case ENGINE_OP_EQUAL:     return fabsf(v - c->value) < 0.001f;
    case ENGINE_OP_NOT_EQUAL: return fabsf(v - c->value) >= 0.001f;
    case ENGINE_OP_GT:        return v > c->value;
    case ENGINE_OP_LT:        return v < c->value;
    case ENGINE_OP_GTE:       return v >= c->value;
    case ENGINE_OP_LTE:       return v <= c->value;
    }
    return false;
}

static bool eval_conditions(const engine_rule_t *rule)
{
    if (rule->num_conditions == 0) return false;

    bool result = eval_condition(&rule->conditions[0]);
    for (uint8_t i = 1; i < rule->num_conditions; i++) {
        bool next = eval_condition(&rule->conditions[i]);
        if (rule->logic[i - 1] == ENGINE_LOGIC_AND)
            result = result && next;
        else
            result = result || next;
    }
    return result;
}

/* ──────────────────────────────────────────────────────────────────────
 *  Action execution
 * ────────────────────────────────────────────────────────────────────── */

static void exec_actions(const engine_rule_t *rule)
{
    for (uint8_t i = 0; i < rule->num_actions; i++) {
        const engine_action_t *a = &rule->actions[i];
        if (a->type == ENGINE_ACTION_SET_TARGET) {
            engine_write_target(a->tgt_id, a->value);
        } else if (a->type == ENGINE_ACTION_SET_PARAM) {
            engine_set_param(a->tgt_id, a->value);
        }
    }
}

/* ──────────────────────────────────────────────────────────────────────
 *  Rule evaluation (with trigger-type logic)
 * ────────────────────────────────────────────────────────────────────── */

static void eval_rule(engine_rule_t *rule)
{
    if (!rule->enabled) return;

    bool cond_true = eval_conditions(rule);
    bool triggered = false;

    switch (rule->type) {

    case ENGINE_RULE_CONTINUOUS:
        triggered = cond_true;
        break;

    case ENGINE_RULE_ONE_SHOT:
        if (cond_true) {
            triggered = true;
            rule->enabled = false;  /* auto-disable */
            APP_LOGI(TAG, "Rule [%u] '%s' ONE_SHOT fired, disabling", rule->id, rule->name);
        }
        break;

    case ENGINE_RULE_EDGE_RISING:
        if (cond_true && !rule->last_result) {
            triggered = true;
        }
        break;

    case ENGINE_RULE_EDGE_FALLING:
        if (!cond_true && rule->last_result) {
            triggered = true;
        }
        break;

    case ENGINE_RULE_DEBOUNCED:
        if (cond_true) {
            rule->debounce_count++;
            if (rule->debounce_count >= rule->debounce_limit) {
                triggered = true;
                rule->debounce_count = 0;
            }
        } else {
            rule->debounce_count = 0;
        }
        break;

    case ENGINE_RULE_COOLDOWN:
        if (rule->cooldown_count > 0) {
            rule->cooldown_count--;
        } else if (cond_true) {
            triggered = true;
            rule->cooldown_count = rule->cooldown_limit;
        }
        break;
    }

    rule->last_result = cond_true;

    if (triggered) {
        exec_actions(rule);
    }
}

/* ──────────────────────────────────────────────────────────────────────
 *  Public: rules CRUD
 *  NOTE: All mutating functions hold s_mutex to avoid race conditions
 *        with engine_eval_loop running in the control task.
 * ────────────────────────────────────────────────────────────────────── */

esp_err_t engine_rule_create(const engine_rule_t *rule)
{
    if (!rule) return ESP_ERR_INVALID_ARG;
    if (rule->id == 0) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (find_rule(rule->id)) {
        xSemaphoreGive(s_mutex);
        APP_LOGW(TAG, "Create: rule [%u] already exists, use update instead", rule->id);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_rule_count >= ENGINE_MAX_RULES) {
        xSemaphoreGive(s_mutex);
        APP_LOGE(TAG, "Create: no room for rule [%u] (%u/%u used)",
                 rule->id, s_rule_count, ENGINE_MAX_RULES);
        return ESP_ERR_NO_MEM;
    }

    engine_rule_t copy = *rule;
    /* Reset runtime state */
    copy.last_result    = false;
    copy.debounce_count = 0;
    copy.cooldown_count = 0;
    if (copy.type == ENGINE_RULE_DEBOUNCED && copy.debounce_limit == 0)
        copy.debounce_limit = ENGINE_DEBOUNCED_DEFAULT;
    if (copy.type == ENGINE_RULE_COOLDOWN && copy.cooldown_limit == 0)
        copy.cooldown_limit = ENGINE_COOLDOWN_DEFAULT;

    s_rules[s_rule_count++] = copy;

    xSemaphoreGive(s_mutex);

    /* NVS write outside mutex (flash writes are slow) */
    nvs_util_save_rule(s_nvs, &copy);

    APP_LOGI(TAG, "Created rule [%u] '%s' (%u/%u rules active)",
             copy.id, copy.name, s_rule_count, ENGINE_MAX_RULES);
    return ESP_OK;
}

esp_err_t engine_rule_update(uint8_t rule_id, const engine_rule_t *rule)
{
    if (!rule || rule_id == 0) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    engine_rule_t *existing = find_rule(rule_id);
    if (!existing) {
        xSemaphoreGive(s_mutex);
        APP_LOGW(TAG, "Update: rule [%u] not found", rule_id);
        return ESP_ERR_NOT_FOUND;
    }

    /* Preserve runtime state */
    bool    last_result    = existing->last_result;
    uint16_t debounce_cnt  = existing->debounce_count;
    uint16_t cooldown_cnt  = existing->cooldown_count;

    *existing = *rule;
    existing->id             = rule_id;
    existing->last_result    = last_result;
    existing->debounce_count = debounce_cnt;
    existing->cooldown_count = cooldown_cnt;

    xSemaphoreGive(s_mutex);

    /* NVS write outside mutex */
    nvs_util_save_rule(s_nvs, existing);
    APP_LOGI(TAG, "Updated rule [%u] '%s'", rule_id, rule->name);
    return ESP_OK;
}

esp_err_t engine_rule_delete(uint8_t rule_id)
{
    if (rule_id == 0) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    for (uint8_t i = 0; i < s_rule_count; i++) {
        if (s_rules[i].id == rule_id) {
            /* Save the deleted rule's actions so we can reset orphaned targets */
            engine_action_t saved_actions[ENGINE_MAX_ACTIONS];
            uint8_t saved_num_actions = s_rules[i].num_actions;
            memcpy(saved_actions, s_rules[i].actions, sizeof(saved_actions));

            /* Shift remaining rules */
            for (uint8_t j = i; j < s_rule_count - 1; j++)
                s_rules[j] = s_rules[j + 1];
            s_rule_count--;
            memset(&s_rules[s_rule_count], 0, sizeof(engine_rule_t));

            /* After delete: check if any remaining rule still controls
             * the same targets. If not, reset them to 0 (OFF). */
            for (uint8_t a = 0; a < saved_num_actions; a++) {
                if (saved_actions[a].type != ENGINE_ACTION_SET_TARGET)
                    continue;

                uint8_t tgt_id = saved_actions[a].tgt_id;
                bool still_controlled = false;

                for (uint8_t r = 0; r < s_rule_count; r++) {
                    for (uint8_t ra = 0; ra < s_rules[r].num_actions; ra++) {
                        if (s_rules[r].actions[ra].type == ENGINE_ACTION_SET_TARGET
                            && s_rules[r].actions[ra].tgt_id == tgt_id) {
                            still_controlled = true;
                            break;
                        }
                    }
                    if (still_controlled) break;
                }

                if (!still_controlled) {
                    APP_LOGI(TAG, "Target [%u] orphaned by rule delete — resetting to 0",
                             tgt_id);
                    engine_write_target(tgt_id, 0);
                }
            }

            xSemaphoreGive(s_mutex);

            /* NVS erase outside mutex */
            nvs_util_erase_rule(s_nvs, rule_id);
            APP_LOGI(TAG, "Deleted rule [%u] (%u rules remaining)",
                     rule_id, s_rule_count);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_mutex);
    APP_LOGW(TAG, "Delete: rule [%u] not found", rule_id);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t engine_rule_enable(uint8_t rule_id, bool enable)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    engine_rule_t *r = find_rule(rule_id);
    if (!r) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    r->enabled = enable;

    xSemaphoreGive(s_mutex);

    nvs_util_save_rule(s_nvs, r);
    APP_LOGI(TAG, "Rule [%u] %s", rule_id, enable ? "enabled" : "disabled");
    return ESP_OK;
}

const engine_rule_t *engine_rule_get(uint8_t rule_id)
{
    return find_rule(rule_id);
}

uint8_t engine_rule_get_count(void)
{
    return s_rule_count;
}

uint8_t engine_get_max_rules(void)
{
    return ENGINE_MAX_RULES;
}

void engine_rule_save_all(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint8_t cnt = s_rule_count;
    engine_rule_t copy[ENGINE_MAX_RULES];
    memcpy(copy, s_rules, sizeof(copy));
    xSemaphoreGive(s_mutex);

    for (uint8_t i = 0; i < cnt; i++) {
        nvs_util_save_rule(s_nvs, &copy[i]);
    }
    APP_LOGI(TAG, "Saved all %u rules to NVS", cnt);
}

uint8_t engine_rule_get_all(const engine_rule_t **out_array)
{
    if (out_array) *out_array = s_rules;
    return s_rule_count;
}

/* ──────────────────────────────────────────────────────────────────────
 *  Lifecycle
 * ────────────────────────────────────────────────────────────────────── */

esp_err_t engine_init(void)
{
    if (s_inited) return ESP_OK;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    /* Open NVS namespace for engine */
    esp_err_t err = nvs_util_open(NVS_NS_ENGINE, &s_nvs);
    if (err != ESP_OK) {
        APP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    APP_LOGI(TAG, "NVS namespace '%s' opened, handle=%p", NVS_NS_ENGINE, s_nvs);

    /* Clear registries */
    s_source_count = 0;
    s_target_count = 0;
    s_param_count  = 0;
    s_rule_count   = 0;
    memset(s_sources, 0, sizeof(s_sources));
    memset(s_targets, 0, sizeof(s_targets));
    memset(s_params,  0, sizeof(s_params));
    memset(s_rules,   0, sizeof(s_rules));

    /* Load persisted data */
    nvs_util_load_rules(s_nvs, s_rules, ENGINE_MAX_RULES, &s_rule_count);

    /* Load param values */
    for (uint8_t i = 0; i < s_param_count; i++) {
        float val = 0;
        if (nvs_util_load_param_value(s_nvs, s_params[i].id, &val) == ESP_OK) {
            if (val < s_params[i].min_value) val = s_params[i].min_value;
            if (val > s_params[i].max_value) val = s_params[i].max_value;
            s_params[i].value = val;
        } else {
            s_params[i].value = s_params[i].default_value;
        }
    }
    APP_LOGI(TAG, "Loaded %u param values from NVS", s_param_count);

    /* Diagnostic dump */
    nvs_util_dump_entries(s_nvs, NVS_NS_ENGINE);

    s_inited = true;
    APP_LOGI(TAG, "Engine initialized");
    return ESP_OK;
}

void engine_eval_loop(void)
{
    if (!s_inited) return;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* 1. Read all sources */
    for (uint8_t i = 0; i < s_source_count; i++) {
        s_sources[i].cached = s_sources[i].read(s_sources[i].user_data);
    }

    /* 2. Evaluate all rules */
    for (uint8_t i = 0; i < s_rule_count; i++) {
        eval_rule(&s_rules[i]);
    }

    xSemaphoreGive(s_mutex);

    vTaskDelay(pdMS_TO_TICKS(ENGINE_EVAL_INTERVAL_MS));
}

void engine_deinit(void)
{
    if (s_nvs) {
        nvs_util_close(s_nvs);
        s_nvs = NULL;
    }
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    s_inited = false;
    APP_LOGI(TAG, "Engine deinitialized");
}

/* ═══════════════════════════════════════════════════════════════════════
 *  JSON serialization (lightweight, snprintf-based, no cJSON dependency)
 *
 *  Format is compact but valid JSON.  Caller provides the buffer.
 * ═══════════════════════════════════════════════════════════════════════ */

static const char *op_str(engine_op_t op)
{
    switch (op) {
    case ENGINE_OP_EQUAL:     return "eq";
    case ENGINE_OP_NOT_EQUAL: return "neq";
    case ENGINE_OP_GT:        return "gt";
    case ENGINE_OP_LT:        return "lt";
    case ENGINE_OP_GTE:       return "gte";
    case ENGINE_OP_LTE:       return "lte";
    }
    return "?";
}

static const char *logic_str(engine_logic_t l)
{
    return (l == ENGINE_LOGIC_AND) ? "and" : "or";
}

static const char *rule_type_str(engine_rule_type_t t)
{
    switch (t) {
    case ENGINE_RULE_CONTINUOUS:   return "continuous";
    case ENGINE_RULE_ONE_SHOT:     return "one_shot";
    case ENGINE_RULE_EDGE_RISING:  return "edge_rising";
    case ENGINE_RULE_EDGE_FALLING: return "edge_falling";
    case ENGINE_RULE_DEBOUNCED:    return "debounced";
    case ENGINE_RULE_COOLDOWN:     return "cooldown";
    }
    return "unknown";
}

static const char *action_type_str(engine_action_type_t t)
{
    return (t == ENGINE_ACTION_SET_TARGET) ? "target" : "param";
}

static const char *cond_from_str(engine_cond_from_t f)
{
    return (f == ENGINE_COND_SOURCE) ? "source" : "param";
}

esp_err_t engine_rule_to_json(const engine_rule_t *rule,
                               char *buf, size_t len)
{
    if (!rule || !buf) return ESP_ERR_INVALID_ARG;

    int n = snprintf(buf, len,
        "{\"id\":%u,\"name\":\"%s\",\"enabled\":%s,\"type\":\"%s\","
        "\"conditions\":[",
        rule->id,
        rule->name,
        rule->enabled ? "true" : "false",
        rule_type_str(rule->type));

    for (uint8_t i = 0; i < rule->num_conditions && n < (int)len; i++) {
        if (i > 0) n += snprintf(buf + n, len - n, ",");
        const engine_condition_t *c = &rule->conditions[i];
        n += snprintf(buf + n, len - n,
            "{\"from\":\"%s\",\"id\":%u,\"op\":\"%s\",\"value\":%.2f}",
            cond_from_str(c->from), c->src_id, op_str(c->op), c->value);
    }

    n += snprintf(buf + n, len - n, "],\"logic\":[");
    for (uint8_t i = 0; i < rule->num_conditions - 1 && n < (int)len; i++) {
        if (i > 0) n += snprintf(buf + n, len - n, ",");
        n += snprintf(buf + n, len - n, "\"%s\"", logic_str(rule->logic[i]));
    }

    n += snprintf(buf + n, len - n, "],\"actions\":[");
    for (uint8_t i = 0; i < rule->num_actions && n < (int)len; i++) {
        if (i > 0) n += snprintf(buf + n, len - n, ",");
        const engine_action_t *a = &rule->actions[i];
        n += snprintf(buf + n, len - n,
            "{\"type\":\"%s\",\"id\":%u,\"value\":%.2f}",
            action_type_str(a->type), a->tgt_id, a->value);
    }

    n += snprintf(buf + n, len - n, "]}");
    return (n < (int)len) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t engine_rule_from_json(engine_rule_t *rule,
                                 const char *json, size_t json_len)
{
    /* Minimal JSON parser — enough for rule import via MQTT/Mesh.
     * Uses strstr-based parsing (good enough for embedded, low code size).
     * A full cJSON-based parser can replace this later if needed. */
    if (!rule || !json) return ESP_ERR_INVALID_ARG;
    memset(rule, 0, sizeof(*rule));

    const char *p;

    /* id */
    p = strstr(json, "\"id\":");
    if (p) rule->id = (uint8_t)atoi(p + 5);

    /* name */
    p = strstr(json, "\"name\":\"");
    if (p) {
        p += 8;
        int i = 0;
        while (*p && *p != '"' && i < ENGINE_RULE_NAME_LEN - 1)
            rule->name[i++] = *p++;
        rule->name[i] = '\0';
    }

    /* enabled */
    p = strstr(json, "\"enabled\":");
    if (p) rule->enabled = (strstr(p, "true") == p + 10);

    /* type */
    p = strstr(json, "\"type\":\"");
    if (p) {
        p += 8;
        if (strncmp(p, "continuous", 10) == 0)        rule->type = ENGINE_RULE_CONTINUOUS;
        else if (strncmp(p, "one_shot", 8) == 0)      rule->type = ENGINE_RULE_ONE_SHOT;
        else if (strncmp(p, "edge_rising", 11) == 0)  rule->type = ENGINE_RULE_EDGE_RISING;
        else if (strncmp(p, "edge_falling", 12) == 0) rule->type = ENGINE_RULE_EDGE_FALLING;
        else if (strncmp(p, "debounced", 9) == 0)     rule->type = ENGINE_RULE_DEBOUNCED;
        else if (strncmp(p, "cooldown", 8) == 0)      rule->type = ENGINE_RULE_COOLDOWN;
    }

    /* Count conditions (count of "from" occurrences in conditions array) */
    const char *scan = json;
    uint8_t cond_count = 0;
    while ((scan = strstr(scan, "\"from\":")) != NULL) {
        cond_count++;
        scan++;
    }
    rule->num_conditions = cond_count;
    if (rule->num_conditions > ENGINE_MAX_CONDITIONS)
        rule->num_conditions = ENGINE_MAX_CONDITIONS;

    /* Parse conditions — find each "from" block */
    scan = json;
    for (uint8_t i = 0; i < rule->num_conditions; i++) {
        engine_condition_t *c = &rule->conditions[i];
        p = strstr(scan, "\"from\":\"");
        if (!p) break;
        p += 8;
        if (strncmp(p, "source", 6) == 0) c->from = ENGINE_COND_SOURCE;
        else                              c->from = ENGINE_COND_PARAM;

        p = strstr(p, "\"id\":");
        if (p) c->src_id = (uint8_t)atoi(p + 5);

        p = strstr(p, "\"op\":\"");
        if (p) {
            p += 6;
            if (strncmp(p, "eq", 2) == 0)       c->op = ENGINE_OP_EQUAL;
            else if (strncmp(p, "neq", 3) == 0) c->op = ENGINE_OP_NOT_EQUAL;
            else if (strncmp(p, "gt", 2) == 0)  c->op = ENGINE_OP_GT;
            else if (strncmp(p, "lt", 2) == 0)  c->op = ENGINE_OP_LT;
            else if (strncmp(p, "gte", 3) == 0) c->op = ENGINE_OP_GTE;
            else if (strncmp(p, "lte", 3) == 0) c->op = ENGINE_OP_LTE;
        }

        p = strstr(p, "\"value\":");
        if (p) c->value = (float)atof(p + 8);

        scan = p ? p + 1 : json + strlen(json);
    }

    /* Parse logic array */
    p = strstr(json, "\"logic\":[");
    if (p) {
        p += 9;
        for (uint8_t i = 0; i < rule->num_conditions - 1; i++) {
            const char *lp = strstr(p, "\"");
            if (!lp) break;
            lp++;
            if (strncmp(lp, "and", 3) == 0) rule->logic[i] = ENGINE_LOGIC_AND;
            else                            rule->logic[i] = ENGINE_LOGIC_OR;
            p = lp + 3;
        }
    }

    /* Parse actions */
    p = strstr(json, "\"actions\":[");
    if (p) {
        p += 11;
        uint8_t act_count = 0;
        const char *ap = p;
        while ((ap = strstr(ap, "\"type\":\"")) != NULL && act_count < ENGINE_MAX_ACTIONS) {
            engine_action_t *a = &rule->actions[act_count];
            ap += 8;
            if (strncmp(ap, "target", 6) == 0) a->type = ENGINE_ACTION_SET_TARGET;
            else                               a->type = ENGINE_ACTION_SET_PARAM;

            const char *tp = strstr(ap, "\"id\":");
            if (tp) a->tgt_id = (uint8_t)atoi(tp + 5);

            tp = strstr(tp ? tp : ap, "\"value\":");
            if (tp) a->value = (float)atof(tp + 8);

            act_count++;
        }
        rule->num_actions = act_count;
    }

    return ESP_OK;
}

esp_err_t engine_sources_to_json(char *buf, size_t len)
{
    int n = snprintf(buf, len, "[");
    for (uint8_t i = 0; i < s_source_count && n < (int)len; i++) {
        if (i > 0) n += snprintf(buf + n, len - n, ",");
        n += snprintf(buf + n, len - n,
            "{\"id\":%u,\"name\":\"%s\",\"value\":%.2f}",
            s_sources[i].id, s_sources[i].name, s_sources[i].cached);
    }
    n += snprintf(buf + n, len - n, "]");
    return (n < (int)len) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t engine_targets_to_json(char *buf, size_t len)
{
    int n = snprintf(buf, len, "[");
    for (uint8_t i = 0; i < s_target_count && n < (int)len; i++) {
        if (i > 0) n += snprintf(buf + n, len - n, ",");
        n += snprintf(buf + n, len - n,
            "{\"id\":%u,\"name\":\"%s\",\"value\":%.2f}",
            s_targets[i].id, s_targets[i].name, s_targets[i].last_value);
    }
    n += snprintf(buf + n, len - n, "]");
    return (n < (int)len) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t engine_params_to_json(char *buf, size_t len)
{
    int n = snprintf(buf, len, "[");
    for (uint8_t i = 0; i < s_param_count && n < (int)len; i++) {
        if (i > 0) n += snprintf(buf + n, len - n, ",");
        n += snprintf(buf + n, len - n,
            "{\"id\":%u,\"name\":\"%s\",\"value\":%.2f,"
            "\"min\":%.2f,\"max\":%.2f}",
            s_params[i].id, s_params[i].name, s_params[i].value,
            s_params[i].min_value, s_params[i].max_value);
    }
    n += snprintf(buf + n, len - n, "]");
    return (n < (int)len) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t engine_export_all_json(char *buf, size_t len)
{
    int n = snprintf(buf, len, "{\"sources\":");
    esp_err_t err = engine_sources_to_json(buf + n, len - n);
    if (err != ESP_OK) return err;
    n += strlen(buf + n);

    n += snprintf(buf + n, len - n, ",\"targets\":");
    err = engine_targets_to_json(buf + n, len - n);
    if (err != ESP_OK) return err;
    n += strlen(buf + n);

    n += snprintf(buf + n, len - n, ",\"params\":");
    err = engine_params_to_json(buf + n, len - n);
    if (err != ESP_OK) return err;
    n += strlen(buf + n);

    /* Rules + capacity info */
    n += snprintf(buf + n, len - n, ",\"rule_count\":%u,\"max_rules\":%u,\"rules\":[",
                  s_rule_count, (uint8_t)ENGINE_MAX_RULES);
    for (uint8_t i = 0; i < s_rule_count; i++) {
        if (i > 0) n += snprintf(buf + n, len - n, ",");
        err = engine_rule_to_json(&s_rules[i], buf + n, len - n);
        if (err != ESP_OK) return err;
        n += strlen(buf + n);
    }
    n += snprintf(buf + n, len - n, "]}");
    return (n < (int)len) ? ESP_OK : ESP_ERR_NO_MEM;
}
