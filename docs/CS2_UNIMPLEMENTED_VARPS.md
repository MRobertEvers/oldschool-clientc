# CS2-referenced varps missing server transmission

> Historical baseline: the implementation described here has now been applied.
> `python3 tools/cs2_varp_audit.py --report` currently reports 175 shared varps
> and **0 transmission gaps**. The reviewed disposition/evidence ledger is
> `OSRS-Content/osrs239-content/port/cs2_varps.map`; 78 rows are implemented and
> the 27 music-unlock rows name their missing region-to-music producer as a
> blocker after storage and transmission were added.

This audit identifies actionable varp implementation gaps in the revision 239 content: a varp is listed when all three conditions hold:

1. A decompiled client script in OSRS-Content/osrs239-content/scripts/*.cs2 references %var<ID>.
2. A server .rs2 script references the corresponding varp symbol.
3. No matching server .varp declaration enables transmit=yes.

This definition intentionally excludes client-owned varps that CS2 reads or writes but the server content never references. Those are not necessarily server implementation gaps.

## Summary

- CS2 files scanned: 9,368
- Distinct varp IDs referenced by CS2: 1,219
- Varp IDs referenced by both CS2 and server scripts: 175
- Missing client transmission: 105 across 47 CS2 scripts
  - No server .varp declaration: 71
  - Server .varp declaration exists, but lacks transmit=yes: 34

## No server .varp declaration

| Varp | Symbol | CS2 scripts |
|---:|---|---|
| 0 | mcannon | script_4024.cs2 |
| 10 | cogquest | script_4024.cs2 |
| 17 | arenaquest | script_1901.cs2, script_4024.cs2 |
| 18 | musicplay | script_315.cs2, script_318.cs2, script_3962.cs2, script_3967.cs2, script_9292.cs2, script_9297.cs2, script_9630.cs2, script_9632.cs2, script_9633.cs2 |
| 20 | musicmulti_1 | script_7305.cs2, script_7306.cs2 |
| 21 | musicmulti_2 | script_7305.cs2, script_7306.cs2 |
| 22 | musicmulti_3 | script_7305.cs2, script_7306.cs2 |
| 23 | musicmulti_4 | script_7305.cs2, script_7306.cs2 |
| 24 | musicmulti_5 | script_7305.cs2, script_7306.cs2 |
| 25 | musicmulti_6 | script_7305.cs2, script_7306.cs2 |
| 75 | journey_number | script_2382.cs2 |
| 76 | scorpcatcher | script_2094.cs2, script_2520.cs2, script_4024.cs2, script_5127.cs2 |
| 102 | poison | script_446.cs2, script_5342.cs2, script_5923.cs2, script_7421.cs2, script_7424.cs2, script_7425.cs2 |
| 111 | treequest | script_1901.cs2, script_4024.cs2 |
| 112 | itgronigen | script_1969.cs2, script_4024.cs2, script_7856.cs2 |
| 116 | zombiequeen | script_1901.cs2, script_4024.cs2 |
| 139 | legendsquest | script_1901.cs2, script_4024.cs2, script_6657.cs2, script_7856.cs2 |
| 148 | crestquest | script_1901.cs2, script_4024.cs2 |
| 150 | grandtree | script_1901.cs2, script_1969.cs2, script_4024.cs2, script_7856.cs2, script_9104.cs2 |
| 153 | pilot_journey | script_1288.cs2, script_1290.cs2 |
| 159 | seaslugquest | script_4024.cs2 |
| 200 | totemquest | script_4024.cs2 |
| 212 | itwatchtower | script_2664.cs2, script_4024.cs2, script_7856.cs2 |
| 267 | magearena | script_4024.cs2 |
| 268 | saramage | script_5993.cs2, script_6838.cs2 |
| 269 | guthmage | script_5993.cs2, script_6838.cs2 |
| 270 | zamomage | script_5993.cs2, script_6838.cs2 |
| 272 | magearena_charge | script_5923.cs2 |
| 298 | musicmulti_7 | script_7305.cs2, script_7306.cs2 |
| 307 | druidspirit | script_4024.cs2 |
| 311 | musicmulti_8 | script_7305.cs2, script_7306.cs2 |
| 320 | tbwt_main | script_4024.cs2 |
| 321 | tbwt_tiadeche | script_4566.cs2 |
| 328 | regicide_quest | script_4024.cs2, script_9104.cs2 |
| 335 | eadgar_quest | script_2664.cs2, script_4024.cs2 |
| 339 | morttonquest | script_1598.cs2, script_4024.cs2 |
| 346 | musicmulti_9 | script_7305.cs2, script_7306.cs2 |
| 347 | viking | script_4024.cs2 |
| 359 | misc_quest | script_2094.cs2, script_4024.cs2 |
| 365 | mm_main | script_1901.cs2, script_2664.cs2, script_4024.cs2, script_9104.cs2 |
| 385 | troll_love | script_1901.cs2, script_4024.cs2 |
| 387 | routequest | script_1901.cs2, script_4024.cs2 |
| 414 | musicmulti_10 | script_7305.cs2, script_7306.cs2 |
| 464 | musicmulti_11 | script_7305.cs2, script_7306.cs2 |
| 598 | musicmulti_12 | script_7305.cs2, script_7306.cs2 |
| 662 | musicmulti_13 | script_7305.cs2, script_7306.cs2 |
| 721 | musicmulti_14 | script_7305.cs2, script_7306.cs2 |
| 906 | musicmulti_15 | script_7305.cs2, script_7306.cs2 |
| 1009 | musicmulti_16 | script_7305.cs2, script_7306.cs2 |
| 1060 | nzone_rewardpoints | script_311.cs2 |
| 1338 | musicmulti_17 | script_7305.cs2, script_7306.cs2 |
| 1510 | total_callisto_kills | script_4776.cs2, script_4778.cs2 |
| 1511 | total_venenatis_kills | script_4776.cs2, script_4778.cs2 |
| 1512 | total_vetion_kills | script_4776.cs2, script_4778.cs2 |
| 1528 | total_wintertodt_kills | script_4775.cs2, script_4778.cs2 |
| 1681 | musicmulti_18 | script_7305.cs2, script_7306.cs2 |
| 2065 | musicmulti_19 | script_7305.cs2, script_7306.cs2 |
| 2224 | makexcrafting | script_2928.cs2, script_2930.cs2, script_3256.cs2, script_3260.cs2, script_3262.cs2 |
| 2237 | musicmulti_20 | script_7305.cs2, script_7306.cs2 |
| 2353 | total_completed_gauntlet | script_4776.cs2, script_4778.cs2 |
| 2354 | total_completed_gauntlet_hm | script_4776.cs2, script_4778.cs2 |
| 2950 | musicmulti_21 | script_7305.cs2, script_7306.cs2 |
| 3418 | musicmulti_22 | script_7305.cs2, script_7306.cs2 |
| 3575 | musicmulti_23 | script_7305.cs2, script_7306.cs2 |
| 3761 | total_artio_kills | script_4776.cs2 |
| 3762 | total_spindel_kills | script_4776.cs2 |
| 3763 | total_calvarion_kills | script_4776.cs2 |
| 4066 | musicmulti_24 | script_7305.cs2, script_7306.cs2 |
| 4411 | musicmulti_25 | script_7305.cs2, script_7306.cs2 |
| 4944 | musicmulti_26 | script_7305.cs2, script_7306.cs2 |
| 5238 | musicmulti_27 | script_7305.cs2, script_7306.cs2 |

## Declared, but not transmitted

These varps have a server .varp section, but that section does not set transmit=yes; the runtime default is server-only.

| Varp | Symbol | CS2 scripts |
|---:|---|---|
| 5 | grail | script_1901.cs2, script_4024.cs2, script_4561.cs2 |
| 11 | fishingcompo | script_4024.cs2 |
| 14 | arthur | script_1969.cs2, script_4024.cs2 |
| 29 | cookquest | script_2352.cs2, script_4024.cs2 |
| 30 | drunkmonkquest | script_4024.cs2 |
| 31 | doricquest | script_4024.cs2 |
| 32 | haunted | script_4024.cs2 |
| 60 | sheepherderquest | script_4024.cs2 |
| 63 | runemysteries | script_1969.cs2, script_3809.cs2, script_4024.cs2 |
| 67 | hetty | script_4024.cs2 |
| 68 | biohazard | script_2664.cs2, script_4024.cs2 |
| 71 | hunt | script_4024.cs2 |
| 80 | druidquest | script_3809.cs2, script_4024.cs2 |
| 107 | prieststart | script_4024.cs2 |
| 122 | squire | script_4024.cs2 |
| 130 | spy | script_4024.cs2 |
| 131 | itexamlevel | script_4024.cs2 |
| 144 | rjquest | script_4024.cs2 |
| 145 | phoenixgang | script_1969.cs2 |
| 146 | blackarmgang | script_1969.cs2 |
| 147 | zanaris | script_1901.cs2, script_4024.cs2 |
| 160 | imp | script_4024.cs2 |
| 161 | upass | script_4024.cs2 |
| 165 | elenaquest | script_2664.cs2, script_4024.cs2 |
| 176 | dragonquest | script_1901.cs2, script_2352.cs2, script_4024.cs2, script_9104.cs2 |
| 178 | vampire | script_1901.cs2, script_2352.cs2, script_4024.cs2 |
| 179 | sheep | script_4024.cs2 |
| 180 | fluffs | script_4024.cs2 |
| 188 | heroquest | script_4024.cs2 |
| 192 | murderquest | script_4024.cs2 |
| 223 | hazeelcultquest | script_1969.cs2, script_4024.cs2 |
| 226 | ballquest | script_1901.cs2, script_4024.cs2 |
| 273 | princequest | script_4024.cs2, script_7856.cs2 |
| 314 | death_equiproom | script_430.cs2, script_4024.cs2 |

## Notes

- Script names are the filenames in OSRS-Content/osrs239-content/scripts/.
- “Referenced by server scripts” includes both reads and writes; this audit does not infer ownership from source syntax.
- A missing transmission is not automatically fixed by adding transmit=yes: shared varp carriers and client-local state still need semantic review before implementation.
