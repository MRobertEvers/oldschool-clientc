#!/usr/bin/env python3
"""Break temporary production sources; reuse the existing UI/CS2 test targets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess


def run_lua_controls(src, out, make_args, selected):
    make=["make","--no-print-directory",*make_args,"test-plugin-lua"]
    with (out/"positive.log").open("w") as log:
        subprocess.run(make,cwd=src,stdout=log,stderr=subprocess.STDOUT,check=True)
    recipe=subprocess.check_output([*make,"-n"],cwd=src,text=True).replace("\\\n"," ")
    command=next(shlex.split(line) for line in recipe.splitlines()
        if "./" in line and "/torirs_plugin_lua_v2_test" in line and " -o " not in line)
    command=[str((src/arg).resolve()) if arg.startswith("./") else arg for arg in command]
    # Each mutant gets a private script fixture. Production sources and objects
    # remain untouched, and the existing runtime test binary runs all products.
    controls={
        "role_binding":("_roleprobe.lua",[("if event.kind == \"bound\" then apply_pending(api) end", "if false then apply_pending(api) end")],
            "late native binding must execute the pending probe action"),
        "fps_counter":("performance_display.lua",[("sample_frames = ev.drawn_frames - sample_drawn_at_start",
            "sample_frames = sample_frames + 1")],"FPS must count rendered frames"),
        "work_window":("performance_display.lua",[("recent_total = recent_total - (recent[slot] or 0)",
            "recent_total = recent_total")],"frame time must exclude pacing sleep"),
        "metric_visibility":("performance_display.lua",[("api.config[metric.visible] and wanted[metric.key] or \"\"",
            "wanted[metric.key]")],"disabled metrics must disappear"),
        "hover_order":("tile_indicator.lua",[("function plugin.on_draw_world(api, draw)",
            "function plugin.on_draw_world(api, draw)\n  plugin_draw_player(api, draw)"),
            ("  plugin_draw_player(api, draw)\nend","end")],"hover uses picked level and draws first"),
        "entity_slot":("entity_highlighter.lua",[("local id = math.floor(sel.tag / TAG_OPS)",
            "local id = api.world.npc_by_slot(7).base_npc_id")],
            "retained Tag must not retarget a recycled NPC slot"),
        "entity_intent":("entity_highlighter.lua",[
            ("if sel.tag % TAG_OPS == OP_TAG then tag_add(api, id) else tag_remove(api, id) end",
             "if not tagged[id] then tag_add(api, id) else tag_remove(api, id) end")],
            "retained Tag preserves its intended operation"),
        # The three the Porcelain port added. Each one is a refusal this plugin
        # used to drop: a reveal key the lane cannot answer, a joined tag list
        # past the store's ceiling, and the hover pass it must leave at once.
        "entity_reveal":("entity_highlighter.lua",[
            ("if not reveal_armed or not reveal_down then return end","if false then return end")],
            "the rows are not offered while the reveal key is up"),
        "entity_ceiling":("entity_highlighter.lua",[
            ('if not api.porcelain.config_list_add("tags", tostring(id)) then return end',
             'api.porcelain.config_list_add("tags", tostring(id))')],
            "a value past the ceiling must never reach the store"),
        "entity_hover":("entity_highlighter.lua",[("if menu.hover_pass then return end",
            "if false then return end")],"the hover pass is left on the first statement"),
        # The one nothing in the host does for a plugin. porcelain.note_key had no
        # caller in the tree outside the layer's own unit test, so this plugin's
        # reveal key could never go down and Tag/Untag were unreachable on EVERY
        # lane -- not just the touch one its header worries about.
        "entity_key_forward":("entity_highlighter.lua",[
            ("    api.porcelain.note_key(event.key, event.down)\n","")],
            "the reveal key edge is moved by the plugin forwarding its own on_key"),
        # The raw verb the layer replaced: menu.add answers false when the
        # host-wide route table is full, and this plugin dropped that bool.
        "entity_menu_verb":("entity_highlighter.lua",[("api.porcelain.menu_add(",
            "api.menu.add(")],
            "a Porcelain plugin adds rows through the layer, which reports a refusal"),
        "ground_origin":("ground_items.lua",[("local base_x, base_z = api.world.scene_origin()",
            "local base_x, base_z = nil, nil")],"mid-session enable while moving must immediately draw ground labels"),
        # The Porcelain port moved the retained-row encoding into the library.
        # Spelling it in the plugin again -- which is exactly what the two
        # shipped plugins each did, with two DIFFERENT constants -- retargets
        # every retained row the moment the library's number moves.
        "ground_intent":("ground_items.lua",[("local id, op = api.porcelain.menu_untag(sel.tag)",
            "local id, op = sel.tag // 4, sel.tag % 4")],
            "retained Highlight survives despawn and remains idempotent"),
        "ground_exceptions":("ground_items.lua",[("local covered = matches(compile_entries(rest), name)",
            "local covered = false")],"Unhide one item must preserve wildcard rules and unrelated data"),
        # Eight more the port added, one per refusal or rule it now owns.
        "ground_latch":("ground_items.lua",[("    api.porcelain.note_script()\n","")],
            "the first caption callback hands every suppressed native back"),
        "ground_reveal":("ground_items.lua",[("if not reveal_armed or not reveal_down then return end",
            "if false then return end")],"the rows are not offered while the reveal key is up"),
        "ground_tier_zero":("ground_items.lua",[("    return api.porcelain.tier(tiers, price)",
            "    if price > tiers.insane then return 4 end\n"
            "    if price > tiers.high then return 3 end\n"
            "    if price > tiers.medium then return 2 end\n"
            "    if price > tiers.low then return 1 end\n    return 0")],
            "a threshold at or below zero disables its tier, it does not match everything"),
        "ground_ceiling":("ground_items.lua",[("        if not stored then return \"consume\" end\n","")],
            "and leaves the OTHER list it would have edited unchanged too"),
        "ground_name_lookup":("ground_items.lua",[("    local info = api.game.item_info(obj_id)",
            "    local info = nil")],
            "one Highlight row and one Hide row per distinct ground obj"),
        "ground_native_gate":("ground_items.lua",[
            ('if api.porcelain.require("cs2_scripts", "native captions") then',"if true then")],
            "no CS2 means no latch and nothing suppressed"),
        # expect_unsupported does NOT relabel a finding already recorded, so a
        # declaration that arrives after the call it excuses is no declaration
        # at all -- and the clean gate is unsatisfiable on every CS1 lane.
        "ground_declaration_order":("ground_items.lua",[
            ('    api.porcelain.expect_unsupported("native captions",\n'
             '        "a CS1 lane runs no CS2, so the cache\'s own ground-item caption script does not exist")\n',""),
            ('        api.porcelain.native_overlay("ground_item_labels", "groundItemCaption", on_caption)\n'
             '    end\n',
             '        api.porcelain.native_overlay("ground_item_labels", "groundItemCaption", on_caption)\n'
             '    end\n'
             '    api.porcelain.expect_unsupported("native captions",\n'
             '        "a CS1 lane runs no CS2, so the cache\'s own ground-item caption script does not exist")\n')],
            "the CS1 lane limitation is one finding, declared before it was raised"),
        "ground_under_value":("ground_items.lua",[
            ("if under > 0 and exchange < under and alch < under",
             "if under > 0 and alch < under")],
            'one price over the threshold is not "under value"'),
        "ground_pool_stale":("ground_items.lua",[
            ("        for at = #tile.items, tile.count + 1, -1 do tile.items[at] = nil end\n","")],
            "a tile that loses a line draws one line, not one left over from last frame"),
        "beam_tick":("_beamprobe.lua",[("function plugin.on_logic_tick(api)",
            "function plugin.on_server_tick(api)")],"beam creation must use the common logic tick"),
        # The plugin no longer hides the native button: it asks for a REPLACE
        # placement and the layer drops the target's paint and input. So the
        # control that used to break the hide now breaks the placement KIND,
        # which is the same property said in the vocabulary that owns it now.
        "screenshot_report_hide":("screenshot.lua",[("{ kind = \"replace\", on = \"report_button\" }",
            "{ kind = \"inside\", on = \"report_button\" }")],"the small camera stands in place of the report button through a REPLACE placement"),
        # The corner arithmetic is the layer's now; what the plugin still owns
        # is the inset it asks for. Zeroing it puts the camera hard against the
        # viewport edge, which is what the margin exists to prevent.
        "screenshot_corner_margin":("screenshot.lua",[("local MARGIN = 6",
            "local MARGIN = 0")],"bottom-right corner keeps the margin"),
        "screenshot_delay":("screenshot.lua",[("shot.ticks_left = shot.ticks_left - 1",
            "shot.ticks_left = shot.ticks_left")],"level up captures after delay_ticks into its category folder"),
        "probe_labels":("_gicount.lua",[("local report = frames % 300 == 0",
            "local report = frames % 300 == 0\n    if not report then return end")],
            "ground-count probe labels must render between log intervals"),
    }
    if selected and set(selected)-controls.keys(): raise ValueError("unknown Lua control")
    receipts=[]
    for name,(file,edits,expected) in controls.items():
        if selected and name not in selected: continue
        fixture=out/name
        shutil.copytree(src.parent/"script",fixture/"script")
        (fixture/"src/plugin").mkdir(parents=True)
        (fixture/"src/plugin/test").symlink_to(src/"plugin/test",target_is_directory=True)
        target=fixture/"script/plugins"/file
        original=target.read_text();mutant=original
        for before,after in edits:
            if mutant.count(before)!=1: raise RuntimeError(f"{name}: mechanism changed")
            mutant=mutant.replace(before,after)
        target.write_text(mutant)
        run=subprocess.run(command,cwd=fixture/"src",text=True,stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,timeout=30)
        (out/(name+".log")).write_text(run.stdout)
        valid=run.returncode==1 and expected in run.stdout
        receipts.append(dict(name=name,source_sha256=hashlib.sha256(original.encode()).hexdigest(),
            edits=edits,expected=expected,exit=run.returncode,observed=valid))
        (out/"receipt.json").write_text(json.dumps(receipts,indent=2)+"\n")
        if not valid: raise RuntimeError(f"{name}: intended failing assertion was not observed")
        print(f"{name}: observed expected failing assertion",flush=True)


def run_cs2_controls(src, out, make_args, selected):
    make = ["make", "--no-print-directory", *make_args]
    with (out / "positive.log").open("w") as log:
        subprocess.run([*make, "test-cs2-transmit-pump"], cwd=src,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    recipe = subprocess.check_output([*make, "-n", "test-cs2-transmit-pump"],
                                     cwd=src, text=True).replace("\\\n", " ")
    link = next(shlex.split(line) for line in recipe.splitlines()
                if "game/test/rs_cs2_transmit_pump_test.c " in line and " -o " in line)
    compile_commands = {}
    snapshot = "this same pass. */\n        if( UITree_ResolveRef(self->host->tree, hook->ref) < 0 )"
    registry = ("if( UITree_ResolveRef(self->host->tree, hook->ref) < 0 )\n"
                "        {\n            hook->last_seen_serial = self->host->inv_change_serial;")
    controls = {
        "resume_context": ("if( self->has_resume_context )", "if( false )",
                           "resumed callback respects widget incarnation dot=0 stale=1"),
        "dispatch_compaction": (
            "if( hint < count && task_cs2_refs_equal(ref, task_cs2_hook_ref(hooks, stride, hint)) )",
            "if( hint < count )", "compaction cannot skip original listener channel=0"),
        "queued_origin": ("(!self->started &&", "(false &&",
                          "queued CS2 callback cannot write recycled native ID"),
        "snapshot_identity": (snapshot, snapshot.replace(
            "UITree_ResolveRef(self->host->tree, hook->ref)",
            "UITree_FindByComponentId(self->host->tree, hook->component_id)"),
            "snapshot callback 0 cannot reach replacement"),
        "registry_identity": (registry, registry.replace(
            "UITree_ResolveRef(self->host->tree, hook->ref)",
            "UITree_FindByComponentId(self->host->tree, hook->component_id)"),
            "registry callback 0 cannot reach replacement"),
    }
    controls = {name: ("game/task_cs2_run.c", *control) for name, control in controls.items()}
    controls.update({
        "widget_pose": ("engine/uitree_anim.c", "if( !cache ) return source;", "if( true ) return source;",
                        "model widgets own independent animation poses"),
        "model_resource_revision": ("engine/uitree_anim.c",
            "if( !pose || pose->source_revision != revision )", "if( !pose )",
            "resource replacement refreshes the private pose"),
        "skeletal_widget": ("engine/uitree_anim.c",
            "!anim || (!anim->base && !anim->skeletal) || anim->frame_count <= 0",
            "!anim || !anim->base || anim->frame_count <= 0", "skeletal widget clock advances"),
        "active_animation": ("ui/uitree_emit.c",
            "out->model_anim_seq = active ? component->u.rs_model.active_anim_seq_id : component->u.rs_model.anim_seq_id;",
            "out->model_anim_seq = component->u.rs_model.anim_seq_id;",
            "cache active animation reaches native rendering dat2=0"),
    })
    if selected and set(selected) - controls.keys():
        raise ValueError("unknown CS2 control")
    for name, (source, before, after, expected) in controls.items():
        if selected and name not in selected:
            continue
        if source not in compile_commands:
            obj = next(arg for arg in link if arg.endswith("/" + Path(source).stem + ".o"))
            recipe = subprocess.check_output([*make, "-n", "-W", source, obj],
                                             cwd=src, text=True).replace("\\\n", " ")
            compile_cmd = next(shlex.split(line) for line in recipe.splitlines()
                               if f" -c {source} " in line)
            compile_commands[source] = (obj, compile_cmd)
        obj, compile_cmd = compile_commands[source]
        original = (src / source).read_text()
        if original.count(before) != 1:
            raise RuntimeError(f"{name}: mechanism changed; update explicit mutation")
        mutant = out / f"{name}.c"
        mutant.write_text(original.replace(before, after))
        mutant_obj = out / f"{name}.o"
        binary = out / name
        compile_mutant = [str(mutant) if arg == source else arg for arg in compile_cmd]
        compile_mutant[compile_mutant.index("-o") + 1] = str(mutant_obj)
        compile_mutant.extend(["-I", str((src / source).parent)])
        link_mutant = [str(mutant_obj) if arg == obj else arg for arg in link]
        link_mutant[link_mutant.index("-o") + 1] = str(binary)
        with (out / f"{name}-build.log").open("w") as log:
            for command in (compile_mutant, link_mutant):
                subprocess.run(command, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
        run = subprocess.run([str(binary)], cwd=src, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=30)
        (out / f"{name}.log").write_text(run.stdout)
        if run.returncode != 1 or "FAIL " + expected not in run.stdout:
            raise RuntimeError(f"{name}: expected assertion was not observed")
        print(f"{name}: observed expected failing assertion", flush=True)


def run_host_controls(src, out, make_args, selected):
    make = ["make", "--no-print-directory", *make_args]
    with (out / "positive.log").open("w") as log:
        subprocess.run([*make, "test-plugin-host"], cwd=src,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    recipe = subprocess.check_output([*make, "-n", "test-plugin-host"], cwd=src, text=True).replace("\\\n", " ")
    command = next(shlex.split(line) for line in recipe.splitlines()
                   if "plugin/test/torirs_plugin_host_test.c " in line and " -o " in line)
    source = "plugin/torirs_plugin_host.c"
    original = (src / source).read_text()
    controls = {
        "watch_epoch": ("ctx->widget_watches[slot].serial != serial", "!ctx->widget_watches[slot].serial",
                        "restarted subscription cannot join the old dispatch snapshot"),
        "watch_change": ("if( memcmp(&previous, &current, sizeof(current)) == 0 ) continue;", "if( false ) continue;",
                         "unrelated topology changes do not replay a stable binding"),
        "tree_watch_epoch": ("ctx->widget_watches[slot].serial != serial", "!ctx->widget_watches[slot].serial",
                         "restarted tree subscription cannot join an old dispatch"),
        "tree_watch_publish": ("if( strcmp(role,\"@tree\")==0 )\n        {", "if( strcmp(role,\"@tree\")==0 )\n        {\n            continue;",
                         "each tree subscription receives one initial notification"),
        "script_dispatch_epoch": ("ctx->lifecycle!=snapshot[i].lifecycle ||", "false ||",
                         "restarted plugin cannot join an active script callback dispatch"),
    }
    if selected and set(selected) - controls.keys():
        raise ValueError("unknown host control")
    for name, (before, after, expected) in controls.items():
        if selected and name not in selected:
            continue
        if original.count(before) != 1:
            raise RuntimeError(f"{name}: mechanism changed; update explicit mutation")
        mutant = out / f"{name}.c"
        mutant.write_text(original.replace(before, after))
        binary = out / name
        build = [str(mutant) if arg == source else arg for arg in command]
        build[build.index("-o") + 1] = str(binary)
        with (out / f"{name}-build.log").open("w") as log:
            subprocess.run(build, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
        run = subprocess.run([str(binary)], cwd=src, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=30)
        (out / f"{name}.log").write_text(run.stdout)
        if run.returncode != 1 or expected not in run.stdout:
            raise RuntimeError(f"{name}: expected assertion was not observed")
        print(f"{name}: observed expected failing assertion", flush=True)


def run_product_controls(src, out, make_args, selected):
    make = ["make", "--no-print-directory", *make_args, "test-nxt-plugins"]
    with (out / "positive.log").open("w") as log:
        subprocess.run(make, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
    recipe = subprocess.check_output([*make, "-n"], cwd=src, text=True).replace("\\\n", " ")
    command = next(shlex.split(line) for line in recipe.splitlines()
                   if "plugin/test/nxt_activities_test.c " in line and " -o " in line)
    source = "plugin/plugins/nxt_cannon_ammo.c"
    original = (src / source).read_text()
    controls = {
        "cannon_coordinate": ("state->last_coord != coord || previous < 0",
                              "!state->last_coord || previous < 0",
                              "replacement cannon does not inherit the previous ammo edge"),
        "cannon_null": ("if( coord <= 0 )", "if( !coord )",
                        "native null coordinate has no ammo events"),
    }
    if selected and set(selected) - controls.keys(): raise ValueError("unknown product control")
    for name, (before, after, expected) in controls.items():
        if selected and name not in selected: continue
        if original.count(before) != 1: raise RuntimeError(f"{name}: mechanism changed")
        mutant = out / (name + ".c")
        mutant.write_text(original.replace(before, after))
        binary = out / name
        build = [str(mutant) if arg == source else arg for arg in command]
        build[build.index("-o") + 1] = str(binary)
        with (out / (name + "-build.log")).open("w") as log:
            subprocess.run(build, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
        run = subprocess.run([str(binary)], cwd=src, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=30)
        (out / (name + ".log")).write_text(run.stdout)
        if run.returncode != 1 or expected not in run.stdout:
            raise RuntimeError(f"{name}: intended failing assertion was not observed")
        print(f"{name}: observed expected failing assertion", flush=True)


def run_orbs_controls(src, out, make_args, selected):
    """Mutate the ported minimap-orbs plugin; its widget-API unit test must fail."""
    make = ["make", "--no-print-directory", *make_args, "test-minimap-orbs-v2"]
    with (out / "positive.log").open("w") as log:
        subprocess.run(make, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
    recipe = subprocess.check_output([*make, "-n"], cwd=src, text=True).replace("\\\n", " ")
    command = next(shlex.split(line) for line in recipe.splitlines()
                   if "plugin/test/minimap_orbs_v2_test.c" in line and " -o " in line)
    source = "plugin/plugins/minimap_orbs.c"
    original = (src / source).read_text()
    controls = {
        "orbs_anchor_change": ("if( orb->control.opaque[2] && !ToriRS_WidgetRefEqual(orb->parent, parent) )\n            orbs_remove_control(api, orb);",
                               "", "owned_under(W_MAP_PARENT, \"orb_run\") < 0"),
        "orbs_disc_clamp": ("    if( x + ORB_W > limit )\n        x = limit - ORB_W;", "",
                            "!orb_covers_map(&widgets[W_MINIMAP], &widgets[i])"),
        "orbs_native_first": ("    if( orbs_native_action(api, orb, &action) )\n    {\n        enum ToriRS_ContractResult result = ui->invoke(ui->context, action);",
                              "    if( false && orbs_native_action(api, orb, &action) )\n    {\n        enum ToriRS_ContractResult result = ui->invoke(ui->context, action);",
                              "invoked_actions == 1"),
    }
    if selected and set(selected) - controls.keys(): raise ValueError("unknown orbs control")
    for name, (before, after, expected) in controls.items():
        if selected and name not in selected: continue
        if original.count(before) != 1: raise RuntimeError(f"{name}: mechanism changed")
        mutant = out / (name + ".c")
        mutant.write_text(original.replace(before, after))
        binary = out / name
        build = [str(mutant) if arg == source else arg for arg in command]
        build[build.index("-o") + 1] = str(binary)
        with (out / (name + "-build.log")).open("w") as log:
            subprocess.run(build, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
        run = subprocess.run([str(binary)], cwd=src, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=30)
        (out / (name + ".log")).write_text(run.stdout)
        if run.returncode != 1 or expected not in run.stdout:
            raise RuntimeError(f"{name}: intended failing assertion was not observed")
        print(f"{name}: observed expected failing assertion", flush=True)


def run_xporbs_controls(src, out, make_args, selected):
    """Mutate the ported XP drop orbs; the widget-API host test must fail."""
    make = ["make", "--no-print-directory", *make_args, "test-xp-orbs"]
    with (out / "positive.log").open("w") as log:
        subprocess.run(make, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
    recipe = subprocess.check_output([*make, "-n"], cwd=src, text=True).replace("\\\n", " ")
    command = next(shlex.split(line) for line in recipe.splitlines()
                   if "plugin/test/xp_orbs_test.c" in line and " -o " in line)
    source = "plugin/plugins/xp_orbs.c"
    original = (src / source).read_text()
    controls = {
        "xporbs_hover_hold": ("            globe->at_ms = now;\n            hovered_slot = i;", "            hovered_slot = i;",
                              "hovering holds a globe past its duration"),
        "xporbs_flip": ("    (void)api->config.set(api, \"vertical\", orb_cfg_bool(api, \"vertical\") ? \"0\" : \"1\");", "",
                        "Flip stacks them into a column"),
        "xporbs_table_frozen": ("    if( count == 0 )\n        return;\n\n    g_skill_count = count;", "    g_skill_count = count;",
                                "a table sized after login still notices the first gain"),
        "xporbs_expired_control": ("    for( int i = g_globe_count; i < ORB_MAX_SHOWN; i++ )\n    {\n        orb_remove_control(api, &state->globe_control[i]);",
                                   "    for( int i = g_globe_count; i < ORB_MAX_SHOWN; i++ )\n    {\n        (void)0;",
                                   "a globe past its duration is gone with its control"),
    }
    if selected and set(selected) - controls.keys(): raise ValueError("unknown xporbs control")
    for name, (before, after, expected) in controls.items():
        if selected and name not in selected: continue
        if original.count(before) != 1: raise RuntimeError(f"{name}: mechanism changed")
        mutant = out / (name + ".c")
        mutant.write_text(original.replace(before, after))
        binary = out / name
        build = [str(mutant) if arg == source else arg for arg in command]
        build[build.index("-o") + 1] = str(binary)
        with (out / (name + "-build.log")).open("w") as log:
            subprocess.run(build, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
        run = subprocess.run([str(binary)], cwd=src, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=60)
        (out / (name + ".log")).write_text(run.stdout)
        if run.returncode != 1 or expected not in run.stdout:
            raise RuntimeError(f"{name}: intended failing assertion was not observed")
        print(f"{name}: observed expected failing assertion", flush=True)


def run_action_controls(src, out, make_args, selected):
    make=["make","--no-print-directory",*make_args]
    with (out/"positive.log").open("w") as log:
        subprocess.run([*make,"test-minimenu-world"],cwd=src,stdout=log,stderr=subprocess.STDOUT,check=True)
    recipe=subprocess.check_output([*make,"-n","test-minimenu-world"],cwd=src,text=True).replace("\\\n"," ")
    link=next(shlex.split(line) for line in recipe.splitlines() if "game/test/rs_minimenu_world_test.c " in line and " -o " in line)
    source="game/rs_minimenu_build.c"
    obj=next(arg for arg in link if arg.endswith("/rs_minimenu_build.o"))
    recipe=subprocess.check_output([*make,"-n","-W",source,obj],cwd=src,text=True).replace("\\\n"," ")
    compile_cmd=next(shlex.split(line) for line in recipe.splitlines() if f" -c {source} " in line)
    original=(src/source).read_text()
    controls={
        "action_signature": ("return revision==RS_Minimenu_WidgetActionRevision(&menu->options[index]) ? index : -1;", "return index;",
            "native action re-resolution rejects changed target signature"),
        "action_availability": ("UITree_NodeOrAncestorDisplayHidden(ctx->tree, index) ||\n        !UITree_NodeNativeInputPresent(ctx->tree, ctx->ui_host, index)", "false",
            "native hide blocks widget actions despite plugin show"),
    }
    if selected and set(selected)-controls.keys(): raise ValueError("unknown native action control")
    for name,(before,after,expected) in controls.items():
        if selected and name not in selected: continue
        if original.count(before)!=1: raise RuntimeError(f"{name}: mechanism changed")
        mutant=out/(name+".c");mutant.write_text(original.replace(before,after))
        mutant_obj=out/(name+".o");binary=out/name
        compile_mutant=[str(mutant) if arg==source else arg for arg in compile_cmd]
        compile_mutant[compile_mutant.index("-o")+1]=str(mutant_obj)
        compile_mutant.extend(["-I",str((src/source).parent)])
        link_mutant=[str(mutant_obj) if arg==obj else arg for arg in link]
        link_mutant[link_mutant.index("-o")+1]=str(binary)
        with (out/(name+"-build.log")).open("w") as log:
            for command in (compile_mutant,link_mutant):
                subprocess.run(command,cwd=src,stdout=log,stderr=subprocess.STDOUT,check=True)
        run=subprocess.run([str(binary)],cwd=src,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
        (out/(name+".log")).write_text(run.stdout)
        if run.returncode!=1 or expected not in run.stdout: raise RuntimeError(f"{name}: intended failing assertion was not observed")
        print(f"{name}: observed expected failing assertion",flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", type=Path)
    parser.add_argument("--only", action="append", help="run only this named mechanism (repeatable)")
    parser.add_argument("--suite", choices=("ui", "cs2", "host", "lua", "products", "actions", "orbs", "xporbs"), default="ui")
    parser.add_argument("--make-arg", action="append", default=[], help="make assignment, e.g. OPT=1")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    out = args.out.resolve()
    src = Path(__file__).resolve().parents[1] / "src"
    if args.suite == "actions":
        run_action_controls(src, out, args.make_arg, args.only)
        return
    if args.suite == "orbs":
        run_orbs_controls(src, out, args.make_arg, args.only)
        return
    if args.suite == "xporbs":
        run_xporbs_controls(src, out, args.make_arg, args.only)
        return
    if args.suite == "products":
        run_product_controls(src, out, args.make_arg, args.only)
        return
    if args.suite == "lua":
        run_lua_controls(src,out,args.make_arg,args.only)
        return
    if args.suite == "host":
        run_host_controls(src, out, args.make_arg, args.only)
        return
    if args.suite == "cs2":
        run_cs2_controls(src, out, args.make_arg, args.only)
        return
    recipe = subprocess.check_output(
        ["make", "--no-print-directory", *args.make_arg, "-n", "test-uitree"], cwd=src, text=True
    ).replace("\\\n", " ")
    command = next(shlex.split(line) for line in recipe.splitlines()
                   if "ui/uitree.c " in line and " -o " in line)
    original = (src / "ui/uitree.c").read_text()
    controls = {
        "widget_visibility": (
            "c->widget_hidden=hidden;", "c->widget_hidden=false;",
            "hidden widget descendants reject input and native operations"),
        "projection_height": (
            "return last ? last->projection_height : 0;", "return 0;",
            "live overlay accepts world projection height"),
        "owned_child_keys": (
            "if( child->plugin_owner ) return -1;", "if( false ) return -1;",
            "owned widgets do not pollute native child keys"),
        "owned_slot_survival": (
            "if( tree->components[child].plugin_owner ||", "if( false ||",
            "native slot replacement preserves attached owned controls"),
        "widget_geometry": (
            "return (position ? 1 : 0) | (size ? 2 : 0);",
            "return 0;", "widget geometry reaches native layout including zero"),
        "operation_freshness": (
            "(!pick->action_signature || pick->action_signature == UITree_ActionSignatureAt(tree, pick->node_index))",
            "true", "retained menu rejects changed operation labels"),
        "operation_text": (
            "hash = action_hash_text(hash, c->u.rs_text.text);",
            "(void)hash;", "retained menu rejects changed native text target"),
        "geometry_audit": ("if( current[i] != record->values[i] )", "if( false )",
                           "audit catches unclassified native position"),
        "global_identity": ("component->incarnation = atomic_fetch_add(&next_incarnation, 1);",
                            "component->incarnation = (uint64_t)idx + 1;",
                            "retained incarnation cannot affect another tree"),
        "copy_state": ("i < src.params_count", "i < 0", "copy preserves integer parameters"),
        "tree_identity": ("ref.tree_instance != tree->instance_id", "false",
                          "reference cannot cross tree instances"),
        "identity": ("!c->freed && c->incarnation == ref.incarnation",
                     "!c->freed", "old focus cannot move"),
        "geometry": ("if( pos->layout_resolved )\n        return pos->abs_w",
                     "if( pos->layout_resolved && pos->abs_w > 0 )\n        return pos->abs_w",
                     "computed zero is not replaced"),
        "content_hide": ("c->item_id = obj_id;",
                         "UITree_SetHideAt(tree, idx, 0); c->item_id = obj_id;",
                         "content update cannot clear native hiding"),
        "mount_hide": ("c->mount_hidden = (uint8_t)hidden;",
                       "c->mount_hidden = (uint8_t)hidden; if( !hidden ) UITree_SetHideAt(tree, idx, 0);",
                       "mount cannot clear later server hide"),
        "topology": ("ancestor == child_index ||", "false ||", "reject descendant cycle"),
    }
    controls = {name: ("ui/uitree.c", *control) for name, control in controls.items()}
    controls["sidebar_group"] = ("ui/uitree_frame.c",
        "return frame_slot_cache.group[slot] = parent;",
        "return UITree_FrameSlotNode(tree,slot);",
        "sidebar widget resolves member parent, not modal or hidden tab")
    if args.only and set(args.only) - controls.keys():
        parser.error("unknown control: " + ", ".join(set(args.only) - controls.keys()))
    for name, (source, before, after, expected) in controls.items():
        if args.only and name not in args.only:
            continue
        original = (src / source).read_text()
        if original.count(before) != 1:
            raise RuntimeError(f"{name}: mechanism changed; update the explicit mutation")
        mutant = out / f"{name}.c"
        mutant.write_text(original.replace(before, after))
        binary = out / name
        build = [str(mutant) if arg == source else arg for arg in command]
        build[build.index("-o") + 1] = str(binary)
        with (out / f"{name}-build.log").open("w") as log:
            subprocess.run(build, cwd=src, stdout=log, stderr=subprocess.STDOUT, check=True)
        run = subprocess.run([str(binary)], cwd=src,
                             env={**os.environ, "TORIRS_TEST_CONTRACT_V3": "1"},
                             text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             timeout=30)
        (out / f"{name}.log").write_text(run.stdout)
        if run.returncode != 1 or "FAIL: " + expected not in run.stdout:
            raise RuntimeError(f"{name}: did not produce its expected assertion: {run.stdout}")
        print(f"{name}: observed expected failing assertion", flush=True)


if __name__ == "__main__":
    main()
