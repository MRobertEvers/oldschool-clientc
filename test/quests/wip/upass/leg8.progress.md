run0: read docs, reading kalrag/cages/disciple scripts
run1 (leg8 only): part A (klank, gauntlets, tomb ashes) PASS 9/0; next part B Kalrag. NOTE: file leg inserted via python; run.py --from-leg 8 takes ~3min foreground (use timeout 590000)
run4: part B Kalrag PASS (needed walk to 2356,9900 for presence); next part C: ascend 2304,9915, cage, disciple, temple, well
run7: C1 PASS (ascend, cage via bridgecollapsed2 2121,4686; BFS tool scratchpad/bfs8.py level1). next: disciple kill (worn must be ONLY robes to open temple door: unequip scimitar+gauntlets), altar
run8: C2 PASS (disciple kill + robes taken, 809 ticks). next: enterTemple (only robes worn), useDollOnWell, Koftik, Lathas
run10: through useDollOnWell PASS (stage 9, player thrown to pocket 2482,9607 L0 closed 213 tiles). CONTENT BUG: caveguide5 only at m33_73.spawn:37 (2170,4727 L1); upass_last_out (upass_tablets.rs2:9) needs caveguide5 within 9 of 2438,9607; caveguide6 at 2443,9607 is regicide's koftik.rs2:12. Next: write talk caveguide6 + last_out press + t.blocked; then hand-off.
