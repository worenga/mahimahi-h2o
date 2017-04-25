from StringIO import StringIO
from cgi import escape
import sys
import json
import os
import subprocess
from strip_chunked import decode
from mimetools import Message
from urlparse import urlparse


class ReplayApp:

    def _parse_push_strategy(self):

        if self.push_strategy_file == "noop":
            return
        with open(self.push_strategy_file) as fd:
            parsed_push_strategy = json.load(fd)
            if len(parsed_push_strategy) > 0:
                for push_strategy_for_host in parsed_push_strategy:
                    if 'push_host' in push_strategy_for_host.keys():
                        # print push_strategy_for_host
                        self.push_host.append(push_strategy_for_host['push_host'])
                        self.push_trigger_path.append(push_strategy_for_host['push_trigger'])
                        self.push_assets.append(push_strategy_for_host['push_resources'])
                    if 'hint_host' in push_strategy_for_host.keys():
                        self.hint_host.append(push_strategy_for_host['hint_host'])
                        self.hint_trigger_path.append(push_strategy_for_host['hint_trigger'])
                        self.hint_assets.append(push_strategy_for_host['hint_resources'])
                        self.hint_mimetype.append(push_strategy_for_host['hint_mimetype'])
                        mimetypes = []
                        for i,mimetype in enumerate(push_strategy_for_host['hint_mimetype']):
                                if 'image' in mimetype:
                                        mimetypes.append('image')
                                elif 'css' in mimetype:
                                        mimetypes.append('style')
                                elif 'javascript' in mimetype:
                                        mimetypes.append('script')
                                elif 'font' in mimetype:
                                        mimetypes.append('font')
                                else: #xhr
                                        print "WARNING: unmatched mimetype",mimetype, push_strategy_for_host['hint_resources'][i]
                                        mimetypes.append('')

                        self.hint_as_string.append(mimetypes)

    def _init_push_responsecache(self):
        env = os.environ
        for i,host in enumerate(self.push_host):
            self.push_cache.append([])
            for j,asset_name in enumerate(self.push_assets[i]):
                parsed = urlparse(asset_name)
                stripped = asset_name[len('https://'+parsed.netloc):]
                asset_name = stripped

                # print "Caching... ", asset_name, parsed
                # load responses into cache
                passed_env = dict()
                # remap for compat with replayserver
                passed_env['SERVER_PROTOCOL'] = "HTTP/1.1"
                passed_env['MAHIMAHI_CHDIR'] = env['WORKING_DIR']
                passed_env['MAHIMAHI_RECORD_PATH'] = env['RECORDING_DIR']
                passed_env['REQUEST_METHOD'] = 'GET' # we assume this is always get..
                passed_env['HTTPS'] = "1"
                passed_env['REQUEST_URI'] = asset_name
                passed_env['HTTP_HOST'] = host
                p = subprocess.Popen([env['REPLAYSERVER_FN']], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=passed_env)
                (replay_stdout, replay_stderr) = p.communicate()
                self.push_cache[i].append(replay_stdout)


    def __init__(self, push_strategy_file, replayserver_fn):
        self.push_strategy_file = push_strategy_file
        self.push_host = []
        self.push_trigger_path = []
        self.push_assets = []
        self.push_cache = []

        self.hint_host = []
        self.hint_trigger_path = []
        self.hint_assets = []
        self.hint_as_string = []
        self.hint_mimetype = []

        self._parse_push_strategy()
        self._init_push_responsecache()

    def __call__(self, environ, start_response):
        #print "call! " + environ['REQUEST_URI']
        #print environ
        hdrlist = []
        env = dict(environ)
        if env['HTTP_HOST'].endswith('.'):
           env['HTTP_HOST'] = env['HTTP_HOST'][:-1]

        cached_response = None
        is_push = False
        if env['REQUEST_METHOD'] == "GET":
            for u,host in enumerate(self.push_host):
                if host == env['HTTP_HOST']:
                  for v,push_resource in enumerate(self.push_assets[u]):
                        parsed = urlparse(push_resource)
                        stripped = push_resource[len('https://'+parsed.netloc):]
                        push_resource = stripped
                        if push_resource == env['REQUEST_URI']:
                            #print "pushing from cache..."
                            is_push = True
                            cached_response = self.push_cache[u][v]

        if cached_response is None:
            # print environ['REQUEST_URI'], "Not cached!"
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

            #if 'HTTP_USER_AGENT' in env.keys():
            #    passed_env['HTTP_USER_AGENT'] = env['HTTP_USER_AGENT']

            if env['wsgi.url_scheme'] == 'https':
                passed_env['HTTPS'] = "1"

            # shell=True,
            p = subprocess.Popen(
                [env['REPLAYSERVER_FN']], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=passed_env)

            (cached_response, replay_stderr) = p.communicate()


        response_header, response_body = cached_response.split('\r\n\r\n', 1)

        status_line, headers_alone = response_header.split('\r\n', 1)
        splitted_status = status_line.split(' ')

        # response_code = status_line[1]

        status_cleaned = ' '.join(splitted_status[1:])

        headers = Message(StringIO(headers_alone))

        #print env['REQUEST_METHOD'], env['REQUEST_URI']
       
        is_chunked = False
        corsfound = False
        hdrlist = []

        for key in headers.keys():
            if key == "transfer-encoding" and 'chunked' in headers[key]:
                is_chunked = True
            else:
                #Filter out any link header the site might emit.
                if key not in ['expires', 'date', 'last-modified','link']:
                    hdrlist.append((key.strip(), headers[key]))
                if key.lower() == 'access-control-allow-origin':
                    corsfound = True
        
        if not corsfound: 
            # this is required for font hinting...
            # note that we will not overwrite cors headers b/c
            # in some xhr situations * is not sufficient
            hdrlist.append(('Access-Control-Allow-Origin','*'))

        
        if not is_push and env['SERVER_PROTOCOL'] == "HTTP/2":
            for i, push_host_strategy in enumerate(self.push_host):
                if passed_env['HTTP_HOST'] == push_host_strategy:
                    if passed_env['REQUEST_URI'] == self.push_trigger_path[i]:
                        linkstr = ''
                        # TODO (bewo): is there any limitation?
                        for asset in self.push_assets[i]:
                            if linkstr != '':
                                 linkstr += ','
                            linkstr += '<' + asset + '>; rel=preload'
                        hdrlist.append(('x-extrapush', str(linkstr)))
                        print 'WILL PUSH: ' ,len(self.push_assets[i]) #//, ('x-extrapush', str(linkstr))
                        break

        if not is_push:
                for i, hint_host_strategy in enumerate(self.hint_host):
                    if passed_env['HTTP_HOST'] == hint_host_strategy:
                        if passed_env['REQUEST_URI'] == self.hint_trigger_path[i]:
                            linkstr = ''
                            for j, asset in enumerate(self.hint_assets[i]):
                                if linkstr != '':
                                   linkstr += ','
                                as_string = self.hint_as_string[i][j]
                                if as_string != '':
                                   as_string='; as='+as_string+''
                                if self.hint_as_string[i][j] == "font":
                                   as_string += ";crossorigin"
                                linkstr_to_append = '<'+asset + '>; rel=preload'+as_string+';type="'+self.hint_mimetype[i][j]+'"'
                                linkstr += linkstr_to_append
                            hdrlist.append(('link', str(linkstr)))
                            print 'WILL HINT: ' ,len(self.hint_assets[i]) #//, ('x-extrapush', str(linkstr))
                            break

        # print "start response! " + environ['REQUEST_URI']

        if is_chunked:
            # print "will decode chunked"
            start_response(status_cleaned, hdrlist)
            for chunk in decode(StringIO(response_body)):
                yield str(chunk)
        else:
            start_response(status_cleaned, hdrlist)
            yield response_body

        #start_response('200 OK', [('Content-Type', 'text/html')])
        # yield replay_stdout
