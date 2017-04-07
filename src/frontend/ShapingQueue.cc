/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <limits>
#include <iostream>
#include "delay_queue.hh"
#include "timestamp.hh"

using namespace std;

void ShapingQueue::read_packet( const string & contents )
{
//    std::cout <<  contents << std::endl;
    packet_queue_.emplace( timestamp() + delay_ms_, contents );
}

void ShapingQueue::write_packets( FileDescriptor & fd )
{
    while ( (!packet_queue_.empty())
            && (packet_queue_.front().first <= timestamp()) ) {
        fd.write( packet_queue_.front().second );
        packet_queue_.pop();
    }
}

unsigned int ShapingQueue::wait_time( void ) const
{
    return packet_queue_.empty() ? numeric_limits<uint16_t>::max() : 0;
}
