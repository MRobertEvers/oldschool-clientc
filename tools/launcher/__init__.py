"""The ./launch implementation.

Split by concern rather than by command:

  iniparse.py    the boot-manifest INI dialect (repeated keys, colon sections)
  profiles.py    profiles, manifests, and the override -> resolved-manifest merge
  services.py    which processes a run needs, and how to start each
  supervisor.py  pidfiles, readiness, status, stop, logs
  staleness.py   [derived:*] blocks, and the run-live.sh fallback
  cli.py         argument parsing and the command bodies
"""
