# Bring the world up, and keep it up. This is what the boot task runs.
#
#     .\start-torirs.ps1 [-GamePort 43594] [-WebPort 8088] [-Distro Ubuntu]
#
# Four jobs, in order:
#
#   1. start the WSL distro (systemd starts dockerd, and the container's
#      `restart: unless-stopped` starts the world behind it);
#   2. wait for the container's ports to answer inside the distro;
#   3. point Windows at them with `netsh interface portproxy`, because a
#      container port published inside WSL is NOT reachable at the machine's
#      LAN address on its own;
#   4. hold a WSL session open forever, because WSL stops a distro the moment
#      nothing is attached to it -- and stopping the distro stops the world.
#
# Step 3 is re-done on every run on purpose: WSL's address is handed out fresh
# each boot, so a portproxy rule written once is wrong by the next restart. That
# is the single thing that makes this fragile if you do it by hand, and the only
# reason this script exists rather than a one-line schtasks entry.

[CmdletBinding()]
param(
    [int]$GamePort = 43594,
    [int]$WebPort  = 8088,
    [string]$Distro = 'Ubuntu',
    [switch]$NoHold
)

$ErrorActionPreference = 'Stop'

function Say($m) { Write-Host "start-torirs: $m" }

# ---------------------------------------------------------------- 1. distro
Say "starting distro '$Distro'"
& wsl.exe -d $Distro -u root --exec /bin/true
if ($LASTEXITCODE -ne 0) { throw "could not start distro '$Distro'" }

$wslIp = (& wsl.exe -d $Distro -u root --exec /bin/sh -c "ip -4 -o addr show eth0 | awk '{print `$4}' | cut -d/ -f1").Trim()
if (-not $wslIp) { throw "could not read the distro's address" }
Say "distro address is $wslIp"

# ---------------------------------------------------------------- 2. wait
foreach ($p in @($GamePort, $WebPort)) {
    $ok = $false
    # The world's boot is tens of seconds from a warm page cache and longer
    # from cold, so this waits minutes rather than seconds before giving up.
    foreach ($i in 1..60) {
        $probe = & wsl.exe -d $Distro -u root --exec /bin/sh -c "(exec 3<>/dev/tcp/127.0.0.1/$p) 2>/dev/null && echo up"
        if ($probe -match 'up') { $ok = $true; break }
        Start-Sleep -Seconds 5
    }
    if ($ok) { Say "port $p is answering inside the distro" }
    else     { Say "WARNING: port $p never answered -- the world may still be booting" }
}

# ---------------------------------------------------------------- 3. forward
foreach ($p in @($GamePort, $WebPort)) {
    # Delete first: a stale rule from the previous boot points at an address
    # the distro no longer has, and `add` will not replace it.
    & netsh.exe interface portproxy delete v4tov4 listenport=$p listenaddress=0.0.0.0 2>&1 | Out-Null
    & netsh.exe interface portproxy add v4tov4 `
        listenaddress=0.0.0.0 listenport=$p connectaddress=$wslIp connectport=$p | Out-Null
    Say "forwarding 0.0.0.0:$p -> ${wslIp}:$p"

    # The Windows firewall DROPS rather than refuses, so without this a client
    # hangs instead of failing -- which reads like a dead server, not a blocked
    # port. Scoped to the VPN subnet, matching the native lane's rules.
    $name = "torirs-docker-$p"
    if (-not (Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName $name -Direction Inbound -Protocol TCP `
            -LocalPort $p -RemoteAddress 10.0.0.0/24 -Action Allow -Profile Any | Out-Null
        Say "firewall rule '$name' added (10.0.0.0/24)"
    }
}

& netsh.exe interface portproxy show v4tov4

# ---------------------------------------------------------------- 4. hold
if ($NoHold) { Say "not holding a session (-NoHold); the distro may stop when idle"; return }

Say "holding a session open -- this process is what keeps the distro alive"
& wsl.exe -d $Distro -u root --exec /bin/sleep infinity
