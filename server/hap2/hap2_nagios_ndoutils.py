#!/usr/bin/env python
# coding: UTF-8
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

import sys
import MySQLdb
import time
import haplib
import standardhap
import logging

class Common:

    STATE_OK = 0
    STATE_WARNING = 1
    STATE_CRITICAL = 2

    STATUS_MAP = {STATE_OK: "OK", STATE_WARNING: "NG", STATE_CRITICAL: "NG"}
    SEVERITY_MAP = {
        STATE_OK: "INFO", STATE_WARNING: "WARNING", STATE_CRITICAL: "CRITICAL"}
    EVENT_TYPE_MAP = {
        STATE_OK: "GOOD", STATE_WARNING: "BAD", STATE_CRITICAL: "BAD"}


    def __init__(self):
        self.__db = None
        self.__db_server = "localhost"
        self.__db_name = "ndoutils"
        self.__db_user = "root"
        self.__db_passwd = ""

    def close_connection(self):
        if self.__cursor is not None:
            self.__cursor.close()
            self.__cursor = None
        if self.__db is not None:
            self.__db.close()
            self.__db = None

    def ensure_connection(self):
        if self.__db is not None:
            return
        try:
            self.__db = MySQLdb.connect(host=self.__db_server,
                                        db=self.__db_name,
                                        user=self.__db_user,
                                        passwd=self.__db_passwd)
            self.__cursor = self.__db.cursor()
        except MySQLdb.Error as (errno, msg):
            logging.error('MySQL Error [%d]: %s' % (errno, msg))
            raise haplib.HandledException

    def collect_hosts_and_put(self):
        t0 = "nagios_hosts"
        t1 = "nagios_objects"
        sql = "SELECT " \
              + "%s.host_object_id, " % t0 \
              + "%s.display_name, " % t0 \
              + "%s.name1 " % t1 \
              + "FROM %s INNER JOIN %s " % (t0, t1) \
              + "ON %s.host_object_id=%s.object_id" % (t0, t1)
        self.__cursor.execute(sql)
        result = self.__cursor.fetchall()
        hosts = []
        for row in result:
            host_id, name, name1 = row
            hosts.append({"hostId":name1, "hostName":name})
        self.put_hosts(hosts)

    def collect_host_groups_and_put(self):
        t0 = "nagios_hostgroups"
        t1 = "nagios_objects"
        sql = "SELECT " \
              + "%s.hostgroup_id, " % t0 \
              + "%s.alias, " % t0 \
              + "%s.name1 " % t1 \
              + "FROM %s INNER JOIN %s " % (t0, t1) \
              + "ON %s.hostgroup_id=%s.object_id" % (t0, t1)
        self.__cursor.execute(sql)
        result = self.__cursor.fetchall()
        groups = []
        for row in result:
            group_id, name, name1 = row
            groups.append({"groupId":name1, "groupName":name})
        self.put_host_groups(groups)

    def collect_host_group_membership_and_put(self):
        sql = "SELECT " \
              + "host_object_id, " \
              + "hostgroup_id " \
              + "FROM nagios_hostgroup_members"
        self.__cursor.execute(sql)
        result = self.__cursor.fetchall()
        members = {}
        for row in result:
            host_id, group_id = row
            members_for_host = members.get(host_id)
            if members_for_host is None:
                members[host_id] = []
            members[host_id].append(group_id)

        membership = []
        for host_id, group_list in members.items():
            membership.append({"hostId":host_id, "groupIds":group_list})
        self.put_host_group_membership(membership)

    def collect_triggers_and_put(self, fetch_id=None, host_ids=None):
        t0 = "nagios_services"
        t1 = "nagios_servicestatus"
        t2 = "nagios_hosts"
        sql = "SELECT " \
              + "%s.service_object_id, " % t0 \
              + "%s.current_state, " % t1 \
              + "%s.status_update_time, " % t1 \
              + "%s.output, " % t1 \
              + "%s.host_object_id, " % t2 \
              + "%s.display_name " % t2 \
              + "FROM %s INNER JOIN %s " % (t0, t1) \
              + "ON %s.service_object_id=%s.service_object_id " % (t0, t1) \
              + "INNER JOIN %s " % t2 \
              + "ON %s.host_object_id=%s.host_object_id" % (t0, t2)

        if host_ids is not None:
            if not self.__validate_host_ids(host_ids):
                # TODO: Send error
                return
            # We don't need to escape the string passed to the SQL statement
            # because only numbers are allowed for host IDs.
            in_cond = "','".join(host_ids)
            sql += " WHERE %s.host_object_id in ('%s')" % (t2, in_cond)

        # NOTE: The update time update in the output is updated every status
        #       check in Nagios even if the value is not changed.
        # TODO: So we should has the previous result and compare it here
        #       in order to improve performance.
        self.__cursor.execute(sql)
        result = self.__cursor.fetchall()

        triggers = []
        for row in result:
            (trigger_id, state, update_time, msg, host_id, host_name) = row

            hapi_status, hapi_severity = \
              self.__parse_status_and_severity(state)

            triggers.append({
                "triggerId": str(trigger_id),
                "status": hapi_status,
                "severity": hapi_severity,
                # TODO: take into acount the timezone
                "lastChangeTime": update_time.strftime("%Y%m%d%H%M%S"),
                "hostId": str(host_id),
                "hostName": host_name,
                "brief": msg,
                "extendedInfo": ""
            })
        # TODO: see issue #42 https://github.com/project-hatohol/HAPI-2.0-Specification/issues/42
        # TODO: update_type should UPDATED.
        update_type = "ALL"
        self.put_triggers(triggers, update_type=update_type, fetch_id=fetch_id)

    def collect_events_and_put(self):
        t0 = "nagios_statehistory"
        t1 = "nagios_services"
        t2 = "nagios_hosts"

        raw_last_info = self.get_cached_event_last_info()
        condition = ""
        if raw_last_info is not None:
            # last_info is inserted into the SQL statement and should be
            # strictly validated.
            last_info = self.__extract_validated_event_last_info(raw_last_info)
            if last_info is None:
                logging.error("Malformed last_info: '%s'",
                              str(raw_last_info))
                logging.error("Getting events was aborted.")
                return
            condition = "WHERE %s.statehistory_id>%s" % (t0, last_info)

        sql = "SELECT " \
              + "%s.statehistory_id, " % t0 \
              + "%s.state, " % t0 \
              + "%s.state_time, " % t0 \
              + "%s.output, " % t0 \
              + "%s.service_object_id, " % t1 \
              + "%s.host_object_id, " % t2 \
              + "%s.display_name " % t2 \
              + "FROM %s INNER JOIN %s " % (t0, t1) \
              + "ON %s.statehistory_id=%s.service_object_id " % (t0, t1) \
              + "INNER JOIN %s " % t2 \
              + "ON %s.host_object_id=%s.host_object_id " % (t1, t2) \
              + condition
        self.__cursor.execute(sql)
        result = self.__cursor.fetchall()

        events = []
        for row in result:
            (event_id, state, event_time, msg, \
             trigger_id, host_id, host_name) = row

            hapi_event_type = self.EVENT_TYPE_MAP.get(state)
            if hapi_event_type is None:
                log.warning("Unknown status: " + str(status))
                hapi_event_type = "UNKNOWN"

            hapi_status, hapi_severity = \
              self.__parse_status_and_severity(state)

            events.append({
                "eventId": str(event_id),
                "time": event_time.strftime("%Y%m%d%H%M%S"),
                "type": hapi_event_type,
                "triggerId": trigger_id,
                "status": hapi_status,
                "severity": hapi_severity,
                "hostId": str(host_id),
                "hostName": host_name,
                "brief": msg,
                "extendedInfo": ""
            })
        self.put_events(events)

    def __extract_validated_event_last_info(self, last_info):
        event_id = None
        try:
            event_id = int(last_info)
        except:
            pass
        if event_id <= 0:
            event_id = None
        return event_id

    def __parse_status_and_severity(self, status):
        hapi_status = self.STATUS_MAP.get(status)
        if hapi_status is None:
            log.warning("Unknown status: " + str(status))
            hapi_status = "UNKNOWN"

        hapi_severity = self.SEVERITY_MAP.get(status)
        if hapi_severity is None:
            log.warning("Unknown status: " + str(status))
            hapi_severity = "UNKNOWN"

        return (hapi_status, hapi_severity)

    def __validate_host_ids(self, host_ids):
        # TODO: implement
        return True


