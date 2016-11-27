/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#include <vector>
#include <set>
#include <string>
#include <net/route.h>
#include <fcntl.h>
#include <cstdlib>
#include <fstream>
#include <algorithm>

#include "util.hh"
#include "netdevice.hh"
#include "web_server.hh"
#include "system_runner.hh"
#include "socket.hh"
#include "event_loop.hh"
#include "temp_file.hh"
#include "http_response.hh"
#include "dns_server.hh"
#include "exception.hh"
#include "network_namespace.hh"
#include "server_certificate.hh"
#include "h2o_configuration.hh"


#include "http_record.pb.h"

#include "config.h"
#include "json.hpp"
using json = nlohmann::json;

using namespace std;

void add_dummy_interface( const string & name, const Address & addr )
{
    run( { IP, "link", "add", name, "type", "dummy" } );

    interface_ioctl( SIOCSIFFLAGS, name,
                     [] ( ifreq &ifr ) { ifr.ifr_flags = IFF_UP; } );
    interface_ioctl( SIOCSIFADDR, name,
                     [&] ( ifreq &ifr ) { ifr.ifr_addr = addr.to_sockaddr(); } );
}

inline bool file_exists(const std::string& filename)
{
    ifstream f(filename.c_str());
    return f.good();
}

int main( int argc, char *argv[] )
{
   try {
        /* clear environment */


        if (std::getenv("MAHIMAHI_ROOT") == NULL)
        {
            throw runtime_error( string( argv[ 0 ] ) + ": Please set `MAHIMAHI_ROOT` environment variable." );
        }

        //std::cout << "MAHIMAHI_ROOT: " <<  ::getenv("MAHIMAHI_ROOT") << std::endl;
        std::string mahimahi_root = std::getenv("MAHIMAHI_ROOT");

        char **user_environment = environ;
        environ = nullptr;

        check_requirements( argc, argv );

        if ( argc < 3 ) {
            throw runtime_error( "Usage: " + string( argv[ 0 ] ) + " directory push_strategy_file ca_merge_policy [command...]" );
        }

        /* clean directory name */
        string directory = argv[ 1 ];

        if ( directory.empty() ) {
            throw runtime_error( string( argv[ 0 ] ) + ": directory name must be non-empty" );
        }

        /* make sure directory ends with '/' so we can prepend directory to file name for storage */
        if ( directory.back() != '/' ) {
            directory.append( "/" );
        }

        /* get working directory */
        const string working_directory { get_working_directory() };


        string push_strategy_file = argv[ 2 ];

        if ( push_strategy_file.empty() ) {
            throw runtime_error( string( argv[ 0 ] ) + ": push_strategy_file name must be non-empty" );
        }

        string merge_policy = argv[ 3 ];
        if ( merge_policy.empty() ) {
            throw runtime_error( string( argv[ 0 ] ) + ": ca_merge_policy name must be non-empty" );
        }

        int pos_command = 4;

        if (merge_policy != "same-ip" && merge_policy != "merge-all" && merge_policy != "same-ip-mergefile")
        {
            throw runtime_error( string( argv[ 0 ] ) + ": ca_merge_policy name must be either same-ip, same-ip-mergefile or merge-all" );
        }

        string mergefile_filename = "";
        if(merge_policy == "same-ip-mergefile")
        {
            
            mergefile_filename = argv[ pos_command ];
            pos_command++;
            if ( !file_exists(mergefile_filename) ) {
                throw runtime_error( string( argv[ 0 ] ) + ": mergefile not found" );
            }
        }


        
        if ( merge_policy.empty() ) {
            throw runtime_error( string( argv[ 0 ] ) + ": ca_merge_policy name must be non-empty" );
        }


        std::map<std::string,json> custom_scheduler_hosts;
    

        /* chdir to result of getcwd just in case */
        SystemCall( "chdir", chdir( working_directory.c_str() ) );

        const string netns_name = string("mahimahi.") +  to_string( getpid() );

        NetworkNamespace network_namespace;

        /* Setup our own resolvconf with nameserver 8.8.8.8 */
        network_namespace.create_resolvconf( "8.8.8.8" );

        /* Switch to the newly created network namespace */
        network_namespace.enter();

        /* what command will we run inside the container? */
        vector< string > command;
        if ( argc == pos_command ) {
            command.push_back( shell_path() );
        } else {

            for ( int i = pos_command; i < argc; i++ ) {
                command.push_back( argv[ i ] );
            }
        }

        /* bring up localhost */
        interface_ioctl( SIOCSIFFLAGS, "lo",
                         [] ( ifreq &ifr ) { ifr.ifr_flags = IFF_UP; } );

        /* provide seed for random number generator used to create apache pid files */
        srandom( time( NULL ) );

        /* collect the IPs, IPs and ports, and hostnames we'll need to serve */
        set< Address > unique_ip;
        std::set<std::string> merge_same;
        set< Address > unique_ip_and_port;
        vector< pair< string, Address > > hostname_to_ip;
	
	    adjust_somaxconn();
	
        {
            TemporarilyUnprivileged tu;
            /* would be privilege escalation if we let the user read directories or open files as root */

            const vector< string > files = list_directory_contents( directory  );

            for ( const auto filename : files ) {
                FileDescriptor fd( SystemCall( "open", open( filename.c_str(), O_RDONLY ) ) );

                MahimahiProtobufs::RequestResponse protobuf;
                if ( not protobuf.ParseFromFileDescriptor( fd.fd_num() ) ) {
                    throw runtime_error( filename + ": invalid HTTP request/response" );
                }

                const Address address( protobuf.ip(), protobuf.port() );

                if (merge_policy != "same-ip" && unique_ip.size() < 1)
                {
                    unique_ip.emplace( address.ip(), 0 );
                }
                else if(merge_policy == "same-ip" || merge_policy == "same-ip-mergefile")
                {
                    unique_ip.emplace( address.ip(), 0 );
                }

                unique_ip_and_port.emplace( address );

                auto request = HTTPRequest( protobuf.request() );
                
                if(request.has_header("Host"))
                {
                    hostname_to_ip.emplace_back( request.get_header_value( "Host" ), address );
                    std::cout << request.get_header_value( "Host" ) << std::endl;
                }
                else if(request.has_header(":authority"))
                {
                    hostname_to_ip.emplace_back( request.get_header_value( ":authority" ), address );
                    //std::cout << request.get_header_value( ":authority" ) << std::endl;
                }
                else
                {
                    throw runtime_error( string( argv[ 0 ] ) + ": Host or authority not found!" );
                }

                

                //std::cout << "Hostname:" << HTTPRequest( protobuf.request() ).get_header_value( "Host" ) << "IP: " << address.ip() << std::endl;
            }
        }

        /* set up dummy interfaces */
        unsigned int interface_counter = 0;
        for ( const auto ip : unique_ip ) {
            add_dummy_interface( "sharded" + to_string( interface_counter ), ip );
            interface_counter++;
        }

        json push_strategy;
        //Parse Push strategy file if its not noop:
        if(push_strategy_file != "noop")
        {
            std::ifstream i(push_strategy_file);
            i >> push_strategy;
        }

        //
        CAEnvironment caenv(mahimahi_root);
        /* set up web servers */
        vector< WebServer > servers;
        vector< std::unique_ptr<ServerCertificate> > certificates;
        
        if(merge_policy == "same-ip")
        {
            for ( const auto ip_port : unique_ip_and_port )
            {

                std::string certfile = h2o_ssl_config_certfile;
                std::string keyfile = h2o_ssl_config_keyfile;

                std::string hostname_found = "";
                std::set<std::string> alts;
                //Get Hostnames that have the same ip:
                    for ( const auto mapping : hostname_to_ip )
                    {
                        
                        if(ip_port.ip() == mapping.second.ip())
                        {
                            if(hostname_found == "")
                            {
                                hostname_found = mapping.first;
                            }
                            else
                            {
                                alts.insert(mapping.first);
                            }
                        }
                    }

                if(hostname_found != "")
                {
                    std::unique_ptr<ServerCertificate> p1(new ServerCertificate(hostname_found,alts,caenv));  // p1 owns Foo
                    certificates.emplace_back(std::move(p1));
                    keyfile = certificates.back()->privatekey_file->name();
                    certfile = certificates.back()->certificate_file->name();
                }
                else
                {

                }

                unsigned webserver_custom_scheduler = 0;
                unsigned webserver_custom_scheduler_push_streams = 0;
                unsigned webserver_offset_compressed = 0;
                unsigned webserver_custom_scheduler_offset = 0;
                string webserver_custom_content_file = "";
                string webserver_custom_trigger_hostname = "";
                string webserver_custom_trigger_path = "";
                string webserver_critical_css_file = "";
                

                if(push_strategy.find("h2o_custom_scheduler") != push_strategy.end())
                {  
                    auto custom_schedulers = push_strategy["h2o_custom_scheduler"];
                    //Iterate over hosts with custom scheduler defined:
                    for (json::iterator it = custom_schedulers.begin(); it != custom_schedulers.end(); ++it) {
                        //custom_scheduler_hosts.insert(std::pair<std::string,json>(j["h2o_custom_scheduler"]['hostname'],j["h2o_custom_scheduler"]))
                        if(hostname_found == (*it)["hostname"] || alts.find((*it)["hostname"]) != alts.end())
                        {
                            webserver_custom_scheduler = 1;
                            webserver_custom_scheduler_push_streams = (*it)["critical_push_streams"];
                            webserver_offset_compressed = (*it)["offset_compressed"];
                            webserver_custom_scheduler_offset = (*it)["offset_index"];
                            webserver_custom_trigger_path = (*it)["index_trigger_path"];
                            webserver_custom_trigger_hostname = (*it)["hostname"];
                            webserver_custom_content_file = (*it)["content_file"];
                            webserver_critical_css_file = (*it)["critical_css_file"];
                            std::cout << "found for custom Scheduler config for " << *it << ip_port.ip() << std::endl;
                        }
                    }
                }

                servers.emplace_back( ip_port, working_directory, directory, push_strategy_file, keyfile, certfile, mahimahi_root , 
                    webserver_custom_scheduler,
                    webserver_custom_scheduler_push_streams,
                    webserver_offset_compressed,
                    webserver_custom_scheduler_offset,
                    webserver_custom_content_file,
                    webserver_critical_css_file,
                    webserver_custom_trigger_hostname,
                    webserver_custom_trigger_path
                );
            }
        }
        else if(merge_policy == "merge-all")
        {
            for ( const auto ip_port : unique_ip_and_port )
            {
                if(ip_port.ip() == unique_ip.begin()->ip())
                {
                    std::string certfile = h2o_ssl_config_certfile;
                    std::string keyfile = h2o_ssl_config_keyfile;

                    std::string hostname_found = "";
                    std::set<std::string> alts;
                    //Get Hostnames that have the same ip:
                    for ( const auto mapping : hostname_to_ip )
                    {
                        if(hostname_found == "")
                        {
                            hostname_found = mapping.first;
                        }
                        else
                        {
                            alts.insert(mapping.first);
                        }
                    }

                    if(hostname_found != "")
                    {
                        std::unique_ptr<ServerCertificate> p1(new ServerCertificate(hostname_found,alts,caenv));  // p1 owns Foo
                        certificates.emplace_back(std::move(p1));
                        keyfile = certificates.back()->privatekey_file->name();
                        certfile = certificates.back()->certificate_file->name();
                    }
                    else
                    {

                    }

                    unsigned webserver_custom_scheduler = 0;
                    unsigned webserver_custom_scheduler_push_streams = 0;
                    unsigned webserver_offset_compressed = 0;
                    unsigned webserver_custom_scheduler_offset = 0;
                    string webserver_custom_content_file = "";
                    string webserver_custom_trigger_hostname = "";
                    string webserver_custom_trigger_path = "";
                    string webserver_critical_css_file = "";
                    

                    if(push_strategy.find("h2o_custom_scheduler") != push_strategy.end())
                    {  
                        auto custom_schedulers = push_strategy["h2o_custom_scheduler"];
                        //Iterate over hosts with custom scheduler defined:
                        for (json::iterator it = custom_schedulers.begin(); it != custom_schedulers.end(); ++it) {
                            //custom_scheduler_hosts.insert(std::pair<std::string,json>(j["h2o_custom_scheduler"]['hostname'],j["h2o_custom_scheduler"]))
                            if(hostname_found == (*it)["hostname"] || alts.find((*it)["hostname"]) != alts.end())
                            {
                                
                                webserver_custom_scheduler = 1;
                                webserver_custom_scheduler_push_streams = (*it)["critical_push_streams"];
                                webserver_offset_compressed = (*it)["offset_compressed"];
                                webserver_custom_scheduler_offset = (*it)["offset_index"];
                                webserver_custom_trigger_path = (*it)["index_trigger_path"];
                                webserver_custom_trigger_hostname = (*it)["hostname"];
                                webserver_custom_content_file = (*it)["content_file"];
                                webserver_critical_css_file = (*it)["critical_css_file"];
                                std::cout << "found for custom Scheduler config for " << *it << ip_port.ip() << std::endl;

                            }
                        }
                    }

                    servers.emplace_back( ip_port, working_directory, directory, push_strategy_file, keyfile, certfile, mahimahi_root, 
                        webserver_custom_scheduler,
                        webserver_custom_scheduler_push_streams,
                        webserver_offset_compressed,
                        webserver_custom_scheduler_offset,
                        webserver_custom_content_file,
                        webserver_critical_css_file,
                        webserver_custom_trigger_hostname,
                        webserver_custom_trigger_path
                    );
                }
            }
        }
        else if(merge_policy == "same-ip-mergefile")
        {
            
            //Read Contents of Mergefile, create n->1 mapping       
            ifstream is(mergefile_filename.c_str());
            string str;
            while(getline(is, str))
            {
                merge_same.insert(str);
            }

            for ( const auto ip_port : unique_ip_and_port )
            {

                std::string certfile = h2o_ssl_config_certfile;
                std::string keyfile = h2o_ssl_config_keyfile;

                std::string hostname_found = "";
                std::set<std::string> alts;
                //Get Hostnames that have the same ip:
                for ( const auto mapping : hostname_to_ip )
                {
                    
                    if(ip_port.ip() == mapping.second.ip())
                    {
                        if(hostname_found == "")
                        {
                            hostname_found = mapping.first;
                        }
                        else
                        {
                            alts.insert(mapping.first);
                        }
                    }
                }

                if(merge_same.find(hostname_found) != merge_same.end())
                {
                    for(const auto merge : merge_same)
                    {
                        alts.insert(merge);    
                    }
                }
                
                for(const auto alt : alts)
                {
                    if(merge_same.find(alt) != merge_same.end())
                    {
                        for(const auto merge : merge_same)
                        {
                            alts.insert(merge);    
                        }
                    }
                }

                if(hostname_found != "")
                {
                    std::unique_ptr<ServerCertificate> p1(new ServerCertificate(hostname_found,alts,caenv));  // p1 owns Foo
                    certificates.emplace_back(std::move(p1));
                    keyfile = certificates.back()->privatekey_file->name();
                    certfile = certificates.back()->certificate_file->name();
                }
                else
                {

                }

                unsigned webserver_custom_scheduler = 0;
                unsigned webserver_custom_scheduler_push_streams = 0;
                unsigned webserver_offset_compressed = 0;
                unsigned webserver_custom_scheduler_offset = 0;
                string webserver_custom_content_file = "";
                string webserver_custom_trigger_hostname = "";
                string webserver_custom_trigger_path = "";
                string webserver_critical_css_file = "";
                

                if(push_strategy.find("h2o_custom_scheduler") != push_strategy.end())
                {  
                    auto custom_schedulers = push_strategy["h2o_custom_scheduler"];
                    //Iterate over hosts with custom scheduler defined:
                    for (json::iterator it = custom_schedulers.begin(); it != custom_schedulers.end(); ++it) {
                        //custom_scheduler_hosts.insert(std::pair<std::string,json>(j["h2o_custom_scheduler"]['hostname'],j["h2o_custom_scheduler"]))
                        if(hostname_found == (*it)["hostname"] || alts.find((*it)["hostname"]) != alts.end())
                        {
                            webserver_custom_scheduler = 1;
                            webserver_custom_scheduler_push_streams = (*it)["critical_push_streams"];
                            webserver_offset_compressed = (*it)["offset_compressed"];
                            webserver_custom_scheduler_offset = (*it)["offset_index"];
                            webserver_custom_trigger_path = (*it)["index_trigger_path"];
                            webserver_custom_trigger_hostname = (*it)["hostname"];
                            webserver_custom_content_file = (*it)["content_file"];
                            webserver_critical_css_file = (*it)["critical_css_file"];
                            std::cout << "found for custom Scheduler config for "  << hostname_found << " <-> " << (*it)["hostname"] << " " << *it << ip_port.ip() << std::endl;
                        }
                    }
                }

                servers.emplace_back( ip_port, working_directory, directory, push_strategy_file, keyfile, certfile, mahimahi_root , 
                    webserver_custom_scheduler,
                    webserver_custom_scheduler_push_streams,
                    webserver_offset_compressed,
                    webserver_custom_scheduler_offset,
                    webserver_custom_content_file,
                    webserver_critical_css_file,
                    webserver_custom_trigger_hostname,
                    webserver_custom_trigger_path
                );
            }

        }

        /* set up DNS server */
        UniqueFile dnsmasq_hosts( "/tmp/replayshell_hosts" );
        if(merge_policy == "same-ip")
        {
            for ( const auto mapping : hostname_to_ip ) {
                dnsmasq_hosts.write( mapping.second.ip() + " " + mapping.first + "\n" );
            }
        }else if(merge_policy == "merge-all"){
            for ( const auto mapping : hostname_to_ip ) {
                dnsmasq_hosts.write( unique_ip.begin()->ip() + " " + mapping.first + "\n" );
            }
        }else if(merge_policy == "same-ip-mergefile"){
            
            std::string merge_ip = "";

            for ( const auto mapping : hostname_to_ip ) {
                
                std::string to_mergeto = mapping.second.ip();

                if(merge_same.find(mapping.first) != merge_same.end())
                {
                    
                    if(merge_ip == "")
                    {
                        merge_ip = mapping.second.ip();
                    }

                    to_mergeto = merge_ip;
                }

                dnsmasq_hosts.write( to_mergeto + " " + mapping.first + "\n" );

            }
        }

        /* initialize event loop */
        EventLoop event_loop;

        /* create dummy interface for each nameserver */
        vector< Address > nameservers = all_nameservers();
        vector< string > dnsmasq_args = { "-H", dnsmasq_hosts.name() };

        for ( unsigned int server_num = 0; server_num < nameservers.size(); server_num++ ) {
            const string interface_name = "nameserver" + to_string( server_num );
            add_dummy_interface( interface_name, nameservers.at( server_num ) );
        }
        /* start dnsmasq */
        event_loop.add_child_process( start_dnsmasq( dnsmasq_args ) );

        /* start shell */
        event_loop.add_child_process( join( command ), [&]() {
                drop_privileges();

                /* restore environment and tweak bash prompt */
                environ = user_environment;
                prepend_shell_prefix( "[replay] " );

                return ezexec( command, true );
        } );

        return event_loop.loop();
    } catch ( const exception & e ) {
        print_exception( e );
        return EXIT_FAILURE;
    }
}
