from StringIO import StringIO
from cgi import escape
import sys
import json
import os
import subprocess
from strip_chunked import decode
from mimetools import Message


class ReplayApp:

    def _parse_push_strategy(self):

        with open(self.push_strategy_file) as fd:
            parsed_push_strategy = json.load(fd)

            if 'push_host' in parsed_push_strategy:
                self.push_host = parsed_push_strategy['push_host']

            if 'push_trigger_path' in parsed_push_strategy:
                self.push_trigger_path = parsed_push_strategy['push_trigger_path']

            if 'push_asset_list' in parsed_push_strategy:
                self.push_asset_list = parsed_push_strategy['push_asset_list']

    def __init__(self, push_strategy_file):
        self.push_strategy_file = push_strategy_file
        self.push_host = None
        self.push_trigger_path = None
        self.push_assets = None
        self._parse_push_strategy()

    def __call__(self, environ, start_response):
        env = dict(environ)

        passed_env = dict()

        # remap for compat with replayserver
        passed_env['MAHIMAHI_CHDIR'] = env['WORKING_DIR']
        passed_env['MAHIMAHI_RECORD_PATH'] = env['RECORDING_DIR']
        passed_env['REQUEST_METHOD'] = env['REQUEST_METHOD']
        passed_env['REQUEST_URI'] = env['REQUEST_URI']

        # env['SERVER_PROTOCOL'], is currently a hack to find the corresponding
        # h1 traces
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
        
        # response_code = status_line[1]

        status_cleaned = ' '.join(splitted_status[1:])

        headers = Message(StringIO(headers_alone))


        hdrlist = []
        if passed_env['HTTP_HOST'] == self.push_host:
            if passed_env['REQUEST_URI'] == self.push_trigger_path:
                linkstr = ''
                # TODO (bewo): is there any limitation?
                for asset in self.push_asset_list:
                    if linkstr != '':
                        linkstr += ','
                    linkstr += '<' + asset + '>; rel=preload'

                hdrlist.append(('x-extrapush', str(linkstr)))
                #print 'WILL PUSH', ('x-extrapush', str(linkstr))

        is_chunked = False
        
        for key in headers.keys():
            if key == "transfer-encoding" and 'chunked' in headers[key]:
                is_chunked = True
            else:
                if key not in ['expires', 'date', 'last-modified']:
                    hdrlist.append((key, headers[key]))

        if is_chunked:
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
