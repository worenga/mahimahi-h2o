/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef SHAPING_QUEUE_HH
#define SHAPING_QUEUE_HH

#include <queue>
#include <cstdint>
#include <string>

#include "file_descriptor.hh"

class ShapingQueue
{
private:
    uint64_t delay_ms_;
    std::queue<std::string> packet_queue_;
    /* release timestamp, contents */

public:
    ShapingQueue( const uint64_t & s_delay_ms ) : delay_ms_( s_delay_ms ), packet_queue_() {}

    void read_packet( const std::string & contents );

    void write_packets( FileDescriptor & fd );

    unsigned int wait_time( void ) const;

    bool pending_output( void ) const { return wait_time() <= 0; }

    static bool finished( void ) { return false; }
};

#endif /* SHAPING_QUEUE_HH */
