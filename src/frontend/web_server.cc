/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string>
#include <iostream>
#include <unistd.h>

#include "web_server.hh"
#include "h2o_configuration.hh"
#include "system_runner.hh"
#include "config.h"
#include "util.hh"
#include "exception.hh"
#include "child_process.hh"

using namespace std;

WebServer::WebServer( const Address & addr, const string & working_directory, const string & record_path )
    : config_file_( "/tmp/replayshell_h2o_config" ),pid_file_("/tmp/replayshell_h2o_pidfile"),
      h2osrv(nullptr),
      moved_away_( false )
{
    pid_file_ = "/tmp/replayshell_apache_pid." + to_string( getpid() ) + "." + to_string( random() );
    config_file_.write( h2o_main_cfg );
    std::cout << getuid() << " " << getgid() << std::endl;
    //config_file_.write( "user: " + std::string(getlogin() ) + "\n" );
    config_file_.write( "pid-file: " + pid_file_ + "\n" );
    config_file_.write( "setenv:\n" );
    config_file_.write( "  WORKING_DIR: \""+working_directory+"\"\n" );
    config_file_.write( "  RECORDING_DIR: \""+record_path+"\"\n" );
    config_file_.write( "  REPLAYSERVER_FN: \""+string(REPLAYSERVER)+"\"\n" );
    config_file_.write( "hosts:\n" );
    config_file_.write( std::string("  \"") + addr.str() + std::string("\":\n") );
    config_file_.write( "    listen:\n");
    config_file_.write( "      host: "+addr.ip()+"\n");
    config_file_.write( "      port: "+to_string(addr.port())+"\n");//+std::to_string(addr.port())+"\n");
    config_file_.write( "      ssl:\n");
    config_file_.write( "        key-file: "+h2o_ssl_config_keyfile+"\n");
    config_file_.write( "        certificate-file: "+h2o_ssl_config_certfile+"\n");
    config_file_.write( "        ocsp-update-interval: 0\n");
    config_file_.write( "    paths:\n");
    config_file_.write( "      \"/\":\n");
    config_file_.write( "        fastcgi.spawn: \"exec /home/bewo/Projects/mahimahi/flup/fluptest.py\"\n");
//    config_file_.write( "        mruby.handler: |\n");
//    config_file_.write( "          Proc.new do |env|\n");
//    config_file_.write( "            [200, {'content-type' => 'text/plain'}, [\"Peter!\\n\"]]\n");
//    config_file_.write( "          end\n");
    //run( {"/sbin/ifconfig"} );
    //run( {"/sbin/setcap","'cap_net_bind_service=+ep'","/usr/local/bin/h2o"} );
    //run( {"/bin/cat", config_file_.name()} );

    vector< string > args = { "/usr/local/bin/h2o", "-c", config_file_.name() };
    h2osrv = new ChildProcess( "h2o", [&] () { return ezexec( args ); }, false, SIGTERM );
}

WebServer::~WebServer()
{
    if ( moved_away_ ) { return; }

    try {
        delete h2osrv;
        //run( { "killall", "h2o" });
    } catch ( const exception & e ) { /* don't throw from destructor */
        print_exception( e );
    }
}

WebServer::WebServer( WebServer && other )
    : config_file_( move( other.config_file_ ) ),
      pid_file_( move( other.pid_file_ ) ),
      h2osrv(move(other.h2osrv)),
      moved_away_( false )
{
    other.h2osrv = nullptr;
    other.moved_away_ = true;
}
