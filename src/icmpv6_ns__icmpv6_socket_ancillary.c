/*  Copyright (C) 2015  Steve (unicornonair@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Send ICMPv6 neighbor solicitation.
// Create a ICMPv6 socket and change hoplimit using ancillary dtata.

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>            // memset()
#include <unistd.h>             // getpid(), sleep()

#include <sys/socket.h>         // socket(), AF_NET6, struct msghdr, struct cmsghdr
#include <netinet/in.h>         // struct sockaddr_in6, IPPROTO_ICMPV6, INET6_ADDRSTRLEN
#include <arpa/inet.h>          // inet_pton(), inet_ntop(), htons()
#include <netinet/icmp6.h>      // struct icmp6_hdr, struct nd_neighbor_solicit
#include <net/if.h>             // struct ifreq
#include <sys/ioctl.h>          // ioctl()

#include <errno.h>              // errno, perror()

int
main (int argc, char *argv[])
{
  int i, sd, hoplimit = 255;
  uint16_t seq = 0;
  // IPv6 address.
  struct sockaddr_in6 addr = { AF_INET6, 0, 0, 0, 0 };
  struct sockaddr_in6 mul_addr = { AF_INET6, 0, 0, 0, 0 };
  char hostname[INET6_ADDRSTRLEN];
  // ICMPv6 ns header.
  struct nd_neighbor_solicit ns;
  // ICMPv6 ns header option.
  uint8_t option[8];
  // Ancillary data.
  struct msghdr msghdr;
  struct cmsghdr *cmsghdr;
  struct iovec iov[2];
  char cmsghdr_buf[CMSG_SPACE (sizeof (hoplimit))];
  // Interface
  struct ifreq ifr;

  // Usage.
  if (argc < 3) {
    printf ("Usage: %s <interface> <adddress>\n", argv[0]);
    exit (EXIT_SUCCESS);
  }

  // Create a socket to look up interface.
  if ((sd = socket (AF_INET6, SOCK_RAW, IPPROTO_IPV6)) < 0) {
    perror ("socket() ");
    exit (EXIT_FAILURE);
  }

  // Use ioctl() to find hardware address.
  memset (&ifr, 0, sizeof(struct ifreq));
  snprintf (ifr.ifr_name, sizeof(ifr.ifr_name), "%s", argv[1]);
  if (ioctl (sd, SIOCGIFHWADDR, &ifr) < 0) {
    perror ("ioctl() failed to find hardware address ");
    return (EXIT_FAILURE);
  }

  // Set the ICMPv6 ns header option.
  option[0] = 1;            // Option Type - "source link layer address" (Section 4.6 of RFC 4861)
  option[1] = 8 / 8;        // Option Length - units of 8 octets (RFC 4861)
  for (i=0; i<6; i++) {
    option[i+2] = (uint8_t) ifr.ifr_hwaddr.sa_data[i];
  }
  printf ("Hardware address is %02X:%02X:%02X:%02X:%02X:%02X\n", option[2], option[3], option[4], option[5], option[6], option[7]);

  // Use ioctl() to find interface index.
  memset (&ifr, 0, sizeof(struct ifreq));
  snprintf (ifr.ifr_name, sizeof(ifr.ifr_name), "%s", argv[1]);
  if (ioctl (sd, SIOCGIFINDEX, &ifr) < 0) {
    perror ("ioctl() failed to find interface ");
    return (EXIT_FAILURE);
  }
  printf ("Index for interface %s is %i\n", argv[1], ifr.ifr_ifindex);

  // Close sd.
  close (sd);

  // Create a ICMPv6 raw socket.
  // The kernel will fill the IPv6 header automatically.
  if ((sd = socket (AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)) < 0) {
    perror ("socket() ");
    exit (EXIT_FAILURE);
  }

  // Convert the hostname to network bytes.
  if (inet_pton (AF_INET6, argv[2], &(addr.sin6_addr)) < 0) {
    perror ("inet_pton() ");
    exit (EXIT_FAILURE);
  }

  // Copy the addr to ns_addr and covert unicast address to ns multicast address.
  memcpy (&mul_addr, &addr, sizeof (struct sockaddr_in6));
  mul_addr.sin6_addr.s6_addr[0]= 255;
  mul_addr.sin6_addr.s6_addr[1]=2;
  for (i=2; i<11; i++) {
    mul_addr.sin6_addr.s6_addr[i] = 0;
  }
  mul_addr.sin6_addr.s6_addr[11]=1;
  mul_addr.sin6_addr.s6_addr[12]=255;

  // Bind the interface using setsockopt()
  if (setsockopt (sd, SOL_SOCKET, SO_BINDTODEVICE, (void*)&ifr, sizeof(struct ifreq)) < 0) {
    perror ("setsockopt() failed to bind interface ");
    return (EXIT_FAILURE);
  }

  // Send ICMPv6 neighbor solicitation.

  // 1. Construct ICMPv6 header and payload.
  memset (&ns, 0, sizeof(ns));

  // Set header type.
  ns.nd_ns_hdr.icmp6_type = ND_NEIGHBOR_SOLICIT;  // 135 (RFC 4861)

  // Code is 0 for ns.
  ns.nd_ns_hdr.icmp6_code = 0;

  // When you create a ICMPv6 raw socket, the kernel will calculate and
  // insert the ICMPv6 checksum automatically.
  ns.nd_ns_hdr.icmp6_cksum = htons(0);

  // The reserved must be 0.
  ns.nd_ns_reserved = htonl(0);
  ns.nd_ns_target = addr.sin6_addr;

  // 2. Assign msghdr's field "msg_name" to multicast destination address.
  memset (&msghdr, 0, sizeof(msghdr));
  msghdr.msg_name = &mul_addr;
  msghdr.msg_namelen = sizeof (mul_addr);

  // 3. Assign the packet to the io vector.
  iov[0].iov_base = &ns;
  iov[0].iov_len = sizeof (struct nd_neighbor_solicit);

  iov[1].iov_base = &option;
  iov[1].iov_len = sizeof (option);

  msghdr.msg_iov = iov;
  msghdr.msg_iovlen = 2;

  // 4. Set the TTL in cmsghdr.
  memset (&cmsghdr_buf, 0, sizeof (cmsghdr_buf));
  msghdr.msg_control = cmsghdr_buf;
  msghdr.msg_controllen = sizeof (cmsghdr_buf);

  // Set the time-to-live value 255.
  cmsghdr = CMSG_FIRSTHDR (&msghdr);
  cmsghdr->cmsg_level = IPPROTO_IPV6;
  cmsghdr->cmsg_type = IPV6_HOPLIMIT;
  cmsghdr->cmsg_len = CMSG_LEN (sizeof (hoplimit));
  *(int *)CMSG_DATA (cmsghdr) = hoplimit;

  // 5. Send the message.
  if (sendmsg (sd, &msghdr, 0) <= 0) {
      perror ("sendmsg() ");
      exit (EXIT_FAILURE);
  }

  // Close sd.
  close (sd);

  // Use "tcpdump -nnvvvXS -i eth1 -s0 icmp6" to check neighbor solicitation progress.

}