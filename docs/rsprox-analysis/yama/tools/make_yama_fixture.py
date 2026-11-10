#!/usr/bin/env python3
"""Build test/bosses/fixtures/yama_kit.ini: the recorder's kit (index 4498 --bas, tick 684).

Worn and carried items as recorded, except the rune pouch: its contents live in varbits, so the
fixture carries the same runes loose (Dark Demonbane: 7 fire, 2 soul per cast; Mark of Darkness
(the recorder's cast at [1600]-ish, +50% under the purging staff): 1 soul, 1 cosmic).
"""
import re, sys
HERE = '/Users/matthewevers/Documents/git_repos/3draster/'
base = open(HERE + 'test/bosses/fixtures/fresh_lumbridge.ini', encoding='utf8').read()
ids = {}
for line in open(HERE + 'OSRS-Content/osrs-content/configs/all.obj.compack', errors='replace'):
    m = re.match(r'^(\d+)=(.+)$', line.strip())
    if m:
        ids[m.group(2)] = int(m.group(1))
worn = {0: 'viking_helmet_magic', 1: 'ma2_saradomin_cape', 2: 'occult_necklace', 3: 'purging_staff',
        4: 'barrows_ahrim_body_50', 5: 'magictraining_bookofmagic', 7: 'barrows_ahrim_legs_50',
        9: 'zenyte_bracelet_enchanted', 10: 'eternal_boots', 12: 'nzone_zenyte_ring_enchanted_recoil',
        13: 'zeah_blessing_elite'}
# The recorded backpack, rune pouch and the two oddments replaced by the loose runes.
inv = [('firerune', 30000), ('emberlight', 1)] + [('mantaray', 1)] * 12 + \
      [('4doseprayerrestore', 1)] * 9 + [('4dose2restore', 1)] * 2 + \
      [('extended_antivenom+4', 1), ('soulrune', 8000), ('cosmicrune', 10000)]
assert len(inv) == 28, len(inv)
# Recorded levels (4498 update_stat at login); skill xp is written as the level's minimum.
XP = {1: 0}
LEVELS = {'attack': 97, 'defence': 99, 'strength': 98, 'hitpoints': 99, 'ranged': 99, 'prayer': 76, 'magic': 99}
STAT = {'attack': 0, 'defence': 1, 'strength': 2, 'hitpoints': 3, 'ranged': 4, 'prayer': 5, 'magic': 6}
def xp_for(level):
    t = 0
    for l in range(1, level):
        t += int(l + 300 * (2 ** (l / 7.0)))
    return t // 4
s = base
# place by the Voice of Yama (the Chasm's lowest floor); the varps: spellbook Arceuus, contract signed
s = re.sub(r'^name = .*$', 'name = yama_kit', s, count=1, flags=re.M)
s = re.sub(r'^x = \d+', 'x = 1442', s, count=1, flags=re.M)
s = re.sub(r'^z = \d+', 'z = 10080', s, count=1, flags=re.M)
s = re.sub(r'^level = \d+', 'level = 0', s, count=1, flags=re.M)
s = s.replace('[varps]\n', '[varps]\n439 = 3\n4716 = 2\n9561 = 1\n', 1)
# strip any existing inv/worn/stats sections then re-emit
def cut(section, text):
    return re.sub(r'^\[%s\]\n(?:[^\[]*\n)?' % section, '', text, flags=re.M)
for sec in ('stats', 'inv', 'worn', 'worn_var'):
    s = cut(sec, s)
out = [s.rstrip('\n'), '', '[stats]', '; <stat> = <boosted> <xp_tenths>']
for name, st in sorted(STAT.items(), key=lambda kv: kv[1]):
    out.append('%d = %d %d' % (st, LEVELS[name], xp_for(LEVELS[name]) * 10))
out += ['', '[inv]', '; <slot> = <obj> <count>']
for i, (n, c) in enumerate(inv):
    out.append('%d = %d %d' % (i, ids[n], c))
out += ['', '[worn]']
for k in sorted(worn):
    out.append('%d = %d 1' % (k, ids[worn[k]]))
out += ['', '[worn_var]', '']
body = '\n'.join(out)
open(HERE + 'test/bosses/fixtures/yama_kit.ini', 'w', encoding='utf8').write(body)
open(HERE + 'test/raids/fixtures/yama_kit.ini', 'w', encoding='utf8').write(body)
print('wrote', len(inv), 'inv rows,', len(worn), 'worn')
