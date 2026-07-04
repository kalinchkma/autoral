/**
 * @file engine_types.h
 * @brief Generic rules engine data types.
 *
 * The engine knows only three primitives:
 *   - Source: provides a float value (sensor, computed, etc.)
 *   - Target: receives a float value (GPIO, PWM, etc.)
 *   - Param:  read+write float (threshold, mode, etc.)
 *
 * Rules combine conditions on sources/params with actions on targets/params.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* ── Limits (override in app_config.h before including) ──────────────── */
#ifndef ENGINE_MAX_SOURCES
#define ENGINE_MAX_SOURCES      16
#endif
#ifndef ENGINE_MAX_TARGETS
#define ENGINE_MAX_TARGETS      16
#endif
#ifndef ENGINE_MAX_PARAMS
#define ENGINE_MAX_PARAMS       8
#endif
#ifndef ENGINE_MAX_RULES
#define ENGINE_MAX_RULES        16
#endif
#ifndef ENGINE_MAX_CONDITIONS
#define ENGINE_MAX_CONDITIONS   8
#endif
#ifndef ENGINE_MAX_ACTIONS
#define ENGINE_MAX_ACTIONS      4
#endif
#ifndef ENGINE_NAME_LEN
#define ENGINE_NAME_LEN         16
#endif
#ifndef ENGINE_RULE_NAME_LEN
#define ENGINE_RULE_NAME_LEN    32
#endif

/* ── Source ──────────────────────────────────────────────────────────── */

/** Callback that returns the current value of a source. */
typedef float (*engine_source_read_fn_t)(void *user_data);

typedef struct {
    uint8_t                 id;
    char                    name[ENGINE_NAME_LEN];
    engine_source_read_fn_t read;        /**< Driver-provided read callback */
    void                   *user_data;   /**< Opaque driver context */
    float                   cached;      /**< Last-read value */
    uint32_t                last_read_ms;
} engine_source_t;

/* ── Target ──────────────────────────────────────────────────────────── */

/** Callback that writes a value to a target. */
typedef esp_err_t (*engine_target_write_fn_t)(float value, void *user_data);

typedef struct {
    uint8_t                  id;
    char                     name[ENGINE_NAME_LEN];
    engine_target_write_fn_t write;      /**< Driver-provided write callback */
    void                    *user_data;  /**< Opaque driver context */
    float                    last_value;
} engine_target_t;

/* ── Param (source + target, persisted to NVS) ──────────────────────── */

typedef struct {
    uint8_t id;
    char    name[ENGINE_NAME_LEN];
    float   value;
    float   min_value;
    float   max_value;
    float   default_value;
} engine_param_t;

/* ── Condition ───────────────────────────────────────────────────────── */

typedef enum {
    ENGINE_COND_SOURCE,   /**< Read from a registered source */
    ENGINE_COND_PARAM,    /**< Read from a registered param  */
} engine_cond_from_t;

typedef enum {
    ENGINE_OP_EQUAL,
    ENGINE_OP_NOT_EQUAL,
    ENGINE_OP_GT,
    ENGINE_OP_LT,
    ENGINE_OP_GTE,
    ENGINE_OP_LTE,
} engine_op_t;

typedef enum {
    ENGINE_LOGIC_AND,
    ENGINE_LOGIC_OR,
} engine_logic_t;

typedef struct {
    engine_cond_from_t from;    /**< Source or param? */
    uint8_t            src_id;  /**< Which source / param by ID */
    engine_op_t        op;      /**< Comparison operator */
    float              value;   /**< Literal to compare against */
} engine_condition_t;

/* ── Action ──────────────────────────────────────────────────────────── */

typedef enum {
    ENGINE_ACTION_SET_TARGET,   /**< Write value to a registered target */
    ENGINE_ACTION_SET_PARAM,    /**< Write value to a registered param  */
} engine_action_type_t;

typedef struct {
    engine_action_type_t type;   /**< Target or param? */
    uint8_t              tgt_id; /**< Which target / param by ID */
    float                value;  /**< Value to write */
} engine_action_t;

/* ── Rule trigger types ──────────────────────────────────────────────── */

typedef enum {
    ENGINE_RULE_CONTINUOUS,    /**< Triggers every eval cycle while true          */
    ENGINE_RULE_ONE_SHOT,      /**< Triggers once, then auto-disables             */
    ENGINE_RULE_EDGE_RISING,   /**< Triggers only on false → true transition      */
    ENGINE_RULE_EDGE_FALLING,  /**< Triggers only on true → false transition      */
    ENGINE_RULE_DEBOUNCED,     /**< Must be true for N consecutive evals          */
    ENGINE_RULE_COOLDOWN,      /**< After trigger, ignores for N evals            */
} engine_rule_type_t;

/* ── Rule ────────────────────────────────────────────────────────────── */

typedef struct {
    uint8_t                 id;
    char                    name[ENGINE_RULE_NAME_LEN];
    bool                    enabled;
    engine_rule_type_t      type;

    /* Conditions */
    uint8_t                 num_conditions;
    engine_condition_t      conditions[ENGINE_MAX_CONDITIONS];
    engine_logic_t          logic[ENGINE_MAX_CONDITIONS - 1];

    /* Actions */
    uint8_t                 num_actions;
    engine_action_t         actions[ENGINE_MAX_ACTIONS];

    /* ── Runtime state (not serialized to NVS) ── */
    bool                    last_result;     /**< Previous eval result (edge detect) */
    uint16_t                debounce_count;  /**< Current debounce counter          */
    uint16_t                cooldown_count;  /**< Current cooldown counter          */
    uint16_t                debounce_limit;  /**< Threshold for debounced rules     */
    uint16_t                cooldown_limit;  /**< Duration for cooldown rules       */
} engine_rule_t;
