#!/usr/bin/env python
# -*- coding: UTF-8 -*-

from cgi import escape
import os
from flup.server.fcgi import WSGIServer
from ReplayApp import ReplayApp

app = ReplayApp(os.environ['PUSH_STRATEGY_FILE'],os.environ['REPLAYSERVER_FN'])
WSGIServer(app,multiplexed=True).run()
