#compdef launch ./launch
#
# zsh tab completion for ./launch.
#
#   ./launch completion zsh > ~/.zfunc/_launch      # with ~/.zfunc in $fpath
# or
#   source tools/launcher/completions/launch.zsh    # after compinit
#
# The candidate list comes from `launch complete`, so this file never has to be
# updated when a subcommand, flag, or profile is added.

_launch()
{
    local exe raw line
    local -a lines described

    exe=${words[1]}
    raw=$("$exe" complete -- "${(@)words[2,CURRENT]}" 2>/dev/null) || return 1
    lines=("${(@f)raw}")

    for line in $lines; do
        [[ -z $line ]] && continue
        # `word<TAB>description` -> the `word:description` _describe wants.
        described+=("${line/$'\t'/:}")
    done
    (( $#described )) || return 1

    # -U: `launch complete` has already matched the half-typed word, and its
    # answer may deliberately not start with what was typed (`osrs-` offers
    # osrs239-web). Without -U, zsh would filter those out again.
    _describe -V -t launch-candidates 'launch' described -U
}

if [[ $zsh_eval_context[-1] == loadautofunc ]]; then
    # Autoloaded from $fpath as _launch: zsh has already picked us by the
    # #compdef line above, and just wants the function run.
    _launch "$@"
elif (( $+functions[compdef] )); then
    compdef _launch launch ./launch
else
    print -u2 "launch: run compinit before sourcing this (compdef is not defined yet)"
fi
