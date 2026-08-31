"""Wake the XP test box with a Wake-on-LAN magic packet.

    python tools/xpbox_wake.py --learn        # once, while the box is UP
    python tools/xpbox_wake.py                # thereafter
    python tools/xpbox_wake.py --wait 120     # and block until it answers

WHY A SCRIPT AND NOT `wol 10.10.10.2`

Wake-on-LAN is addressed to a MAC, never to an IP, and that is not a detail you
can work around. A machine that is off does not answer ARP, so the moment you
actually need to wake it is the one moment its IP cannot be turned into a MAC.
The address has to have been learned earlier and written down -- which is what
`--learn` does, and why it only works while the box is still on.

The second thing that catches people here: this host has several adapters, and
the XP box is on a direct point-to-point link (10.10.10.1 <-> 10.10.10.2, no
router, no DHCP server, nothing else on the wire). A broadcast sent without
binding the source address goes out whichever interface the routing table likes
best, which is generally the one facing the internet -- so the packet is sent,
sendto() reports success, and nothing wakes up. The socket is bound to the
source IP explicitly for that reason.

WHAT WAKE-ON-LAN NEEDS AT THE OTHER END, if this does nothing:

  - enabled in the BIOS/firmware ("Wake on LAN", "Power on by PCI-E", "Resume
    by PME"). On P4-era boards it is usually OFF by default.
  - enabled on the NIC in Windows: Device Manager -> the adapter -> Power
    Management -> "Allow this device to wake the computer".
  - real power to the NIC when the machine is off. Some machines only keep it
    powered from S3/S5 and not after a full power cut; if the box was unplugged
    since it was last shut down, WoL will not work until it has been booted
    once more.
"""
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CONFIG = os.path.join(HERE, '.xpbox_wake.json')

DEFAULT_IP = '10.10.10.2'
DEFAULT_SOURCE = '10.10.10.1'
DEFAULT_BROADCAST = '10.10.10.255'

# rpdxp, the control service the XP drivers in tools/mem/ talk to
# (http://10.10.10.2:8088). This is the readiness signal worth waiting on:
# ICMP starts answering while the box is still working through boot, so a
# script that waits for ping and then POSTs a job gets a connection refused.
# Waiting for the port that will actually be used avoids that race.
DEFAULT_WAIT_PORT = 8088

# 9 is the conventional discard-protocol port for this; 7 (echo) is what some
# older NICs were built to watch instead. The packet is identified by its
# contents, not by the port, so sending both costs nothing and covers both.
WOL_PORTS = (9, 7)


def normalise_mac(text):
    """'00:1A:2B:3C:4D:5E', '00-1a-2b-3c-4d-5e' or '001a2b3c4d5e' -> bytes."""
    digits = re.sub(r'[^0-9a-fA-F]', '', text)
    if len(digits) != 12:
        raise ValueError(
            'not a MAC address: %r (want 12 hex digits, got %d)' % (text, len(digits)))
    return bytes.fromhex(digits)


def pretty_mac(raw):
    return ':'.join('%02X' % b for b in raw)


def magic_packet(mac):
    """The frame itself: six 0xFF bytes, then the MAC sixteen times over.

    That is the whole specification. It is deliberately something that cannot
    occur by accident in normal traffic, which is what lets a sleeping NIC
    recognise it with the host CPU powered down."""
    return b'\xff' * 6 + mac * 16


def load_config():
    try:
        with open(CONFIG) as f:
            return json.load(f)
    except (IOError, ValueError):
        return {}


def save_config(cfg):
    with open(CONFIG, 'w') as f:
        json.dump(cfg, f, indent=2, sort_keys=True)
        f.write('\n')


