/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SHAPED_PACKETSHELL
#define SHAPED_PACKETSHELL

#include <string>

#include "netdevice.hh"
#include "nat.hh"
#include "util.hh"
#include "address.hh"
#include "dns_proxy.hh"
#include "event_loop.hh"
#include "socketpair.hh"

template <class FerryQueueType>
class ShapedPacketShell
{
private:
    char ** const user_environment_;
    std::pair<Address, Address> egress_ingress;
    Address nameserver_;
    TunDevice egress_tun_;
    std::string egress_name;
    DNSProxy dns_outside_;
    NAT nat_rule_ {};

    std::pair<UnixDomainSocket, UnixDomainSocket> pipe_;

    EventLoop event_loop_;

    const Address & egress_addr( void ) { return egress_ingress.first; }
    const Address & ingress_addr( void ) { return egress_ingress.second; }

    class Ferry : public EventLoop
    {
    public:
        int loop( FerryQueueType & ferry_queue, FileDescriptor & tun, FileDescriptor & sibling );
    };

    Address get_mahimahi_base( void ) const;

public:
    ShapedPacketShell( const std::string & device_prefix, char ** const user_environment );

    template <typename... Targs>
    void start_uplink( const std::string & shell_prefix,
                       const std::vector< std::string > & command,
                       const uint64_t delay_ms,
                       const uint64_t uplink_kbits_s,
                       const uint64_t downlink_kbits_s,
                       Targs&&... Fargs );

    template <typename... Targs>
    void start_downlink(const uint64_t delay_ms,
                        const uint64_t uplink_kbits_s,
                        const uint64_t downlink_kbits_s,
                        Targs&&... Fargs );

    int wait_for_exit( void );

    ShapedPacketShell( const ShapedPacketShell & other ) = delete;
    ShapedPacketShell & operator=( const ShapedPacketShell & other ) = delete;
};

#endif
