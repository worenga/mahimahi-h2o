#!/usr/bin/env python
# -*- coding: UTF-8 -*-

from cgi import escape
import sys
import os
import subprocess

from mimetools import Message
from StringIO import StringIO

from flup.server.fcgi import WSGIServer
#from __future__ import unicode_literals

def app(environ, start_response):

    env = dict(environ)

    passed_env = dict()
    #remap for compat
    passed_env['MAHIMAHI_CHDIR'] = env['WORKING_DIR']
    passed_env['MAHIMAHI_RECORD_PATH'] = env['RECORDING_DIR']
    passed_env['REQUEST_METHOD'] = env['REQUEST_METHOD']
    passed_env['REQUEST_URI'] = env['REQUEST_URI']
    passed_env['SERVER_PROTOCOL'] = "HTTP/1.1" # env['SERVER_PROTOCOL'], is currently a hack
    passed_env['HTTP_HOST'] = env['HTTP_HOST']
    if 'HTTP_USER_AGENT' in env.keys():
        passed_env['HTTP_USER_AGENT'] = env['HTTP_USER_AGENT']
    
    if env['wsgi.url_scheme'] == 'https':
        passed_env['HTTPS'] = "1"


    #shell=True,
    p = subprocess.Popen([env['REPLAYSERVER_FN']],stdout=subprocess.PIPE, stderr=subprocess.PIPE,env=passed_env)
    
    (replay_stdout,replay_stderr) = p.communicate()
    
    
    response_header, response_body = replay_stdout.split('\r\n\r\n', 1)

    status_line, headers_alone = response_header.split('\r\n', 1)
    status_cleaned = ' '.join(status_line.split(' ')[1:])

    headers = Message(StringIO(headers_alone))
    
    hdrlist = []
    for key in headers.keys():
        hdrlist.append((key,headers[key]))

    start_response(status_cleaned, hdrlist)
    yield response_body
    #start_response('200 OK', [('Content-Type', 'text/html')])
    #yield replay_stdout


WSGIServer(app).run()