class Hap2NagiosNDOUtilsPoller(haplib.BasePoller, Common):

    def __init__(self, *args, **kwargs):
        haplib.BasePoller.__init__(self, *args, **kwargs)
        Common.__init__(self)

    def poll_setup(self):
        self.ensure_connection()

    def poll_hosts(self):
        self.collect_hosts_and_put()

    def poll_host_groups(self):
        self.collect_host_groups_and_put()

    def poll_host_group_membership(self):
        self.collect_host_group_membership_and_put()

    def poll_triggers(self):
        self.collect_triggers_and_put()

    def poll_events(self):
        self.collect_events_and_put()

    def on_aborted_poll(self):
        self.reset()
        self.close_connection()


class Hap2NagiosNDOUtilsMain(haplib.BaseMainPlugin, Common):
    def __init__(self, *args, **kwargs):
        haplib.BaseMainPlugin.__init__(self, kwargs["transporter_args"])
        Common.__init__(self)

    def hap_fetch_triggers(self, params, request_id):
        self.ensure_connection()
        fetch_id = params["fetchId"]
        host_ids = params["hostIds"]
        self.collect_triggers_and_put(fetch_id, host_ids)


class Hap2NagiosNDOUtils(standardhap.StandardHap):
    def create_main_plugin(self, *args, **kwargs):
        return Hap2NagiosNDOUtilsMain(*args, **kwargs)

    def create_poller(self, *args, **kwargs):
        return Hap2NagiosNDOUtilsPoller(self, *args, **kwargs)


if __name__ == '__main__':
    hap = Hap2NagiosNDOUtils()
    hap()
