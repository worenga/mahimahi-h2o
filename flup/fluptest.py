#!/usr/bin/env python
# -*- coding: UTF-8 -*-

from cgi import escape
import sys
import os
import subprocess
import re
from mimetools import Message
from StringIO import StringIO

from flup.server.fcgi import WSGIServer
#from __future__ import unicode_literals

# https://github.com/simon-engledew/python-chunks/blob/master/chunks/__init__.py


def from_pattern(pattern, type, *args):
    def coerce(value):
        value = str(value)
        match = pattern.search(value)
        if match is not None:
            return type(match.group(1), *args)
        raise ValueError('unable to coerce "%s" into a %s' %
                         (value, type.__name__))
    return coerce

to_megabytes = lambda n: n * 1024 * 1024
to_hex = from_pattern(re.compile('([-+]?[0-9A-F]+)', re.IGNORECASE), int, 16)


def decode(fileobj, chunk_limit=to_megabytes(5)):
    hexsizestr = ''
    len_size_bytes = 0
    while True:
        firstbyte = fileobj.read(1)
        if firstbyte == '\r':
            nextbyte = fileobj.read(1)
            assert nextbyte == '\n'
        else:
            len_size_bytes += 1
            hexsizestr += firstbyte
            if len_size_bytes > 100:
                raise OverflowError('too many digits...')
            continue

        length = to_hex(hexsizestr)
        hexsizestr = ''
        len_size_bytes = 0
        
        if length > chunk_limit:
            raise OverflowError(
                'invalid chunk size of "%d" requested, max is "%d"' % (length, chunk_limit))

        value = fileobj.read(length)

        assert len(value) == length

        yield value

        tail = fileobj.read(2)

        if not tail:
            raise ValueError('missing \\r\\n after chunk')

        assert tail == '\r\n', 'unexpected characters "%s" after chunk' % tail

        if not length:
            return


def app(environ, start_response):

    env = dict(environ)

    passed_env = dict()
    # remap for compat
    passed_env['MAHIMAHI_CHDIR'] = env['WORKING_DIR']
    passed_env['MAHIMAHI_RECORD_PATH'] = env['RECORDING_DIR']
    passed_env['REQUEST_METHOD'] = env['REQUEST_METHOD']
    passed_env['REQUEST_URI'] = env['REQUEST_URI']
    # env['SERVER_PROTOCOL'], is currently a hack
    passed_env['SERVER_PROTOCOL'] = "HTTP/1.1"
    passed_env['HTTP_HOST'] = env['HTTP_HOST']
    if 'HTTP_USER_AGENT' in env.keys():
        passed_env['HTTP_USER_AGENT'] = env['HTTP_USER_AGENT']

    if env['wsgi.url_scheme'] == 'https':
        passed_env['HTTPS'] = "1"

    # shell=True,
    p = subprocess.Popen(
        [env['REPLAYSERVER_FN']], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=passed_env)

    (replay_stdout, replay_stderr) = p.communicate()

    response_header, response_body = replay_stdout.split('\r\n\r\n', 1)

    status_line, headers_alone = response_header.split('\r\n', 1)
    splitted_status = status_line.split(' ')
    response_code = status_line[1]
    status_cleaned = ' '.join(splitted_status[1:])

    headers = Message(StringIO(headers_alone))

    is_chunked = False
    hdrlist = []
    for key in headers.keys():
        if key == "transfer-encoding" and 'chunked' in headers[key]:
            is_chunked = True
        else:
            hdrlist.append((key, headers[key]))

    if is_chunked:
        if passed_env['REQUEST_METHOD'] == "HEAD" or str(response_code).startswith("1") or response_code == "204" or response_code == "304" or response_code == "302":
            hdrlist.append(('transfer-encoding','chunked'))
            start_response(status_cleaned, hdrlist)
            yield response_body
        else:
            decoded = StringIO()
            start_response(status_cleaned, hdrlist)
            for chunk in decode(StringIO(response_body)):
                decoded.write(chunk)
            yield decoded.getvalue()
    else:
        start_response(status_cleaned, hdrlist)
        yield response_body

    #start_response('200 OK', [('Content-Type', 'text/html')])
    # yield replay_stdout


WSGIServer(app).run()
