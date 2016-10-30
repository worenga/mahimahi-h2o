#!/usr/bin/env python
# -*- coding: UTF-8 -*-

from cgi import escape
import sys, os
from flup.server.fcgi import WSGIServer

def app(environ, start_response):
    

    ## TODO Set 
	""" setenv( "MAHIMAHI_CHDIR", config.working_dir, TRUE );
    setenv( "MAHIMAHI_RECORD_PATH", config.recording_dir, TRUE );
    setenv( "REQUEST_METHOD", request_method, TRUE );
    setenv( "REQUEST_URI", request_uri, TRUE );
    setenv( "SERVER_PROTOCOL", protocol, TRUE );
    setenv( "HTTP_HOST", http_host, TRUE );
    if ( user_agent != NULL ) {
        setenv( "HTTP_USER_AGENT", user_agent, TRUE );
    } """ 
    ## Then invoke replayserver
    ## Then Parse the headers
    ## Then Emit header

	start_response('200 OK', [('Content-Type', 'text/html')])

	# Then emit body
	
    ##TODO parse record protobuf, and replay recorded request
    yield '<h1>FastCGI Environment</h1>'
    yield str(environ)

WSGIServer(app).run()
