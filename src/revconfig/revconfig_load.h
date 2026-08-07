#ifndef REVCONFIG_LOAD_H
#define REVCONFIG_LOAD_H

#include "revconfig.h"

#include <stdint.h>

void
revconfig_load_fields_from_ini(
    const char* filename,
    struct RevConfigBuffer* revconfig_buffer);

void
revconfig_load_fields_from_ini_bytes(
    const uint8_t* data,
    uint32_t size,
    struct RevConfigBuffer* revconfig_buffer);

/**
 * Read only the sections whose header carries `section_prefix`, e.g. a boot
 * manifest's `[revconfig:component:world]` under prefix "revconfig". The prefix
 * and its ':' are stripped, so what the field stream sees is an ordinary
 * `[component:world]` — the two INI dialects can therefore share one file
 * without either parser guessing at the other's keys.
 *
 * Sections without the prefix are dropped whole: no ITEMTYPE, no keyvals, no
 * ITEMDONE. That is the point — a manifest's `[ui:gameframe]` uses free-form
 * keys, some of which (`left`, `top`, …) collide with layout fields.
 *
 * NULL/"" prefix is the unprefixed dialect, i.e. exactly
 * revconfig_load_fields_from_ini().
 */
void
revconfig_load_fields_from_ini_prefixed(
    const char* filename,
    const char* section_prefix,
    struct RevConfigBuffer* revconfig_buffer);

void
revconfig_load_fields_from_ini_bytes_prefixed(
    const uint8_t* data,
    uint32_t size,
    const char* section_prefix,
    struct RevConfigBuffer* revconfig_buffer);

/** True when `filename` holds at least one `[<section_prefix>:…]` section. */
int
revconfig_ini_has_prefixed_sections(
    const char* filename,
    const char* section_prefix);

#endif