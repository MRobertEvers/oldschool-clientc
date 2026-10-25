/*
 * The op lists the entity-info executor reads, as text: one line per op, the
 * kind by its RSPROT_OP_* name and every field in a fixed order, so two
 * readings of a recording diff line for line. The STOP a reader ends on is
 * not the executor's and prints nothing. Test-only.
 */
#ifndef INFO_OP_DUMP_H
#define INFO_OP_DUMP_H

#include "streams/rsprot_info.h"

#include <stdio.h>

static void
dump_hex(FILE* f, uint8_t const* p, int n)
{
    fprintf(f, " %d:", n);
    for( int i = 0; i < n; i++ )
        fprintf(f, "%02x", p ? p[i] : 0);
}

/** `npc`: the npc stream's list, whose FACE_ANGLE carries `spawn`. */
static void
dump_info_ops(FILE* f, RsprotInfoOp const* ops, int n, int npc)
{
    for( int i = 0; i < n; i++ )
    {
        RsprotInfoOp const* o = &ops[i];
        unsigned long long v = (unsigned long long)o->value;

        switch( o->kind )
        {
        case RSPROT_OP_STOP: continue;
        case RSPROT_OP_SET_LOCAL_PLAYER: fprintf(f, "SET_LOCAL_PLAYER"); break;
        case RSPROT_OP_ADD_NEW: fprintf(f, "ADD_NEW %llu", v); break;
        case RSPROT_OP_ADD_OLD: fprintf(f, "ADD_OLD %llu", v); break;
        case RSPROT_OP_SET_EXT: fprintf(f, "SET_EXT %llu", v); break;
        case RSPROT_OP_CLEAR_OLD: fprintf(f, "CLEAR_OLD %llu", v); break;
        case RSPROT_OP_REMOVE_PID: fprintf(f, "REMOVE_PID %llu", v); break;
        case RSPROT_OP_COUNT_RESET: fprintf(f, "COUNT_RESET %llu", v); break;
        case RSPROT_OP_INFO: fprintf(f, "INFO %llu", v); break;
        case RSPROT_OP_WALKDIR: fprintf(f, "WALKDIR %llu", v); break;
        case RSPROT_OP_RUNDIR: fprintf(f, "RUNDIR %llu", v); break;
        case RSPROT_OP_CRAWLDIR: fprintf(f, "CRAWLDIR %llu", v); break;
        case RSPROT_OP_NPCTYPE: fprintf(f, "NPCTYPE %llu", v); break;
        case RSPROT_OP_SPAWN_CYCLE: fprintf(f, "SPAWN_CYCLE %llu", v); break;
        case RSPROT_OP_VISIBLE_OPS: fprintf(f, "VISIBLE_OPS %llu", v); break;
        case RSPROT_OP_LEVEL_CHANGE: fprintf(f, "LEVEL_CHANGE %llu", v); break;
        case RSPROT_OP_LOCAL_XZLEVEL:
        case RSPROT_OP_ABS_XZLEVEL:
            fprintf(f, "%s %d %d %d %d %d %d %d",
                    o->kind == RSPROT_OP_ABS_XZLEVEL ? "ABS_XZLEVEL" : "LOCAL_XZLEVEL",
                    o->xz_level.x, o->xz_level.z, o->xz_level.level, o->xz_level.jump,
                    o->xz_level.has_move_speed, o->xz_level.move_speed, o->xz_level.route_only);
            break;
        case RSPROT_OP_DELTA_XZ:
            fprintf(f, "DELTA_XZ %d %d %d", o->delta_xz.dx, o->delta_xz.dz, o->delta_xz.jump);
            break;
        case RSPROT_OP_APPEARANCE:
            fprintf(f, "APPEARANCE");
            dump_hex(f, o->appearance.appearance, o->appearance.len);
            break;
        case RSPROT_OP_SEQUENCE:
            fprintf(f, "SEQUENCE %d %d", o->sequence.sequence_id, o->sequence.delay);
            break;
        case RSPROT_OP_FACE_ENTITY:
            fprintf(f, "FACE_ENTITY %d %d %d %d %d %d", o->face_entity.entity_id,
                    o->face_entity.fallback_angle, o->face_entity.has_fallback_angle,
                    o->face_entity.instant, o->face_entity.movement_mode, o->face_entity.modern);
            break;
        case RSPROT_OP_SAY: fprintf(f, "SAY \"%s\"", o->say.text ? o->say.text : ""); break;
        case RSPROT_OP_DAMAGE:
        case RSPROT_OP_DAMAGE2:
            fprintf(f, "%s %d %d %d %d %d %d", o->kind == RSPROT_OP_DAMAGE ? "DAMAGE" : "DAMAGE2",
                    o->damage.damage_type, o->damage.damage, o->damage.health,
                    o->damage.total_health, o->damage.delay, o->damage.slots);
            break;
        case RSPROT_OP_HEADBAR:
            fprintf(f, "HEADBAR %d %d %d %d %d %d", o->headbar.type, o->headbar.duration,
                    o->headbar.start_delay, o->headbar.start_fill, o->headbar.end_fill,
                    o->headbar.remove);
            break;
        case RSPROT_OP_FACE_COORD:
            fprintf(f, "FACE_COORD %d %d %d %d %d", o->face_coord.x, o->face_coord.z,
                    o->face_coord.instant, o->face_coord.movement_mode, o->face_coord.modern);
            break;
        case RSPROT_OP_CHAT:
            fprintf(f, "CHAT %d %d", o->chat.colour_effect, o->chat.type);
            dump_hex(f, o->chat.data, o->chat.length);
            break;
        case RSPROT_OP_CHANGE_TYPE: fprintf(f, "CHANGE_TYPE %d", o->change_type.npc_type); break;
        case RSPROT_OP_SPOTANIM:
            fprintf(f, "SPOTANIM %d %d %d %d", o->spotanim.slot, o->spotanim.spotanim_id,
                    o->spotanim.height_delay, o->spotanim.loop);
            break;
        case RSPROT_OP_EXACT_MOVE:
            fprintf(f, "EXACT_MOVE %d %d %d %d %d %d %d %d %d", o->exact_move.start_x,
                    o->exact_move.start_z, o->exact_move.end_x, o->exact_move.end_z,
                    o->exact_move.start_cycle_delta, o->exact_move.end_cycle_delta,
                    o->exact_move.facing, o->exact_move.facing_is_yaw, o->exact_move.relative);
            break;
        case RSPROT_OP_FACE_ANGLE:
            fprintf(f, "FACE_ANGLE %d %d %d %d %d", o->face_angle.angle, o->face_angle.instant,
                    npc ? o->face_angle.spawn : 0, o->face_angle.movement_mode,
                    o->face_angle.modern);
            break;
        case RSPROT_OP_NAME_CHANGE:
            fprintf(f, "NAME_CHANGE \"%s\"", o->name_change.name ? o->name_change.name : "");
            break;
        case RSPROT_OP_BAS_CHANGE:
        {
            RsprotInfoBasChange const* b = &o->bas_change;

            fprintf(f, "BAS_CHANGE %u %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", b->mask,
                    b->turnanim, b->turnanim_r, b->walkanim, b->walkanim_b, b->walkanim_l,
                    b->walkanim_r, b->runanim, b->runanim_b, b->runanim_l, b->runanim_r,
                    b->crawlanim, b->crawlanim_b, b->crawlanim_l, b->crawlanim_r, b->readyanim);
            break;
        }
        case RSPROT_OP_HEADICON_CUSTOM:
            fprintf(f, "HEADICON_CUSTOM %d", o->headicon.mask);
            for( int k = 0; k < 8; k++ )
                fprintf(f, " %d/%d", o->headicon.group[k], o->headicon.index[k]);
            break;
        case RSPROT_OP_BODY_CUSTOM:
            fprintf(f, "BODY_CUSTOM %d %d", o->body.reset, o->body.model_count);
            for( int k = 0; k < o->body.model_count; k++ )
                fprintf(f, " %d", o->body.models[k]);
            break;
        case RSPROT_OP_TINT:
            fprintf(f, "TINT %d %d %d %d %d %d", o->tint.start_cycle_delta, o->tint.end_cycle_delta,
                    o->tint.hue, o->tint.saturation, o->tint.lightness, o->tint.weight);
            break;
        case RSPROT_OP_TRANSPARENCY:
            fprintf(f, "TRANSPARENCY %d %d %d %d %d", o->transparency.start_cycle_delta,
                    o->transparency.end_cycle_delta, o->transparency.from, o->transparency.to,
                    o->transparency.use_start);
            break;
        default: fprintf(f, "KIND%d", o->kind); break;
        }
        fprintf(f, "\n");
    }
}

#endif
