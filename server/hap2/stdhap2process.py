#! /usr/bin/env python
"""
  Copyright (C) 2015 Project Hatohol

  This file is part of Hatohol.

  Hatohol is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License, version 3
  as published by the Free Software Foundation.

  Hatohol is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with Hatohol. If not, see
  <http://www.gnu.org/licenses/>.
"""

import multiprocessing
import argparse
import logging
import time
import sys
import traceback

class StdHap2Process:

    DEFAULT_ERROR_SLEEP_TIME = 10

    def __init__(self):
        self.__error_sleep_time = self.DEFAULT_ERROR_SLEEP_TIME

        parser = argparse.ArgumentParser()

        choices = ("DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL")
        parser.add_argument("--log", dest="loglevel", choices=choices,
                            default="INFO")
        parser.add_argument("--amqp-broker", nargs=1, type=str,
                            default="localhost")
        parser.add_argument("--amqp-port", nargs=1, type=int, default=5672)
        parser.add_argument("--amqp-vhost", nargs=1, type=str,
                            default="hatohol")
        parser.add_argument("--amqp-queue", nargs=1, type=str,
                            default="stdhap2proccess-queue")
        parser.add_argument("--amqp-user", nargs=1, type=str,
                            default="hatohol")
        parser.add_argument("--amqp-password", nargs=1, type=str,
                            default="hatohol")

        self.__parser = parser

    def get_argument_parser(self):
        return self.__parser

    def create_main_plugin(self):
        assert False, "create_main_plugin shall be overriden"

    """
    An abstract method to create poller process.

    @return
    A class for poller. The class shall have run method.
    If this method returns None, no poller process is created.
    """
    def create_poller(self):
        return None

    def on_parsed_argument(self, args):
        pass

    def __setup_logging_level(self, args):
        numeric_level = getattr(logging, args.loglevel.upper(), None)
        if not isinstance(numeric_level, int):
            raise ValueError('Invalid log level: %s' % loglevel)
        logging.basicConfig(level=numeric_level)

    def __parse_argument(self):
        args = self.__parser.parse_args()
        self.__setup_logging_level(args)

        self.on_parsed_argument(args)
        return args

    def run(self):
        while True:
            try:
                self.__run()
            except KeyboardInterrupt:
                break
            except:
                (ty, val, tb) = sys.exc_info()
                logging.error("type: " + str(ty) + ", value: " + str(val) + "\n"
                              + traceback.format_exc())
            logging.info("Rerun after %d sec" % self.__error_sleep_time)
            time.sleep(self.__error_sleep_time)

    def __run(self):
        args = self.__parse_argument()

        main_req_que = multiprocessing.JoinableQueue()
        main_plugin = self.create_main_plugin(host=args.amqp_broker,
                                              port=args.amqp_port,
                                              vhost=args.amqp_vhost,
                                              queue_name=args.amqp_queue,
                                              user_name=args.amqp_user,
                                              user_password=args.amqp_password,
                                              main_request_queue=main_req_que)
        assert main_plugin is not None
        poller = self.create_poller()
        if poller is not None:
            poll_process = multiprocessing.Process(target=poller.run)
            poll_process.daemon = True
            poll_process.start()
        main_plugin.run()
