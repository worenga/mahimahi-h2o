/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <limits>
#include <iostream>
#include "shaping_queue.hh"
#include "timestamp.hh"

using namespace std;

void ShapingQueue::read_packet( const string & contents )
{
//    std::cout <<  contents << std::endl;
    packet_queue_.emplace( contents );
}

void ShapingQueue::write_packets( FileDescriptor & fd )
{
    while ( not packet_queue_.empty() ) {
        fd.write( packet_queue_.front() );
        packet_queue_.pop();
    }
}

unsigned int ShapingQueue::wait_time( void ) const
{
    return packet_queue_.empty() ? numeric_limits<uint16_t>::max() : 0;

}
