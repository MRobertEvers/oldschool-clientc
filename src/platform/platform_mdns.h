#ifndef SRC_PLATFORM_PLATFORM_MDNS_H
#define SRC_PLATFORM_PLATFORM_MDNS_H

/*
 * One-shot multicast-DNS (RFC 6762) resolution of a `.local` name to an IPv4
 * address.
 *
 * WHY THIS EXISTS. `.local` is not served by any unicast DNS server; it is
 * answered by the hosts themselves over UDP multicast. macOS resolves it inside
 * getaddrinfo (mDNSResponder is wired into the system resolver) and so does a
 * desktop Linux running nss-mdns, so on those hosts nothing here is ever
 * reached. ANDROID DOES NOT: bionic's getaddrinfo has no mDNS path at all, and
 * the only nameserver a phone has is the LAN router, which answers a `.local`
 * query with NXDOMAIN. A boot manifest naming a dev machine by its Bonjour name
 * therefore resolves on the developer's Mac and fails on the device, with the
 * client reporting nothing more useful than "Invalid address".
 *
 * This is a RESOLVER, not a browser: it asks one question (A record for one
 * name) and gives up quickly. There is no service enumeration, no PTR/SRV/TXT,
 * no cache and no background thread -- it is called on the boot path, where the
 * only acceptable cost of a miss is a few hundred milliseconds.
 *
 * Unavailable lanes (the web build has no UDP multicast) compile to a stub that
 * always reports failure, so the caller's fallback needs no #if of its own.
 */

#include <stdint.h>

/**
 * Non-zero when `host` ends in ".local" or ".local." (case-insensitive) --
 * i.e. when it is a name mDNS could answer and unicast DNS never will.
 */
int
PlatformMdns_IsLocalName(const char* host);

/**
 * Ask the local link for `host`'s A record.
 *
 * Returns 1 and stores the address in NETWORK byte order (assignable straight
 * to `struct in_addr.s_addr`) when a matching A record came back; returns 0 if
 * nothing answered within the budget, if the reply was malformed, or if this
 * build has no multicast sockets. Total wall cost of a miss is bounded by
 * TORIRS_MDNS_ATTEMPTS * TORIRS_MDNS_TIMEOUT_MS (see the .c) and is deliberately
 * kept under a second.
 */
int
PlatformMdns_ResolveIpv4(
    const char* host,
    uint32_t* out_addr_net);

#endif
