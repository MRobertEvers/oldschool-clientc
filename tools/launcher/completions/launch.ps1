# PowerShell tab completion for launch (launch.cmd on Windows, ./launch on a
# dev host running pwsh).
#
#   ./launch completion powershell | Out-String | Invoke-Expression
# or, once, in $PROFILE:
#   . <repo>/tools/launcher/completions/launch.ps1
#
# The candidate list comes from `launch complete`, so this file never has to be
# updated when a subcommand, flag, or profile is added. launch.cmd hands
# `complete` to the Python launcher like every other word, so this works
# wherever `launch.cmd list` does.

$__launchCompleter = {
    param($wordToComplete, $commandAst, $cursorPosition)

    $elements = @($commandAst.CommandElements)
    if ($elements.Count -eq 0) { return }
    $exe = $elements[0].Extent.Text
    $words = @()
    if ($elements.Count -gt 1) {
        $words = @($elements[1..($elements.Count - 1)] | ForEach-Object { $_.Extent.Text })
    }
    # After a trailing space PowerShell hands an empty word and no element for
    # it; `launch complete` wants that empty word as the one being completed.
    if ($wordToComplete -eq '') { $words += '' }

    $lines = @()
    try {
        $lines = & $exe complete -- @words 2>$null
    } catch {
        return
    }
    foreach ($line in $lines) {
        if ([string]::IsNullOrEmpty($line)) { continue }
        # `word<TAB>description`; the description becomes the tooltip.
        $parts = $line -split "`t", 2
        $word = $parts[0]
        $tip = if ($parts.Count -gt 1 -and $parts[1]) { $parts[1] } else { $word }
        [System.Management.Automation.CompletionResult]::new($word, $word, 'ParameterValue', $tip)
    }
}

# Every spelling the command is invoked by: the shim on Windows, the script on
# a dev host, each with and without the explicit path.
foreach ($name in @('launch', 'launch.cmd', './launch', '.\launch', '.\launch.cmd', './launch.cmd')) {
    Register-ArgumentCompleter -Native -CommandName $name -ScriptBlock $__launchCompleter
}
