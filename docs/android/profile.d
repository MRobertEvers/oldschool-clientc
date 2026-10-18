#!/usr/sbin/dtrace -s

/* 
 * Profile application stacks at ~1000Hz
 * Usage: sudo ./profile.d -c <command>
 * Change the number profile-<tick speed>
 */

profile-4000 /pid == $target/ {
    @[ustack()] = count();
}

tick-30s {
    exit(0);
}