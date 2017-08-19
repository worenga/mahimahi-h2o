/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <random>
#include <thread>
#include <chrono>
#include <string>
#include <sstream>

#include "child_process.hh"
#include "system_runner.hh"
#include "exception.hh"
#include "socket.hh"

#include "config.h"

using namespace std;

void add_tc_rule( std::string device, uint64_t delay, uint64_t rate, uint64_t loss)
{

    //tc qdisc add dev docker1 root handle 1: htb default 12
    //tc class add dev docker1 parent 1:1 classid 1:12 htb rate 1mbit ceil 1mbit
    //tc qdisc add dev docker1 parent 1:12 netem delay 100ms
    
    //tc qdisc add dev ingress root netem delay 25ms rate 16000kbit loss 0%
    
    vector <string> args2 = {"/sbin/tc", "class", "add", "dev", device, "parent", "1:1", "classid", "1:12", "htb", "rate" };
    if(rate != 0)
    {
        std::ostringstream stringStream;
        stringStream << std::to_string(rate);
        stringStream << "kbit";
        args2.push_back(stringStream.str());
        args2.push_back("ceil");
        args2.push_back(stringStream.str());
    }
    else
    {
        std::ostringstream stringStream;
        stringStream << std::to_string(rate);
        args2.push_back(stringStream.str());
        args2.push_back("ceil");
        args2.push_back(stringStream.str());
    }
    
    //vector< string > args = { "/sbin/tc", "qdisc", "add", "dev", device, "root", "netem", "delay"};
    vector <string> args1 = {"/sbin/tc", "qdisc", "add", "dev", device, "root","handle","1:", "htb", "default", "12"};

    vector <string> args3 = {"/sbin/tc", "qdisc", "add", "dev", device, "parent", "1:12", "netem", "delay"};

    if(delay != 0)
    {
        std::ostringstream stringStream;
        stringStream << std::to_string(delay);
        stringStream << "ms";
        args3.push_back(stringStream.str());
    }
    else
    {
        std::ostringstream stringStream;
        stringStream << std::to_string(delay);
        args3.push_back(stringStream.str());
    }



    args3.push_back("loss");
    std::ostringstream stringStream;
    stringStream << std::to_string(loss);
    stringStream << "%";
    args3.push_back(stringStream.str());

    for(auto arg : args1){
        std::cout << arg << " ";
    }
    run(args1);

    std::cout << std::endl;

    for(auto arg : args2){
        std::cout << arg << " ";
    }
    run(args2);
    
    std::cout << std::endl;

    for(auto arg : args3){
        std::cout << arg << " ";
    }
    run ( args3 );
}
