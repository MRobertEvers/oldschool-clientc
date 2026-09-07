#!/usr/bin/env python3
"""Prove the canoe gate rejects regressions in a private warm content session."""
import argparse
import json
from pathlib import Path
import time
from content_selftest import Session, recompile
from canoe_animation_test import Capture, prepare, arrival_after_seat, verify


def run(session, source):
    source = Path(source).resolve()
    if not source.is_relative_to(session.directory) or source.name != 'canoe_cutscene.rs2':
        raise ValueError('Use the canoe script copied inside this private session directory.')
    original = source.read_text()
    mutations = {
        'wrong_camera_bank': ('cam_moveto(^canoe_cam_river_eye, 255, 100, 100);',
                              'cam_moveto(0_28_70_33_35, 255, 100, 100);'),
        'sideways_paddler': ('facesquare(movecoord(coord, -1, 0, 0));',
                            'facesquare(movecoord(coord, 0, 0, 1));'),
        'backward_scenery': ('npc_walk(movecoord(npc_coord, 0, 0, 32));',
                             'npc_walk(movecoord(npc_coord, 0, 0, -32));'),
        'late_tree': ('case 7: ~canoe_scenery_add', 'case 9: ~canoe_scenery_add'),
    }
    expected_errors = {'wrong_camera_bank':'camera', 'sideways_paddler':'facing',
                       'backward_scenery':'lane/path', 'late_tree':'spawn/despawn'}
    results = {}
    started = time.monotonic()
    try:
        for name, (before, after) in mutations.items():
            assert original.count(before) == 1, f'{name}: mutation target changed'
            session.call('close'); session.step(900); session.call('close')
            source.write_text(original.replace(before, after))
            recompile(session, source)
            prepare(session)
            session.call('cheat canoeride 4')
            capture = Capture(session)
            rows = capture.run('ride-4', 1200, (1817,4514),
                               stop=arrival_after_seat((1817,4515),(3109,3415)))
            # A setup/runtime failure is not a detected visual regression.
            try:
                verify(rows)
            except AssertionError as error:
                assert expected_errors[name] in str(error), f'{name}: unexpected failure: {error}'
                results[name] = str(error)
            else:
                raise AssertionError(f'{name}: broken cutscene passed')
            (session.directory/f'{name}-trace.json').write_text(json.dumps(rows)+'\n')
            session.step(330); session.call('resume messagebox:continue'); session.step(30)
    finally:
        source.write_text(original)
        recompile(session, source)
    report = {'ok':True, 'seconds':time.monotonic()-started, 'rejected':results}
    (session.directory/'cutscene-mutations.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    if not __debug__: raise SystemExit('Run without -O.')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', required=True)
    parser.add_argument('--source', required=True)
    args = parser.parse_args()
    run(Session(args.session), args.source)
