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

WebServer::WebServer( const Address & addr, const string & working_directory, const string & record_path, const std::string & push_strategy_file, const string & keyfile, const string & certfile, const std::string & mahimahi_root,
    unsigned webserver_custom_scheduler,
    unsigned webserver_custom_scheduler_push_streams,
    unsigned webserver_offset_compressed,
    unsigned webserver_custom_scheduler_offset,
    string custom_content_file,
    string custom_css_file,
    string webserver_custom_trigger_hostname,
    string webserver_custom_trigger_path
)
    : config_file_( "/tmp/replayshell_h2o_config" ),pid_file_("/tmp/replayshell_h2o_pidfile"),
      h2osrv(nullptr),
      moved_away_( false )
{

    std::string custom_envvars = "H2O_CUSTOM_CONTENT_FILE='"+custom_content_file + "' H2O_CRITICAL_CSS_FILE='"+custom_css_file+
    "' H2O_CUSTOM_SCHEDULER='"+to_string(webserver_custom_scheduler)+
    "' H2O_CUSTOM_TRIGGER_PATH='"+webserver_custom_trigger_path+
    "' H2O_CUSTOM_TRIGGER_HOST='"+webserver_custom_trigger_hostname+
    "' H2O_CUSTOM_OFFSET='"+to_string(webserver_custom_scheduler_offset)+"'";

    std::cout << custom_envvars << std::endl;

    pid_file_ = "/tmp/replayshell_apache_pid." + to_string( getpid() ) + "." + to_string( random() );
    config_file_.write( h2o_main_cfg );

    config_file_.write( "http2-custom-push-scheduler: "+to_string(webserver_custom_scheduler)+" \n");
    config_file_.write( "http2-custom-push-number-streams: "+to_string(webserver_custom_scheduler_push_streams)+" \n");
    config_file_.write( "http2-custom-push-index-offset: "+to_string(webserver_offset_compressed)+" \n");

    config_file_.write( "pid-file: " + pid_file_ + "\n" );
    config_file_.write( "setenv:\n" );
    config_file_.write( "  WORKING_DIR: \""+working_directory+"\"\n" );
    config_file_.write( "  RECORDING_DIR: \""+record_path+"\"\n" );
    config_file_.write( "  REPLAYSERVER_FN: \""+string(REPLAYSERVER)+"\"\n" );
    config_file_.write( "hosts:\n" );


    config_file_.write( std::string("  \"") + "*" + std::string("\":\n") );
    config_file_.write( "    listen:\n");
    config_file_.write( "      host: "+addr.ip()+"\n");
    config_file_.write( "      port: "+to_string(addr.port())+"\n");//+std::to_string(addr.port())+"\n");

    if(addr.port() != 80)
    {
        config_file_.write( "      ssl:\n");
        config_file_.write( "        key-file: "+keyfile+"\n");
        config_file_.write( "        certificate-file: "+certfile+"\n");
        config_file_.write( "        ocsp-update-interval: 0\n");
    }
    config_file_.write( "    paths:\n");
    config_file_.write( "      \"/\":\n");
    config_file_.write( "        fastcgi.spawn: \""+custom_envvars+" PUSH_STRATEGY_FILE='"+push_strategy_file+"' REPLAYSERVER_FN='"+string(REPLAYSERVER)+"' WORKING_DIR='"+working_directory+"' RECORDING_DIR='"+record_path+"' exec " + mahimahi_root + "/fcgi/FcgiHandler.py\"\n");

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
