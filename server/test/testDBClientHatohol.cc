/*
 * Copyright (C) 2013 Project Hatohol
 *
 * This file is part of Hatohol.
 *
 * Hatohol is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * Hatohol is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Hatohol. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cppcutter.h>
#include <cutter.h>

#include "Hatohol.h"
#include "DBClientHatohol.h"
#include "Helpers.h"
#include "DBClientTest.h"
#include "Params.h"
#include "CacheServiceDBClient.h"

namespace testDBClientHatohol {

class TestHostResourceQueryOption : public HostResourceQueryOption {
public:
	string callGetServerIdColumnName(void) const
	{
		return getServerIdColumnName();
	}

	static
	string callMakeCondition(const ServerHostGrpSetMap &srvHostGrpSetMap,
				 const string &serverIdColumnName,
				 const string &hostGroupIdColumnName,
				 const string &hostIdColumnName,
				 uint32_t targetServerId = ALL_SERVERS,
				 uint64_t targetHostId = ALL_HOSTS)
	{
		return makeCondition(srvHostGrpSetMap,
				     serverIdColumnName,
				     hostGroupIdColumnName,
				     hostIdColumnName,
				     targetServerId, targetHostId);
	}
};

static const string serverIdColumnName = "server_id";
static const string hostGroupIdColumnName = "host_group_id";
static const string hostIdColumnName = "host_id";

static void addTriggerInfo(TriggerInfo *triggerInfo)
{
	DBClientHatohol dbHatohol;
	dbHatohol.addTriggerInfo(triggerInfo);
}
#define assertAddTriggerToDB(X) \
cut_trace(_assertAddToDB<TriggerInfo>(X, addTriggerInfo))

static string makeTriggerOutput(const TriggerInfo &triggerInfo)
{
	string expectedOut =
	  StringUtils::sprintf(
	    "%"PRIu32"|%"PRIu64"|%d|%d|%ld|%lu|%"PRIu64"|%s|%s\n",
	    triggerInfo.serverId,
	    triggerInfo.id,
	    triggerInfo.status, triggerInfo.severity,
	    triggerInfo.lastChangeTime.tv_sec,
	    triggerInfo.lastChangeTime.tv_nsec,
	    triggerInfo.hostId,
	    triggerInfo.hostName.c_str(),
	    triggerInfo.brief.c_str());
	return expectedOut;
}

template<class TResourceType, class TQueryOption>
struct AssertGetHostResourceArg {
	list<TResourceType> actualRecordList;
	TQueryOption option;
	UserIdType userId;
	uint32_t targetServerId;
	uint64_t targetHostId;
	DataQueryOption::SortOrder sortOrder;
	size_t maxNumber;
	uint64_t startId;
	HatoholErrorCode expectedErrorCode;
	vector<TResourceType*> authorizedRecords;
	vector<TResourceType*> expectedRecords;
	ServerHostGrpSetMap authMap;
	TResourceType *fixtures;
	size_t numberOfFixtures;

	AssertGetHostResourceArg(void)
	: userId(USER_ID_SYSTEM),
	  targetServerId(ALL_SERVERS),
	  targetHostId(ALL_HOSTS),
	  sortOrder(DataQueryOption::SORT_DONT_CARE),
	  maxNumber(0),
	  startId(0),
	  expectedErrorCode(HTERR_OK),
	  fixtures(NULL),
	  numberOfFixtures(0)
	{
	}

	virtual void fixupOption(void)
	{
		option.setUserId(userId);
		option.setTargetServerId(targetServerId);
		option.setTargetHostId(targetHostId);
	}

	virtual void fixup(void)
	{
		fixupOption();
		fixupAuthorizedMap();
		fixupAuthorizedRecords();
		fixupExpectedRecords();
	}

	virtual void fixupAuthorizedMap(void)
	{
		makeServerHostGrpSetMap(authMap, userId);
	}

	virtual bool isAuthorized(const TResourceType &record)
	{
		return ::isAuthorized(authMap, userId, record.serverId);
	}

	virtual void fixupAuthorizedRecords(void)
	{
		for (size_t i = 0; i < numberOfFixtures; i++) {
			if (isAuthorized(fixtures[i]))
				authorizedRecords.push_back(&fixtures[i]);
		}
	}

	virtual void fixupExpectedRecords(void)
	{
		for (size_t i = 0; i < authorizedRecords.size(); i++) {
			TResourceType *record = authorizedRecords[i];
			if (targetServerId != ALL_SERVERS) {
				if (record->serverId != targetServerId)
					continue;
			}
			if (targetHostId != ALL_HOSTS) {
				if (record->hostId != targetHostId)
					continue;
			}
			expectedRecords.push_back(record);
		}
	}

	virtual TResourceType &getExpectedRecord(size_t idx)
	{
		if (startId)
			idx += (startId - 1);
		if (sortOrder == DataQueryOption::SORT_DESCENDING)
			idx = (NumTestEventInfo - 1) - idx;
		cut_assert_true(idx < expectedRecords.size());
		return *expectedRecords[idx];
	}

	virtual void assertNumberOfRecords(void)
	{
		size_t expectedNum
		  = maxNumber && maxNumber < expectedRecords.size() ?
		    maxNumber : expectedRecords.size();
		cppcut_assert_equal(expectedNum, actualRecordList.size());
	}

	virtual string makeExpectedOutput(const TResourceType &record) = 0;

	virtual void assert(void)
	{
		assertNumberOfRecords();

		string expectedText;
		string actualText;
		typename list<TResourceType>::iterator it
		  = actualRecordList.begin();
		for (size_t i = 0; it != actualRecordList.end(); i++, ++it) {
			TResourceType &expectedRecord = getExpectedRecord(i);
			expectedText += makeExpectedOutput(expectedRecord);
			actualText += makeExpectedOutput(*it);
		}
		cppcut_assert_equal(expectedText, actualText);
	}
};

struct AssertGetTriggersArg
  : public AssertGetHostResourceArg<TriggerInfo, TriggersQueryOption>
{
	AssertGetTriggersArg(void)
	{
		fixtures = testTriggerInfo;
		numberOfFixtures = NumTestTriggerInfo;
	}

	string makeExpectedOutput(const TriggerInfo &triggerInfo)
	{
		return makeTriggerOutput(triggerInfo);
	}
};

static void _assertGetTriggers(AssertGetTriggersArg &arg)
{
	DBClientHatohol dbHatohol;
	arg.fixup();
	dbHatohol.getTriggerInfoList(arg.actualRecordList, arg.option);
	if (arg.expectedErrorCode != HTERR_OK)
		return;
	arg.assert();
}
#define assertGetTriggers(A) cut_trace(_assertGetTriggers(A))

static void _assertGetTriggersWithFilter(AssertGetTriggersArg &arg)
{
	// setup trigger data
	void test_addTriggerInfoList(void);
	test_addTriggerInfoList();

	if (arg.maxNumber)
		arg.option.setMaximumNumber(arg.maxNumber);
	arg.option.setSortOrder(arg.sortOrder);
	if (arg.startId)
		arg.option.setStartId(arg.startId);
	assertGetTriggers(arg);
}
#define assertGetTriggersWithFilter(ARG) \
cut_trace(_assertGetTriggersWithFilter(ARG))

static void _setupTestTriggerDB(void)
{
	for (size_t i = 0; i < NumTestTriggerInfo; i++)
		assertAddTriggerToDB(&testTriggerInfo[i]);
}
#define setupTestTriggerDB() cut_trace(_setupTestTriggerDB())

static void _assertGetTriggerInfoList(uint32_t serverId, uint64_t hostId = ALL_HOSTS)
{
	setupTestTriggerDB();
	AssertGetTriggersArg arg;
	arg.targetServerId = serverId;
	arg.targetHostId = hostId;
	assertGetTriggers(arg);
}
#define assertGetTriggerInfoList(SERVER_ID, ...) \
cut_trace(_assertGetTriggerInfoList(SERVER_ID, ##__VA_ARGS__))


static string makeEventOutput(const EventInfo &eventInfo)
{
	string output =
	  StringUtils::sprintf(
	    "%"PRIu32"|%"PRIu64"|%ld|%ld|%d|%u|%"PRIu64"|%s|%s\n",
	    eventInfo.serverId, eventInfo.id,
	    eventInfo.time.tv_sec, eventInfo.time.tv_nsec,
	    eventInfo.status, eventInfo.severity,
	    eventInfo.hostId,
	    eventInfo.hostName.c_str(),
	    eventInfo.brief.c_str());
	return output;
}


struct AssertGetEventsArg
  : public AssertGetHostResourceArg<EventInfo, EventsQueryOption>
{
	AssertGetEventsArg(void)
	{
		fixtures = testEventInfo;
		numberOfFixtures = NumTestEventInfo;
	}

	string makeExpectedOutput(const EventInfo &eventInfo)
	{
		return makeEventOutput(eventInfo);
	}
};

static void _assertGetEvents(AssertGetEventsArg &arg)
{
	DBClientHatohol dbHatohol;
	arg.fixup();
	assertHatoholError(
	  arg.expectedErrorCode,
	  dbHatohol.getEventInfoList(arg.actualRecordList, arg.option));
	if (arg.expectedErrorCode != HTERR_OK)
		return;
	arg.assert();
}
#define assertGetEvents(A) cut_trace(_assertGetEvents(A))

static void _assertGetEventsWithFilter(AssertGetEventsArg &arg)
{
	// setup event data
	void test_addEventInfoList(void);
	test_addEventInfoList();

	if (arg.maxNumber)
		arg.option.setMaximumNumber(arg.maxNumber);
	arg.option.setSortOrder(arg.sortOrder);
	if (arg.startId)
		arg.option.setStartId(arg.startId);
	assertGetEvents(arg);
}
#define assertGetEventsWithFilter(ARG) \
cut_trace(_assertGetEventsWithFilter(ARG))

static string makeItemOutput(const ItemInfo &itemInfo)
{
	string expectedOut =
	  StringUtils::sprintf(
	    "%"PRIu32"|%"PRIu64"|%"PRIu64"|%s|%ld|%lu|%s|%s|%s\n",
	    itemInfo.serverId, itemInfo.id,
	    itemInfo.hostId,
	    itemInfo.brief.c_str(),
	    itemInfo.lastValueTime.tv_sec,
	    itemInfo.lastValueTime.tv_nsec,
	    itemInfo.lastValue.c_str(),
	    itemInfo.prevValue.c_str(),
	    itemInfo.itemGroupName.c_str());
	return expectedOut;
}

struct AssertGetItemsArg
  : public AssertGetHostResourceArg<ItemInfo, ItemsQueryOption>
{
	AssertGetItemsArg(void)
	{
		fixtures = testItemInfo;
		numberOfFixtures = NumTestItemInfo;
	}

	string makeExpectedOutput(const ItemInfo &itemInfo)
	{
		return makeItemOutput(itemInfo);
	}
};

static void _assertGetItems(AssertGetItemsArg &arg)
{
	DBClientHatohol dbHatohol;
	arg.fixup();
	dbHatohol.getItemInfoList(arg.actualRecordList, arg.option);
	if (arg.expectedErrorCode != HTERR_OK)
		return;
	arg.assert();
}
#define assertGetItems(A) cut_trace(_assertGetItems(A))

static void _assertGetItemsWithFilter(AssertGetItemsArg &arg)
{
	// setup item data
	void test_addItemInfoList(void);
	test_addItemInfoList();

	if (arg.maxNumber)
		arg.option.setMaximumNumber(arg.maxNumber);
	arg.option.setSortOrder(arg.sortOrder);
	if (arg.startId)
		arg.option.setStartId(arg.startId);
	assertGetItems(arg);
}
#define assertGetItemsWithFilter(ARG) \
cut_trace(_assertGetItemsWithFilter(ARG))

void _assertItemInfoList(uint32_t serverId)
{
	DBClientHatohol dbHatohol;
	ItemInfoList itemInfoList;
	for (size_t i = 0; i < NumTestItemInfo; i++)
		itemInfoList.push_back(testItemInfo[i]);
	dbHatohol.addItemInfoList(itemInfoList);

	AssertGetItemsArg arg;
	arg.targetServerId = serverId;
	assertGetItems(arg);
}
#define assertItemInfoList(SERVER_ID) cut_trace(_assertItemInfoList(SERVER_ID))

static void _assertGetHostInfoList(uint32_t serverId)
{
	// We have to insert test trigger data in DB first. Because current
	// implementation of DBClientHatohol creates hostInfoList
	// from trigger table.
	// TODO: The implementation will be fixed in the future. The DB table
	//       for host will be added. After that, we will fix this setup.
	setupTestTriggerDB();

	HostInfoList actualHostList;
	HostInfoList expectedHostList;
	ServerIdHostIdMap svIdHostIdMap;
	DBClientHatohol dbHatohol;
	HostsQueryOption option(USER_ID_SYSTEM);
	option.setTargetServerId(serverId);
	dbHatohol.getHostInfoList(actualHostList, option);
	getTestHostInfoList(expectedHostList, serverId, &svIdHostIdMap);

	// comapre two lists
	cppcut_assert_equal(expectedHostList.size(), actualHostList.size());

	HostInfoListIterator actualHost = actualHostList.begin();
	for (; actualHost != actualHostList.end(); ++actualHost) {
		// server ID
		ServerIdHostIdMapIterator svIt =
		   svIdHostIdMap.find(actualHost->serverId);
		cppcut_assert_equal(true, svIt != svIdHostIdMap.end());

		// Host ID
		HostIdSet &hostIdSet = svIt->second;
		HostIdSetIterator hostIt = hostIdSet.find(actualHost->id);
		cppcut_assert_equal(true, hostIt != hostIdSet.end());

		// delete the element from svIdHostIdMap.
		// This is needed to check the duplication of hosts in
		// actualHostList
		hostIdSet.erase(hostIt);
		if (hostIdSet.empty())
			svIdHostIdMap.erase(svIt);
	}
	cppcut_assert_equal((size_t)0, svIdHostIdMap.size());
}
#define assertGetHostInfoList(SERVER_ID) \
cut_trace(_assertGetHostInfoList(SERVER_ID))

static void _assertGetNumberOfHostsWithStatus(bool status)
{
	setupTestTriggerDB();

	uint32_t serverId = testTriggerInfo[0].serverId;
	// TODO: should should give the appropriate host group ID after
	//       Hatohol support it.
	uint64_t hostGroupId = 0;

	DBClientHatohol dbHatohol;
	int expected = getNumberOfTestHostsWithStatus(serverId, hostGroupId,
	                                              status);
	int actual;
	if (status)
		actual  = dbHatohol.getNumberOfGoodHosts(serverId, hostGroupId);
	else
		actual  = dbHatohol.getNumberOfBadHosts(serverId, hostGroupId);
	cppcut_assert_equal(expected, actual);
}
#define assertGetNumberOfHostsWithStatus(ST) \
cut_trace(_assertGetNumberOfHostsWithStatus(ST))

static
void _assertTriggerInfo(const TriggerInfo &expect, const TriggerInfo &actual)
{
	cppcut_assert_equal(expect.serverId, actual.serverId);
	cppcut_assert_equal(expect.id, actual.id);
	cppcut_assert_equal(expect.status, actual.status);
	cppcut_assert_equal(expect.severity, actual.severity);
	cppcut_assert_equal(expect.lastChangeTime.tv_sec,
	                    actual.lastChangeTime.tv_sec);
	cppcut_assert_equal(expect.lastChangeTime.tv_nsec,
	                    actual.lastChangeTime.tv_nsec);
	cppcut_assert_equal(expect.hostId, actual.hostId);
	cppcut_assert_equal(expect.hostName, actual.hostName);
	cppcut_assert_equal(expect.brief, actual.brief);
}
#define assertTriggerInfo(E,A) cut_trace(_assertTriggerInfo(E,A))

static void _assertMakeCondition(const ServerHostGrpSetMap &srvHostGrpSetMap,
				 const string &expect,
				 uint32_t targetServerId = ALL_SERVERS,
				 uint64_t targetHostId = ALL_HOSTS)
{
	string cond = TestHostResourceQueryOption::callMakeCondition(
			srvHostGrpSetMap,
			serverIdColumnName,
			hostGroupIdColumnName,
			hostIdColumnName,
			targetServerId, targetHostId);
	cppcut_assert_equal(expect, cond);
}
#define assertMakeCondition(M, ...) \
  cut_trace(_assertMakeCondition(M, ##__VA_ARGS__))

static string makeExpectedConditionForUser(UserIdType userId)
{
	string exp;
	UserIdIndexMap userIdIndexMap;
	makeTestUserIdIndexMap(userIdIndexMap);
	UserIdIndexMapIterator it = userIdIndexMap.find(userId);
	if (it == userIdIndexMap.end())
		return DBClientHatohol::getAlwaysFalseCondition();

	ServerHostGrpSetMap srvHostGrpSetMap;
	const set<int> &indexes = it->second;
	set<int>::const_iterator jt = indexes.begin();
	for (; jt != indexes.end(); ++jt) {
		const AccessInfo &accInfo = testAccessInfo[*jt];
		srvHostGrpSetMap[accInfo.serverId].insert(accInfo.hostGroupId);
	}
	exp = TestHostResourceQueryOption::callMakeCondition(
		srvHostGrpSetMap,
		serverIdColumnName,
		hostGroupIdColumnName,
		hostIdColumnName);
	return exp;
}

void cut_setup(void)
{
	hatoholInit();
	deleteDBClientHatoholDB();
}

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------
void test_createDB(void)
{
	// remove the DB that already exists
	string dbPath = getDBPathForDBClientHatohol();

	// create an instance (the database will be automatically created)
	DBClientHatohol dbHatohol;
	cut_assert_exist_path(dbPath.c_str());

	// check the version
	string statement = "select * from _dbclient_version";
	string output = execSqlite3ForDBClientHatohol(statement);
	string expectedOut = StringUtils::sprintf
	                       ("%d|%d\n", DB_DOMAIN_ID_HATOHOL,
	                                   DBClientHatohol::HATOHOL_DB_VERSION);
	cppcut_assert_equal(expectedOut, output);
}

void test_createTableTrigger(void)
{
	const string tableName = "triggers";
	string dbPath = getDBPathForDBClientHatohol();
	DBClientHatohol dbHatohol;
	string command = "sqlite3 " + dbPath + " \".table\"";
	assertExist(tableName, executeCommand(command));

	// check content
	string statement = "select * from " + tableName;
	string output = execSqlite3ForDBClientHatohol(statement);
	string expectedOut = ""; // currently no data
	cppcut_assert_equal(expectedOut, output);
}

void test_addTriggerInfo(void)
{
	string dbPath = getDBPathForDBClientHatohol();

	// added a record
	TriggerInfo *testInfo = testTriggerInfo;
	assertAddTriggerToDB(testInfo);

	// confirm with the command line tool
	string cmd = StringUtils::sprintf(
	               "sqlite3 %s \"select * from triggers\"", dbPath.c_str());
	string result = executeCommand(cmd);
	string expectedOut = makeTriggerOutput(*testInfo);
	cppcut_assert_equal(expectedOut, result);
}

void test_getTriggerInfo(void)
{
	setupTestTriggerDB();
	int targetIdx = 2;
	TriggerInfo &targetTriggerInfo = testTriggerInfo[targetIdx];
	TriggerInfo triggerInfo;
	DBClientHatohol dbHatohol;
	cppcut_assert_equal(true,
	   dbHatohol.getTriggerInfo(
	      triggerInfo, targetTriggerInfo.serverId, targetTriggerInfo.id));
	assertTriggerInfo(targetTriggerInfo, triggerInfo);
}

void test_getTriggerInfoNotFound(void)
{
	setupTestTriggerDB();
	uint32_t invalidSvId = -1;
	uint32_t invalidTrigId = -1;
	TriggerInfo triggerInfo;
	DBClientHatohol dbHatohol;
	cppcut_assert_equal(false,
	   dbHatohol.getTriggerInfo(triggerInfo, invalidSvId, invalidTrigId));
}

void test_getTriggerInfoList(void)
{
	assertGetTriggerInfoList(ALL_SERVERS);
}

void test_getTriggerInfoListForOneServer(void)
{
	uint32_t targetServerId = testTriggerInfo[0].serverId;
	assertGetTriggerInfoList(targetServerId);
}

void test_getTriggerInfoListForOneServerOneHost(void)
{
	uint32_t targetServerId = testTriggerInfo[1].serverId;
	uint64_t targetHostId = testTriggerInfo[1].hostId;
	assertGetTriggerInfoList(targetServerId, targetHostId);
}

void test_setTriggerInfoList(void)
{
	DBClientHatohol dbHatohol;
	TriggerInfoList triggerInfoList;
	for (size_t i = 0; i < NumTestTriggerInfo; i++)
		triggerInfoList.push_back(testTriggerInfo[i]);
	uint32_t serverId = testTriggerInfo[0].serverId;
	dbHatohol.setTriggerInfoList(triggerInfoList, serverId);

	AssertGetTriggersArg arg;
	assertGetTriggers(arg);
}

void test_addTriggerInfoList(void)
{
	size_t i;
	DBClientHatohol dbHatohol;

	// First call
	size_t numFirstAdd = NumTestTriggerInfo / 2;
	TriggerInfoList triggerInfoList0;
	for (i = 0; i < numFirstAdd; i++)
		triggerInfoList0.push_back(testTriggerInfo[i]);
	dbHatohol.addTriggerInfoList(triggerInfoList0);

	// Second call
	TriggerInfoList triggerInfoList1;
	for (; i < NumTestTriggerInfo; i++)
		triggerInfoList1.push_back(testTriggerInfo[i]);
	dbHatohol.addTriggerInfoList(triggerInfoList1);

	// Check
	AssertGetTriggersArg arg;
	assertGetTriggers(arg);
}

void test_getTriggerWithOneAutorizedServer(void)
{
	setupTestDBUser(true, true);
	AssertGetTriggersArg arg;
	arg.userId = 5;
	assertGetTriggersWithFilter(arg);
}

void test_getTriggerWithNoAutorizedServer(void)
{
	setupTestDBUser(true, true);
	AssertGetTriggersArg arg;
	arg.userId = 4;
	assertGetTriggersWithFilter(arg);
}

void test_getTriggerWithInvalidUserId(void)
{
	setupTestDBUser(true, true);
	AssertGetTriggersArg arg;
	arg.userId = INVALID_USER_ID;
	assertGetTriggersWithFilter(arg);
}

void test_itemInfoList(void)
{
	assertItemInfoList(ALL_SERVERS);
}

void test_itemInfoListForOneServer(void)
{
	uint32_t targetServerId = testItemInfo[0].serverId;
	assertItemInfoList(targetServerId);
}

void test_addItemInfoList(void)
{
	DBClientHatohol dbHatohol;
	ItemInfoList itemInfoList;
	for (size_t i = 0; i < NumTestItemInfo; i++)
		itemInfoList.push_back(testItemInfo[i]);
	dbHatohol.addItemInfoList(itemInfoList);

	AssertGetItemsArg arg;
	assertGetItems(arg);
}

void test_getItemsWithOneAutorizedServer(void)
{
	setupTestDBUser(true, true);
	AssertGetItemsArg arg;
	arg.userId = 5;
	assertGetItemsWithFilter(arg);
}

void test_getItemWithNoAutorizedServer(void)
{
	setupTestDBUser(true, true);
	AssertGetItemsArg arg;
	arg.userId = 4;
	assertGetItemsWithFilter(arg);
}

void test_getItemWithInvalidUserId(void)
{
	setupTestDBUser(true, true);
	AssertGetItemsArg arg;
	arg.userId = INVALID_USER_ID;
	assertGetItemsWithFilter(arg);
}

void test_addEventInfoList(void)
{
	// DBClientHatohol internally joins the trigger table and the event table.
	// So we also have to add trigger data.
	// When the internal join is removed, the following line will not be
	// needed.
	test_setTriggerInfoList();

	DBClientHatohol dbHatohol;
	EventInfoList eventInfoList;
	for (size_t i = 0; i < NumTestEventInfo; i++)
		eventInfoList.push_back(testEventInfo[i]);
	dbHatohol.addEventInfoList(eventInfoList);

	AssertGetEventsArg arg;
	assertGetEvents(arg);
}

void test_getLastEventId(void)
{
	test_addEventInfoList();
	DBClientHatohol dbHatohol;
	const int serverid = 3;
	cppcut_assert_equal(findLastEventId(serverid),
	                    dbHatohol.getLastEventId(serverid));
}

void test_getHostInfoList(void)
{
	assertGetHostInfoList(ALL_SERVERS);
}

void test_getHostInfoListForOneServer(void)
{
	uint32_t targetServerId = testTriggerInfo[0].serverId;
	assertGetHostInfoList(targetServerId);
}

void test_getNumberOfTriggersBySeverity(void)
{
	setupTestTriggerDB();

	uint32_t targetServerId = testTriggerInfo[0].serverId;
	// TODO: should give the appropriate host group ID after
	//       Hatohol support it.
	uint64_t hostGroupId = 0;

	DBClientHatohol dbHatohol;
	for (int i = 0; i < NUM_TRIGGER_SEVERITY; i++) {
		TriggersQueryOption option(USER_ID_SYSTEM);
		option.setTargetServerId(targetServerId);
		//TODO: uncomment it after Hatohol supports host group
		//option.setTargetHostGroupId(hostGroupId);
		TriggerSeverityType severity = (TriggerSeverityType)i;
		size_t actual = dbHatohol.getNumberOfTriggers(option, severity);
		size_t expected = getNumberOfTestTriggers
		                    (targetServerId, hostGroupId, severity);
		cppcut_assert_equal(expected, actual,
		                    cut_message("severity: %d", i));
	}
}

void test_getNumberOfTriggersBySeverityWithoutPriviledge(void)
{
	setupTestTriggerDB();

	uint32_t targetServerId = testTriggerInfo[0].serverId;
	// TODO: should give the appropriate host group ID after
	//       Hatohol support it.
	//uint64_t hostGroupId = 0;

	DBClientHatohol dbHatohol;
	for (int i = 0; i < NUM_TRIGGER_SEVERITY; i++) {
		TriggersQueryOption option;
		option.setTargetServerId(targetServerId);
		//TODO: uncomment it after Hatohol supports host group
		//option.setTargetHostGroupId(hostGroupId);
		TriggerSeverityType severity = (TriggerSeverityType)i;
		size_t actual = dbHatohol.getNumberOfTriggers(option, severity);
		size_t expected = 0;
		cppcut_assert_equal(expected, actual,
		                    cut_message("severity: %d", i));
	}
}

void test_getNumberOfGoodHosts(void)
{
	assertGetNumberOfHostsWithStatus(true);
}

void test_getNumberOfBadHosts(void)
{
	assertGetNumberOfHostsWithStatus(false);
}

void test_makeConditionEmpty(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	string expect = DBClientHatohol::getAlwaysFalseCondition();
	assertMakeCondition(srvHostGrpSetMap, expect);
}

void test_makeConditionAllServers(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[ALL_SERVERS].insert(ALL_HOST_GROUPS);
	string expect = "";
	assertMakeCondition(srvHostGrpSetMap, expect);
}

void test_makeConditionAllServersWithOthers(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[1].insert(1);
	srvHostGrpSetMap[1].insert(2);
	srvHostGrpSetMap[3].insert(1);
	srvHostGrpSetMap[ALL_SERVERS].insert(ALL_HOST_GROUPS);
	string expect = "";
	assertMakeCondition(srvHostGrpSetMap, expect);
}

void test_makeConditionAllServersWithSpecifiedHostGroup(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[ALL_SERVERS].insert(1);
	string expect = "";
	assertMakeCondition(srvHostGrpSetMap, expect);
}

void test_makeConditionOneServerAllHostGrp(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[1].insert(ALL_HOST_GROUPS);
	string expect =
	  StringUtils::sprintf("%s=1", serverIdColumnName.c_str());
	assertMakeCondition(srvHostGrpSetMap, expect);
}

void test_makeConditionOneServerAndOneHostGroup(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[1].insert(3);
	string expect =
	  StringUtils::sprintf("(%s=1 AND %s IN (3))",
	  serverIdColumnName.c_str(),
	  hostGroupIdColumnName.c_str());
	assertMakeCondition(srvHostGrpSetMap, expect);
}

void test_makeConditionOneServerAndHostGroups(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[1].insert(3);
	srvHostGrpSetMap[1].insert(1003);
	srvHostGrpSetMap[1].insert(2048);
	string expect =
	  StringUtils::sprintf("(%s=1 AND %s IN (3,1003,2048))",
	  serverIdColumnName.c_str(),
	  hostGroupIdColumnName.c_str());
	assertMakeCondition(srvHostGrpSetMap, expect);
}

void test_makeConditionMultipleServers(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[5].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[14].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[768].insert(ALL_HOST_GROUPS);
	string expect = StringUtils::sprintf("(%s=5 OR %s=14 OR %s=768)",
	  serverIdColumnName.c_str(),
	  serverIdColumnName.c_str(),
	  serverIdColumnName.c_str());
	assertMakeCondition(srvHostGrpSetMap, expect);
}

void test_makeConditionWithTargetServer(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[5].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[14].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[768].insert(ALL_HOST_GROUPS);
	string expect = StringUtils::sprintf("%s=14",
	  serverIdColumnName.c_str());
	assertMakeCondition(srvHostGrpSetMap, expect, 14);
}

void test_makeConditionWithUnauthorizedTargetServer(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[5].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[14].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[768].insert(ALL_HOST_GROUPS);
	assertMakeCondition(srvHostGrpSetMap, "0", 7);
}

void test_makeConditionWithTargetServerAndHost(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[5].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[14].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[768].insert(ALL_HOST_GROUPS);
	string expect = StringUtils::sprintf("((%s=14) AND %s=21)",
					     serverIdColumnName.c_str(),
					     hostIdColumnName.c_str());
	assertMakeCondition(srvHostGrpSetMap, expect, 14, 21);
}

void test_conditionForAdminWithTargetServerAndHost(void)
{
	HostResourceQueryOption option(USER_ID_SYSTEM);
	option.setTargetServerId(26);
	option.setTargetHostId(32);
	string expect = StringUtils::sprintf("%s=26 AND %s=32",
					     serverIdColumnName.c_str(),
					     hostIdColumnName.c_str());
	cppcut_assert_equal(expect, option.getCondition());
}

void test_eventQueryOptionDefaultTableName(void)
{
	HostResourceQueryOption option;
	cppcut_assert_equal(string(""), option.getTableNameForServerId());
}

void test_eventQueryOptionSetTableName(void)
{
	HostResourceQueryOption option;
	const string tableName = "test_event";
	option.setTableNameForServerId(tableName);
	cppcut_assert_equal(tableName, option.getTableNameForServerId());
}

void test_eventQueryOptionGetServerIdColumnName(void)
{
	TestHostResourceQueryOption option;
	const string tableName = "test_event";
	const string expectedServerIdColumnName
	  = tableName + "." + serverIdColumnName;
	option.setTableNameForServerId(tableName);
	cppcut_assert_equal(expectedServerIdColumnName,
			    option.callGetServerIdColumnName());
}

void test_makeConditionComplicated(void)
{
	ServerHostGrpSetMap srvHostGrpSetMap;
	srvHostGrpSetMap[5].insert(205);
	srvHostGrpSetMap[5].insert(800);
	srvHostGrpSetMap[14].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[768].insert(817);
	srvHostGrpSetMap[768].insert(818);
	srvHostGrpSetMap[768].insert(12817);
	srvHostGrpSetMap[2000].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[2001].insert(ALL_HOST_GROUPS);
	srvHostGrpSetMap[8192].insert(4096);
	string expect = StringUtils::sprintf(
	  "((%s=5 AND %s IN (205,800)) OR "
	  "%s=14 OR "
	  "(%s=768 AND %s IN (817,818,12817)) OR "
	  "%s=2000 OR "
	  "%s=2001 OR "
	  "(%s=8192 AND %s IN (4096)))",
	  serverIdColumnName.c_str(), hostGroupIdColumnName.c_str(),
	  serverIdColumnName.c_str(),
	  serverIdColumnName.c_str(), hostGroupIdColumnName.c_str(),
	  serverIdColumnName.c_str(),
	  serverIdColumnName.c_str(),
	  serverIdColumnName.c_str(), hostGroupIdColumnName.c_str());
	assertMakeCondition(srvHostGrpSetMap, expect);
}

void test_makeSelectConditionUserAdmin(void)
{
	HostResourceQueryOption option(USER_ID_SYSTEM);
	string actual = option.getCondition();
	string expect = "";
	cppcut_assert_equal(actual, expect);
}

void test_makeSelectConditionAllEvents(void)
{
	HostResourceQueryOption option;
	option.setFlags(OperationPrivilege::makeFlag(OPPRVLG_GET_ALL_SERVER));
	string actual = option.getCondition();
	string expect = "";
	cppcut_assert_equal(actual, expect);
}

void test_makeSelectConditionNoneUser(void)
{
	setupTestDBUser(true, true);
	HostResourceQueryOption option;
	string actual = option.getCondition();
	string expect = DBClientHatohol::getAlwaysFalseCondition();
	cppcut_assert_equal(actual, expect);
}

void test_makeSelectCondition(void)
{
	setupTestDBUser(true, true);
	HostResourceQueryOption option;
	for (size_t i = 0; i < NumTestUserInfo; i++) {
		UserIdType userId = i + 1;
		option.setUserId(userId);
		string actual = option.getCondition();
		string expect = makeExpectedConditionForUser(userId);
		cppcut_assert_equal(expect, actual);
	}
}

void test_getEventSortAscending(void)
{
	AssertGetEventsArg arg;
	arg.sortOrder = DataQueryOption::SORT_ASCENDING;
	assertGetEventsWithFilter(arg);
}

void test_getEventSortDescending(void)
{
	AssertGetEventsArg arg;
	arg.sortOrder = DataQueryOption::SORT_DESCENDING;
	assertGetEventsWithFilter(arg);
}

void test_getEventWithMaximumNumber(void)
{
	AssertGetEventsArg arg;
	arg.maxNumber = 2;
	assertGetEventsWithFilter(arg);
}

void test_getEventWithMaximumNumberDescending(void)
{
	AssertGetEventsArg arg;
	arg.maxNumber = 2;
	arg.sortOrder = DataQueryOption::SORT_DESCENDING;
	assertGetEventsWithFilter(arg);
}

void test_getEventWithMaximumNumberAscendingStartId(void)
{
	AssertGetEventsArg arg;
	arg.maxNumber = 2;
	arg.sortOrder = DataQueryOption::SORT_ASCENDING;
	arg.startId = 2;
	assertGetEventsWithFilter(arg);
}

void test_getEventWithMaximumNumberDescendingStartId(void)
{
	AssertGetEventsArg arg;
	arg.maxNumber = 2;
	arg.sortOrder = DataQueryOption::SORT_DESCENDING;
	arg.startId = NumTestEventInfo - 1;
	assertGetEventsWithFilter(arg);
}

void test_getEventWithStartIdWithoutSortOrder(void)
{
	AssertGetEventsArg arg;
	arg.startId = 2;
	arg.expectedErrorCode = HTERR_NOT_FOUND_SORT_ORDER;
	assertGetEventsWithFilter(arg);
}

void test_getEventWithOneAutorizedServer(void)
{
	setupTestDBUser(true, true);
	AssertGetEventsArg arg;
	arg.userId = 5;
	assertGetEventsWithFilter(arg);
}

void test_getEventWithNoAutorizedServer(void)
{
	setupTestDBUser(true, true);
	AssertGetEventsArg arg;
	arg.userId = 4;
	assertGetEventsWithFilter(arg);
}

void test_getEventWithInvalidUserId(void)
{
	setupTestDBUser(true, true);
	AssertGetEventsArg arg;
	arg.userId = INVALID_USER_ID;
	assertGetEventsWithFilter(arg);
}

} // namespace testDBClientHatohol
