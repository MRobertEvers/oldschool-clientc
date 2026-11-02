# fish tab completion for ./launch.
#
#   ./launch completion fish > ~/.config/fish/completions/launch.fish
# or, for the current shell only:
#   ./launch completion fish | source
#
# The candidate list comes from `launch complete`, so this file never has to be
# updated when a subcommand, flag, or profile is added. fish shows the
# `word<TAB>description` lines as word and description itself.

function __launch_complete
    # Every token before the cursor, the command itself first; then the word
    # under the cursor, which is "" when the cursor sits after a space.
    set -l tokens (commandline -opc)
    set -l current (commandline -ct)
    set -l exe $tokens[1]
    set -e tokens[1]
    # No prefix test here: `launch complete` has already decided what the
    # half-typed word matches, infix and subsequence matches included.
    $exe complete -- $tokens $current 2>/dev/null
end

# -f: `launch complete` offers nothing for a free-form client argument, and a
# file list mixed into every other slot would bury the real candidates. fish
# matches `./launch` and `launch` alike by the command's name. -k keeps the
# launcher's own order (fish would otherwise sort the subsequence matches away
# from where the launcher ranked them).
complete -c launch -f -k -a '(__launch_complete)'
