# bash tab completion for ./launch.
#
#   source tools/launcher/completions/launch.bash
# or
#   eval "$(./launch completion bash)"
#
# The candidate list comes from `launch complete`, so this file never has to be
# updated when a subcommand, flag, or profile is added.

_launch_complete()
{
    local exe line
    exe="${COMP_WORDS[0]}"
    COMPREPLY=()

    local -a sent
    sent=("${COMP_WORDS[@]:1:COMP_CWORD}")

    while IFS= read -r line; do
        # `word<TAB>description` — bash shows the word only.
        line="${line%%$'\t'*}"
        [ -z "$line" ] && continue
        # No prefix test here: `launch complete` has already decided what the
        # half-typed word matches, infix and subsequence matches included, and
        # a second filter here would throw exactly those away.
        COMPREPLY+=("$line")
    done < <("$exe" complete -- "${sent[@]}" 2>/dev/null)
}

# -o default: when there is nothing to suggest (a free-form client argument),
# fall back to filenames rather than to nothing at all.
complete -o default -F _launch_complete launch ./launch
