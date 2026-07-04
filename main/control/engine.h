/**
 * @file engine.h
 * @brief Generic rules engine — public API.
 *
 * The engine evaluates rules against registered sources/params and
 * fires actions on registered targets/params.  It has zero knowledge
 * of GPIO, sensors, or connectivity — the host app registers everything.
 */

#pragma once

#include "engine_types.h"
#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════
 *  Registration  (call once at startup, before engine_start)
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * @brief Register a data source (sensor, computed value, constant, …).
 * @param id        Unique ID (0 is reserved / unused).
 * @param name      Human-readable name (copied, max ENGINE_NAME_LEN).
 * @param read_fn   Callback that returns the current float value.
 * @param user_data Opaque pointer forwarded to read_fn.
 */
esp_err_t engine_register_source(uint8_t id, const char *name,
                                  engine_source_read_fn_t read_fn,
                                  void *user_data);

/**
 * @brief Register a controllable target (GPIO, PWM, MQTT publish, …).
 * @param id        Unique ID.
 * @param name      Human-readable name.
 * @param write_fn  Callback that applies a float value.
 * @param user_data Opaque pointer forwarded to write_fn.
 */
esp_err_t engine_register_target(uint8_t id, const char *name,
                                  engine_target_write_fn_t write_fn,
                                  void *user_data);

/**
 * @brief Register a tunable parameter (read/write, persisted to NVS).
 * @param id            Unique ID.
 * @param name          Human-readable name.
 * @param default_val   Initial value if nothing is stored in NVS.
 * @param min_val       Minimum allowed value.
 * @param max_val       Maximum allowed value.
 */
esp_err_t engine_register_param(uint8_t id, const char *name,
                                 float default_val, float min_val,
                                 float max_val);

/* ═══════════════════════════════════════════════════════════════════════
 *  Runtime value access
 * ═══════════════════════════════════════════════════════════════════════ */

/** Read a source (calls its read callback, caches the result). */
float engine_read_source(uint8_t id);

/** Write a value to a target (calls its write callback). */
esp_err_t engine_write_target(uint8_t id, float value);

/** Get the current value of a param. */
float engine_get_param(uint8_t id);

/**
 * @brief Set a param value (clamps to min/max, persists to NVS).
 */
esp_err_t engine_set_param(uint8_t id, float value);

/* ═══════════════════════════════════════════════════════════════════════
 *  Rules CRUD
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * @brief Create a new rule (copied into engine, persisted to NVS).
 *        The rule's id must not already exist.
 */
esp_err_t engine_rule_create(const engine_rule_t *rule);

/**
 * @brief Replace an existing rule (matched by rule->id).
 */
esp_err_t engine_rule_update(uint8_t rule_id, const engine_rule_t *rule);

/** Delete a rule by ID and remove from NVS. */
esp_err_t engine_rule_delete(uint8_t rule_id);

/** Enable or disable a rule without deleting it. */
esp_err_t engine_rule_enable(uint8_t rule_id, bool enable);

/** Get a pointer to a rule by ID (NULL if not found). */
const engine_rule_t *engine_rule_get(uint8_t rule_id);

/** Get the number of currently active rules. */
uint8_t engine_rule_get_count(void);

/** Get the maximum number of rules allowed. */
uint8_t engine_get_max_rules(void);

/**
 * @brief Re-save all rules to NVS.
 *        Useful after a bulk update or to verify persistence.
 */
void engine_rule_save_all(void);

/**
 * @brief Get pointers to all active rules.
 * @param out_array  Set to the internal rules array.
 * @return Number of active rules.
 */
uint8_t engine_rule_get_all(const engine_rule_t **out_array);

/* ═══════════════════════════════════════════════════════════════════════
 *  Lifecycle
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * @brief Initialise the engine (clears registries, loads NVS).
 *        Call engine_register_*() afterwards, then engine_start().
 */
esp_err_t engine_init(void);

/**
 * @brief Start the engine evaluation loop in the calling task context.
 *        Call this from task_control.  Does NOT create a new task.
 */
void engine_eval_loop(void);

/** Clean up (free NVS handles, clear state). */
void engine_deinit(void);

/* ═══════════════════════════════════════════════════════════════════════
 *  Serialization  (for MQTT / BLE Mesh transport)
 * ═══════════════════════════════════════════════════════════════════════ */

/** Serialize a single rule to a JSON string. */
esp_err_t engine_rule_to_json(const engine_rule_t *rule,
                               char *buf, size_t len);

/** Deserialize a JSON string into a rule struct (does NOT register it). */
esp_err_t engine_rule_from_json(engine_rule_t *rule,
                                 const char *json, size_t json_len);

/** Export all sources as a JSON array. */
esp_err_t engine_sources_to_json(char *buf, size_t len);

/** Export all targets as a JSON array. */
esp_err_t engine_targets_to_json(char *buf, size_t len);

/** Export all params as a JSON array. */
esp_err_t engine_params_to_json(char *buf, size_t len);

/** Export everything (sources + targets + params + rules) in one JSON. */
esp_err_t engine_export_all_json(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
