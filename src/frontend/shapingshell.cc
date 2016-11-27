/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <vector>
#include <string>

#include "shaping_queue.hh"
#include "util.hh"
#include "ezio.hh"
#include "shapedpacketshell.cc"

using namespace std;

int main( int argc, char *argv[] )
{
    try {
        /* clear environment while running as root */
        char ** const user_environment = environ;
        environ = nullptr;

        check_requirements( argc, argv );

        if ( argc < 4 ) {
            throw runtime_error( "Usage: " + string( argv[ 0 ] ) + " delay-milliseconds uplink-kbits downlink-kbits [command...]" );
        }

        const uint64_t delay_ms = myatoi( argv[ 1 ] );

        const uint64_t uplink_kbits_s = myatoi( argv[ 2 ] );

        const uint64_t downlink_kbits_s = myatoi( argv[ 3 ] );

        vector< string > command;

        if ( argc == 4 ) {
            command.push_back( shell_path() );
        } else {
            for ( int i = 4; i < argc; i++ ) {
                command.push_back( argv[ i ] );
            }
        }

        ShapedPacketShell<ShapingQueue> delay_shell_app( "shaping", user_environment );

        delay_shell_app.start_uplink( "[Shaping " + to_string( delay_ms ) + " ms] ",
                                      command,
                                      delay_ms,
                                      uplink_kbits_s,
                                      downlink_kbits_s,
                                      delay_ms );
        delay_shell_app.start_downlink( delay_ms,
                                        uplink_kbits_s,
                                        downlink_kbits_s,
                                        delay_ms);
        return delay_shell_app.wait_for_exit();
    } catch ( const exception & e ) {
        print_exception( e );
        return EXIT_FAILURE;
    }
}