def arp_lookup(ip):
    """Ask the OS for the MAC it has learned for `ip`, or None.

    Only ever works while the host is reachable -- see the module docstring."""
    # A ping first: the entry we want is a side effect of having talked to it.
    subprocess.run(['ping', '-n', '1', '-w', '1000', ip],
                   capture_output=True, text=True)
    out = subprocess.run(['arp', '-a', ip], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if ip not in line:
            continue
        m = re.search(r'([0-9a-fA-F]{2}(?:[:-][0-9a-fA-F]{2}){5})', line)
        if m:
            return normalise_mac(m.group(1))
    return None


def send_magic(mac, broadcast, source, repeat, unicast_ip=None):
    """Broadcast the frame, and optionally also send it to a unicast address.

    Broadcast is the normal form and is what a NIC in standby is documented to
    accept. Some NICs are fussier and only act on a magic packet whose ETHERNET
    destination is their own MAC, which a subnet broadcast never is -- for
    those, the packet has to go to the host's own IP.

    That only works if the OS can frame it, and a machine that is off does not
    answer ARP, so it needs a static ARP entry to exist first:

        netsh interface ipv4 add neighbors "Ethernet 3" 10.10.10.2 00-0d-60-95-47-77

    which needs an elevated shell. Without it the send silently goes nowhere:
    the OS holds the packet waiting on an ARP reply that never comes.
    """
    packet = magic_packet(mac)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    try:
        if source:
            # Port 0: any source port will do, the destination is what matters.
            # This bind is the difference between waking the box and sending
            # the packet out of the internet-facing NIC. See the docstring.
            sock.bind((source, 0))
        targets = [broadcast] + ([unicast_ip] if unicast_ip else [])
        sent = 0
        for _ in range(repeat):
            for target in targets:
                for port in WOL_PORTS:
                    try:
                        sock.sendto(packet, (target, port))
                        sent += 1
                    except OSError as exc:
                        # A unicast send with no ARP entry fails here rather
                        # than silently; say so instead of counting it.
                        print('  send to %s:%d failed: %s' % (target, port, exc),
                              file=sys.stderr)
            # A NIC that misses the frame misses it entirely -- there is no
            # retransmit and no acknowledgement -- so they go out a few times,
            # spaced enough not to arrive as one burst.
            time.sleep(0.15)
        return sent
    finally:
        sock.close()


def has_static_arp(ip):
    """Is there an ARP entry we could frame a unicast packet with?"""
    out = subprocess.run(['arp', '-a', ip], capture_output=True, text=True).stdout
    return bool(re.search(r'([0-9a-fA-F]{2}(?:[:-][0-9a-fA-F]{2}){5})', out))


def is_up(ip, port):
    if port:
        try:
            with socket.create_connection((ip, port), timeout=1.5):
                return True
        except OSError:
            return False
    done = subprocess.run(['ping', '-n', '1', '-w', '1000', ip],
                          capture_output=True, text=True)
    return done.returncode == 0 and 'TTL=' in done.stdout


def main():
    cfg = load_config()
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--mac', default=os.environ.get('XPBOX_MAC') or cfg.get('mac'),
                    help='target MAC. Remembered in %s once given.' % os.path.basename(CONFIG))
    ap.add_argument('--ip', default=cfg.get('ip', DEFAULT_IP),
                    help='the box, for --learn and --wait (default %(default)s)')
    ap.add_argument('--broadcast', default=cfg.get('broadcast', DEFAULT_BROADCAST),
                    help='subnet broadcast to send to (default %(default)s)')
    ap.add_argument('--source', default=cfg.get('source', DEFAULT_SOURCE),
                    help='local IP to send FROM, which picks the adapter '
                         '(default %(default)s; "" to let the OS choose)')
    ap.add_argument('--repeat', type=int, default=3,
                    help='how many times to send it (default %(default)s)')
    ap.add_argument('--unicast', action='store_true',
                    help='ALSO send to --ip, for a NIC that only accepts a magic '
                         'packet addressed to its own MAC. Needs a static ARP entry '
                         '(see send_magic); the script says so if one is missing.')
    ap.add_argument('--learn', action='store_true',
                    help='resolve the MAC via ARP and save it. The box must be ON.')
    ap.add_argument('--wait', type=int, metavar='SECONDS', default=0,
                    help='after sending, poll until it answers, or give up')
    ap.add_argument('--wait-port', type=int, default=DEFAULT_WAIT_PORT,
                    help='TCP port that means "ready" (default %(default)s, rpdxp). '
                         '0 falls back to ICMP, which answers earlier than it should.')
    args = ap.parse_args()

    if args.learn:
        print('learning the MAC for %s (it has to be ON for this)...' % args.ip)
        found = arp_lookup(args.ip)
        if not found:
            print('  no ARP entry -- the box is not reachable at %s right now.' % args.ip,
                  file=sys.stderr)
            print('  Boot it, confirm `ping %s` answers, then run --learn again.' % args.ip,
                  file=sys.stderr)
            return 1
        cfg.update({'mac': pretty_mac(found), 'ip': args.ip,
                    'broadcast': args.broadcast, 'source': args.source})
        save_config(cfg)
        print('  %s is %s -- saved to %s' % (args.ip, pretty_mac(found), CONFIG))
        return 0

    if not args.mac:
        print('No MAC address known for the box, and Wake-on-LAN cannot work without one.',
              file=sys.stderr)
        print('', file=sys.stderr)
        print('Get it in any of these ways:', file=sys.stderr)
        print('  - while the box is on:  python %s --learn'
              % os.path.relpath(__file__), file=sys.stderr)
        print('  - on the box itself:    ipconfig /all   ("Physical Address")',
              file=sys.stderr)
        print('  - off the sticker on the NIC or the machine.', file=sys.stderr)
        print('', file=sys.stderr)
        print('Then: --mac 00:1A:2B:3C:4D:5E  (saved for next time), or set XPBOX_MAC.',
              file=sys.stderr)
        return 2

    mac = normalise_mac(args.mac)
    if cfg.get('mac') != pretty_mac(mac):
        cfg.update({'mac': pretty_mac(mac), 'ip': args.ip,
                    'broadcast': args.broadcast, 'source': args.source})
        save_config(cfg)

    # ICMP, deliberately, and NOT the readiness port. "Is the machine powered
    # on" and "is the machine usable" are different questions: rpdxp is a
    # service that has to be started, so a box that is up with rpdxp not
    # running would fail a port check and get sent a wake packet it does not
    # need. Harmless -- a running NIC ignores the frame -- but it would report
    # "sent" for a machine that was never asleep, which is a lie in a log.
    if is_up(args.ip, 0):
        print('%s already answers ICMP -- powered on, nothing to wake.' % args.ip)
        if args.wait_port and not is_up(args.ip, args.wait_port):
            print('  (but nothing is listening on %d -- if that is rpdxp, it needs starting)'
                  % args.wait_port)
        return 0

    unicast_ip = None
    if args.unicast:
        if has_static_arp(args.ip):
            unicast_ip = args.ip
        else:
            print('--unicast asked for, but there is no ARP entry for %s, so the OS '
                  'cannot' % args.ip, file=sys.stderr)
            print('  frame the packet. In an ELEVATED shell, once:', file=sys.stderr)
            print('    netsh interface ipv4 add neighbors "Ethernet 3" %s %s'
                  % (args.ip, pretty_mac(mac).replace(':', '-').lower()), file=sys.stderr)
            print('  Sending broadcast only.', file=sys.stderr)

    sent = send_magic(mac, args.broadcast, args.source, args.repeat, unicast_ip)
    print('sent %d magic packet(s) for %s to %s:%s%s'
          % (sent, pretty_mac(mac), args.broadcast, '/'.join(str(p) for p in WOL_PORTS),
             (' from %s' % args.source) if args.source else ''))

    if not args.wait:
        # Nothing acknowledges a magic packet, so "sent" is genuinely all this
        # can report. --wait is the only way to find out whether it worked.
        print('not waiting. Add --wait 120 to block until it answers.')
        return 0

    deadline = time.time() + args.wait
    what = 'tcp/%d' % args.wait_port if args.wait_port else 'ping'
    print('waiting up to %ds for %s (%s)...' % (args.wait, args.ip, what))
    while time.time() < deadline:
        if is_up(args.ip, args.wait_port):
            print('  up after %ds' % int(args.wait - (deadline - time.time())))
            return 0
        time.sleep(2)
    print('  still no answer after %ds.' % args.wait, file=sys.stderr)
    print('  If it never wakes, the cause is almost always at the far end -- see',
          file=sys.stderr)
    print('  the notes at the top of this file (BIOS, NIC power management, and',
          file=sys.stderr)
    print('  whether the box has been booted since it last lost power).', file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
