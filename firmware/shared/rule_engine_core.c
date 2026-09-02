/**
 * @file rule_engine_core.c
 * @brief Rule engine core — generic evaluation with vtable dispatch.
 *
 * Architecture ref: Section 6.
 *
 * This file contains zero domain-specific logic. All domain knowledge
 * comes through the DomainProfileVTable_t interface.
 */

#include "rule_engine_core.h"
#include <string.h>

/* ---------- Internal state ---------- */

static bool g_initialised = false;

/* ---------- Public API ---------- */

bool rule_engine_init(void)
{
    g_initialised = true;
    return true;
}

uint8_t rule_engine_evaluate(
    const DomainProfileVTable_t *vtable,
    const SensorReading_t *reading,
    RuleEvaluationResult_t *results,
    uint8_t max_results)
{
    if (!vtable || !reading || !results || max_results == 0)
        return 0;
    if (!g_initialised)
        return 0;

    const RuleEntry_t *rules = NULL;
    uint8_t rule_count = 0;
    vtable->getRuleTable(&rules, &rule_count);

    if (!rules || rule_count == 0)
        return 0;

    uint8_t triggered_count = 0;

    for (uint8_t i = 0; i < rule_count && triggered_count < max_results; i++) {
        const RuleEntry_t *rule = &rules[i];

        /* Only evaluate rules that match this metric. */
        if (rule->metric_id != reading->metric_id)
            continue;

        bool triggered = false;

        /* Determine comparison direction. */
        if (rule->comparison_type == RULE_COMPARE_BELOW) {
            triggered = (reading->value < rule->threshold);
        } else {
            triggered = (reading->value >= rule->threshold);
        }

        if (triggered) {
            RuleEvaluationResult_t *result = &results[triggered_count];
            result->rule_id = rule->rule_id;
            result->triggered = true;
            result->action_type = rule->action_type;
            result->actuator_id = rule->actuator_id;

            /* Determine commanded state from the reading vs threshold. */
            if (reading->value >= rule->threshold) {
                result->actuator_state = (uint8_t)ACTUATOR_STATE_ON;
            } else {
                result->actuator_state = (uint8_t)ACTUATOR_STATE_OFF;
            }

            triggered_count++;
        }
    }

    return triggered_count;
}

const RuleEntry_t *rule_engine_find_rule(
    const DomainProfileVTable_t *vtable,
    uint16_t rule_id)
{
    if (!vtable) return NULL;
    if (!g_initialised) return NULL;

    const RuleEntry_t *rules = NULL;
    uint8_t rule_count = 0;
    vtable->getRuleTable(&rules, &rule_count);

    if (!rules) return NULL;

    for (uint8_t i = 0; i < rule_count; i++) {
        if (rules[i].rule_id == rule_id) {
            return &rules[i];
        }
    }

    return NULL;
}
