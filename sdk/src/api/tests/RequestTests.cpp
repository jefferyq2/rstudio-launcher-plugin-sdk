/*
 * RequestTests.cpp
 *
 * Copyright (C) 2020 by RStudio, PBC
 *
 * Unless you have received this program directly from RStudio pursuant to the terms of a commercial license agreement
 * with RStudio, then this program is licensed to you under the following terms:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <TestMain.hpp>

#include <Error.hpp>
#include <MockLogDestination.hpp>
#include <api/Constants.hpp>
#include <api/Request.hpp>
#include <json/Json.hpp>
#include <logging/Logger.hpp>
#include <system/User.hpp>

namespace rstudio {
namespace launcher_plugins {
namespace api {

using namespace logging;

TEST_CASE("Parse valid bootstrap request")
{
   MockLogPtr logDest = getMockLogDest();

   json::Object versionObj;
   versionObj.insert(FIELD_VERSION_MAJOR, 2);
   versionObj.insert(FIELD_VERSION_MINOR, 11);
   versionObj.insert(FIELD_VERSION_PATCH, 375);

   json::Object requestObj;
   requestObj.insert(FIELD_VERSION, versionObj);
   requestObj.insert(FIELD_MESSAGE_TYPE, static_cast<int>(Request::Type::BOOTSTRAP));
   requestObj.insert(FIELD_REQUEST_ID, 6);

   std::shared_ptr<Request> request;
   Error error = Request::fromJson(requestObj, request);

   REQUIRE_FALSE(error);
   REQUIRE(request != nullptr);
   CHECK(request->getType() == Request::Type::BOOTSTRAP);
   CHECK(request->getId() == 6);

   BootstrapRequest* bootstrapRequest = dynamic_cast<BootstrapRequest*>(request.get());
   REQUIRE(bootstrapRequest != nullptr);
   CHECK(bootstrapRequest->getMajorVersion() == 2);
   CHECK(bootstrapRequest->getMinorVersion() == 11);
   CHECK(bootstrapRequest->getPatchNumber() == 375);
   CHECK(logDest->getSize() == 0);
}

TEST_CASE("Parse invalid bootstrap request")
{
   MockLogPtr logDest = getMockLogDest();

   json::Object versionObj;
   versionObj.insert(FIELD_VERSION_MAJOR, 2);
   versionObj.insert(FIELD_VERSION_PATCH, 375);

   json::Object requestObj;
   requestObj.insert(FIELD_VERSION, versionObj);
   requestObj.insert(FIELD_MESSAGE_TYPE, static_cast<int>(Request::Type::BOOTSTRAP));
   requestObj.insert(FIELD_REQUEST_ID, 6);

   std::shared_ptr<Request> request;
   Error error = Request::fromJson(requestObj, request);

   REQUIRE(error);
   CHECK(error.getMessage().find("Invalid request received from launcher") != std::string::npos);
   REQUIRE(logDest->getSize() == 1);
   CHECK(logDest->peek().Level == LogLevel::ERR);
   CHECK(logDest->pop().Message.find(FIELD_VERSION_MINOR) != std::string::npos);
}

TEST_CASE("Parse invalid request - missing message type")
{
   json::Object requestObj;
   requestObj.insert(FIELD_REQUEST_ID, 6);

   std::shared_ptr<Request> request;
   Error error = Request::fromJson(requestObj, request);

   REQUIRE(error);
   REQUIRE(error.getMessage().find(FIELD_MESSAGE_TYPE) != std::string::npos);
}

TEST_CASE("Parse invalid request - missing request request ID")
{
   MockLogPtr logDest = getMockLogDest();
   json::Object requestObj;
   requestObj.insert(FIELD_MESSAGE_TYPE, static_cast<int>(Request::Type::BOOTSTRAP));


   std::shared_ptr<Request> request;
   Error error = Request::fromJson(requestObj, request);

   REQUIRE(error);
   CHECK(error.getMessage().find("Invalid request received from launcher") != std::string::npos);
   REQUIRE(logDest->getSize() == 2);
   CHECK(logDest->peek().Level == LogLevel::ERR);
   CHECK(logDest->pop().Message.find(FIELD_REQUEST_ID) != std::string::npos); // Base constructor runs first.
   CHECK(logDest->peek().Level == LogLevel::ERR);
   CHECK(logDest->pop().Message.find(FIELD_VERSION) != std::string::npos); // Then bootstrap.
}

TEST_CASE("Parse invalid request - negative message type")
{
   MockLogPtr logDest = getMockLogDest();
   json::Object requestObj;
   requestObj.insert(FIELD_MESSAGE_TYPE, -4);
   requestObj.insert(FIELD_REQUEST_ID, 6);

   std::shared_ptr<Request> request;
   Error error = Request::fromJson(requestObj, request);

   REQUIRE(error);
   CHECK(error.getMessage().find("-4") != std::string::npos);
   CHECK(logDest->getSize() == 0);
}

TEST_CASE("Parse invalid request - message type too large")
{
   MockLogPtr logDest = getMockLogDest();
   json::Object requestObj;
   requestObj.insert(FIELD_MESSAGE_TYPE, 568);
   requestObj.insert(FIELD_REQUEST_ID, 6);

   std::shared_ptr<Request> request;
   Error error = Request::fromJson(requestObj, request);

   REQUIRE(error);
   REQUIRE(error.getMessage().find("568") != std::string::npos);
   CHECK(logDest->getSize() == 0);
}

TEST_CASE("Parse heartbeat request")
{
   MockLogPtr logDest = getMockLogDest();
   json::Object requestObj;
   requestObj.insert(FIELD_MESSAGE_TYPE, static_cast<int>(Request::Type::HEARTBEAT));
   requestObj.insert(FIELD_REQUEST_ID, 0);

   std::shared_ptr<Request> request;
   Error error = Request::fromJson(requestObj, request);

   REQUIRE_FALSE(error);
   CHECK(request->getType() == Request::Type::HEARTBEAT);
   CHECK(request->getId() == 0);
   CHECK(logDest->getSize() == 0);
}

TEST_CASE("Parse cluster info request")
{
   MockLogPtr logDest = getMockLogDest();
   json::Object requestObj;
   requestObj[FIELD_MESSAGE_TYPE] = static_cast<int>(Request::Type::GET_CLUSTER_INFO);
   requestObj[FIELD_REQUEST_ID] = 6;
   requestObj[FIELD_REAL_USER] = USER_TWO;

   std::shared_ptr<Request> request;

   system::User user;
   REQUIRE_FALSE(system::User::getUserFromIdentifier(USER_TWO, user));
   REQUIRE_FALSE(Request::fromJson(requestObj, request));
   CHECK(request->getType() == Request::Type::GET_CLUSTER_INFO);
   CHECK(request->getId() == 6);
   CHECK(std::static_pointer_cast<UserRequest>(request)->getUser() == user);
   CHECK(std::static_pointer_cast<UserRequest>(request)->getRequestUsername().empty());
   CHECK(logDest->getSize() == 0);
}

TEST_CASE("Parse cluster info request (admin user)")
{
   MockLogPtr logDest = getMockLogDest();
   json::Object requestObj;
   requestObj[FIELD_MESSAGE_TYPE] = static_cast<int>(Request::Type::GET_CLUSTER_INFO);
   requestObj[FIELD_REQUEST_ID] = 14;
   requestObj[FIELD_REAL_USER] = "*";
   requestObj[FIELD_REQUEST_USERNAME] = USER_TWO;

   std::shared_ptr<Request> request;

   REQUIRE_FALSE(Request::fromJson(requestObj, request));
   CHECK(request->getType() == Request::Type::GET_CLUSTER_INFO);
   CHECK(request->getId() == 14);
   CHECK(std::static_pointer_cast<UserRequest>(request)->getUser().isAllUsers());
   CHECK(std::static_pointer_cast<UserRequest>(request)->getRequestUsername() == USER_TWO);
   CHECK(logDest->getSize() == 0);
}

TEST_CASE("Parse invalid cluster info request")
{
   json::Object requestObj;
   requestObj[FIELD_MESSAGE_TYPE] = static_cast<int>(Request::Type::GET_CLUSTER_INFO);
   requestObj[FIELD_REQUEST_ID] = 6;
   requestObj[FIELD_REAL_USER] = "notauser";

   std::shared_ptr<Request> request;

   REQUIRE(Request::fromJson(requestObj, request));
}

TEST_CASE("Parse get job request")
{
   MockLogPtr logDest = getMockLogDest();

   json::Object requestObj;
   requestObj[FIELD_MESSAGE_TYPE] = static_cast<int>(Request::Type::GET_JOB);
   requestObj[FIELD_REQUEST_ID] = 657;
   requestObj[FIELD_REAL_USER] = "*";
   requestObj[FIELD_REQUEST_USERNAME] = USER_TWO;
   requestObj[FIELD_JOB_ID] = "2588";

   std::shared_ptr<Request> request;

   REQUIRE_FALSE(Request::fromJson(requestObj, request));
   CHECK(request->getType() == Request::Type::GET_JOB);
   CHECK(request->getId() == 657);

   Optional<system::DateTime> endTime, startTime;
   Optional<std::set<Job::State> > statuses;

   std::shared_ptr<JobStateRequest> jobStateRequest = std::static_pointer_cast<JobStateRequest>(request);
   CHECK(jobStateRequest->getUser().isAllUsers());
   CHECK(jobStateRequest->getRequestUsername() == USER_TWO);
   CHECK(jobStateRequest->getJobId() == "2588");
   CHECK(jobStateRequest->getEncodedJobId() == "");
   CHECK((!jobStateRequest->getEndTime(endTime) && !endTime));
   CHECK_FALSE(jobStateRequest->getFieldSet());
   CHECK((!jobStateRequest->getStartTime(startTime) && !startTime));
   CHECK((!jobStateRequest->getStatusSet(statuses) && !statuses));
   CHECK_FALSE(jobStateRequest->getTagSet());
   CHECK(logDest->getSize() == 0);
}

TEST_CASE("Parse get job request w/ encoded ID")
{
   MockLogPtr logDest = getMockLogDest();

   json::Object requestObj;
   requestObj[FIELD_MESSAGE_TYPE] = static_cast<int>(Request::Type::GET_JOB);
   requestObj[FIELD_REQUEST_ID] = 91;
   requestObj[FIELD_REAL_USER] = USER_TWO;
   requestObj[FIELD_REQUEST_USERNAME] = USER_TWO;
   requestObj[FIELD_JOB_ID] = "142";
   requestObj[FIELD_ENCODED_JOB_ID] = "Y2x1c3Rlci0xNDIK";

   std::shared_ptr<Request> request;

   system::User user;
   REQUIRE_FALSE(system::User::getUserFromIdentifier(USER_TWO, user));
   REQUIRE_FALSE(Request::fromJson(requestObj, request));
   CHECK(request->getType() == Request::Type::GET_JOB);
   CHECK(request->getId() == 91);

   Optional<system::DateTime> endTime, startTime;
   Optional<std::set<Job::State> > statuses;

   std::shared_ptr<JobStateRequest> jobStateRequest = std::static_pointer_cast<JobStateRequest>(request);
   CHECK(jobStateRequest->getUser() == user);
   CHECK(jobStateRequest->getRequestUsername() == USER_TWO);
   CHECK(jobStateRequest->getJobId() == "142");
   CHECK(jobStateRequest->getEncodedJobId() == "Y2x1c3Rlci0xNDIK");
   CHECK((!jobStateRequest->getEndTime(endTime) && !endTime));
   CHECK_FALSE(jobStateRequest->getFieldSet());
   CHECK((!jobStateRequest->getStartTime(startTime) && !startTime));
   CHECK((!jobStateRequest->getStatusSet(statuses) && !statuses));
   CHECK_FALSE(jobStateRequest->getTagSet());
   CHECK(logDest->getSize() == 0);
}

TEST_CASE("Parse complete get job request")
{
   MockLogPtr logDest = getMockLogDest();

   system::DateTime expectedEnd, expectedStart;
   REQUIRE_FALSE(system::DateTime::fromString("2020-03-15T18:00:00", expectedEnd));
   REQUIRE_FALSE(system::DateTime::fromString("2020-03-15T15:00:00", expectedStart));

   std::set<std::string> expectedFields, expectedTags;
   expectedFields.insert("id");
   expectedFields.insert("status");
   expectedFields.insert("statusMessage");

   expectedTags.insert("tag1");
   expectedTags.insert("tag 2");

   std::set<Job::State> expectedStatuses;
   expectedStatuses.insert(Job::State::PENDING);
   expectedStatuses.insert(Job::State::RUNNING);

   json::Array fields, statusArr, tags;
   fields.push_back("id");
   fields.push_back("status");
   fields.push_back("statusMessage");

   statusArr.push_back("Pending");
   statusArr.push_back("Running");

   tags.push_back("tag1");
   tags.push_back("tag 2");

   json::Object requestObj;
   requestObj[FIELD_MESSAGE_TYPE] = static_cast<int>(Request::Type::GET_JOB);
   requestObj[FIELD_REQUEST_ID] = 91;
   requestObj[FIELD_REAL_USER] = USER_FIVE;
   requestObj[FIELD_REQUEST_USERNAME] = USER_FIVE;
   requestObj[FIELD_JOB_ID] = "142";
   requestObj[FIELD_ENCODED_JOB_ID] = "Y2x1c3Rlci0xNDIK";
   requestObj[FIELD_JOB_END_TIME] = "2020-03-15T18:00:00";
   requestObj[FIELD_JOB_FIELDS] = fields;
   requestObj[FIELD_JOB_START_TIME] = "2020-03-15T15:00:00";
   requestObj[FIELD_JOB_STATUSES] = statusArr;
   requestObj[FIELD_JOB_TAGS] = tags;

   std::shared_ptr<Request> request;

   system::User user;
   REQUIRE_FALSE(system::User::getUserFromIdentifier(USER_FIVE, user));
   REQUIRE_FALSE(Request::fromJson(requestObj, request));
   CHECK(request->getType() == Request::Type::GET_JOB);
   CHECK(request->getId() == 91);
   CHECK(logDest->getSize() == 0);

   Optional<system::DateTime> endTime, startTime;
   Optional<std::set<Job::State> > statuses;

   std::shared_ptr<JobStateRequest> jobRequest = std::static_pointer_cast<JobStateRequest>(request);
   CHECK(jobRequest->getUser() == user);
   CHECK(jobRequest->getRequestUsername() == USER_FIVE);
   CHECK(jobRequest->getJobId() == "142");
   CHECK(jobRequest->getEncodedJobId() == "Y2x1c3Rlci0xNDIK");
   CHECK((!jobRequest->getEndTime(endTime) && endTime &&
      endTime.getValueOr(system::DateTime()) == expectedEnd));
   CHECK((jobRequest->getFieldSet() && jobRequest->getFieldSet().getValueOr({}) == expectedFields));
   CHECK((!jobRequest->getStartTime(startTime) && startTime &&
      startTime.getValueOr(system::DateTime()) == expectedStart));
   CHECK((!jobRequest->getStatusSet(statuses) && statuses &&
   statuses.getValueOr({}) == expectedStatuses));
   CHECK((jobRequest->getTagSet() && jobRequest->getTagSet().getValueOr({}) == expectedTags));
}

TEST_CASE("Parse get job request with fields (no id)")
{
   MockLogPtr logDest = getMockLogDest();

   std::set<std::string> expectedFields;
   expectedFields.insert("id"); // ID is expected no matter what.
   expectedFields.insert("status");
   expectedFields.insert("statusMessage");

   json::Array fields;
   fields.push_back("status");
   fields.push_back("statusMessage");

   json::Object requestObj;
   requestObj[FIELD_MESSAGE_TYPE] = static_cast<int>(Request::Type::GET_JOB);
   requestObj[FIELD_REQUEST_ID] = 91;
   requestObj[FIELD_REAL_USER] = USER_FIVE;
   requestObj[FIELD_REQUEST_USERNAME] = USER_FIVE;
   requestObj[FIELD_JOB_ID] = "142";
   requestObj[FIELD_ENCODED_JOB_ID] = "Y2x1c3Rlci0xNDIK";
   requestObj[FIELD_JOB_FIELDS] = fields;

   std::shared_ptr<Request> request;

   system::User user;
   REQUIRE_FALSE(system::User::getUserFromIdentifier(USER_FIVE, user));
   REQUIRE_FALSE(Request::fromJson(requestObj, request));
   CHECK(request->getType() == Request::Type::GET_JOB);
   CHECK(request->getId() == 91);
   CHECK(logDest->getSize() == 0);

   Optional<system::DateTime> endTime, startTime;
   Optional<std::set<Job::State> > statuses;

   std::shared_ptr<JobStateRequest> jobRequest = std::static_pointer_cast<JobStateRequest>(request);
   CHECK(jobRequest->getUser() == user);
   CHECK(jobRequest->getRequestUsername() == USER_FIVE);
   CHECK(jobRequest->getJobId() == "142");
   CHECK(jobRequest->getEncodedJobId() == "Y2x1c3Rlci0xNDIK");
   CHECK((!jobRequest->getEndTime(endTime) && !endTime));
   CHECK((jobRequest->getFieldSet() && jobRequest->getFieldSet().getValueOr({}) == expectedFields));
   CHECK((!jobRequest->getStartTime(startTime) && !startTime));
   CHECK((!jobRequest->getStatusSet(statuses) && !statuses));
   CHECK_FALSE(jobRequest->getTagSet());
}

TEST_CASE("Parse invalid get job request")
{
   json::Object requestObj;
   requestObj[FIELD_MESSAGE_TYPE] = static_cast<int>(Request::Type::GET_JOB);
   requestObj[FIELD_REQUEST_ID] = 91;
   requestObj[FIELD_REAL_USER] = USER_TWO;
   requestObj[FIELD_REQUEST_USERNAME] = USER_TWO;
   requestObj[FIELD_ENCODED_JOB_ID] = "Y2x1c3Rlci0xNDIK";

   system::User user;
   REQUIRE_FALSE(system::User::getUserFromIdentifier(USER_TWO, user));

   SECTION("Missing Job ID")
   {
      std::shared_ptr<Request> request;
      CHECK(Request::fromJson(requestObj, request));
   }

   SECTION("Invalid date/time")
   {
      requestObj[FIELD_JOB_ID] = "444";
      requestObj[FIELD_JOB_END_TIME] = "not a date time";

      std::shared_ptr<Request> request;
      CHECK_FALSE(Request::fromJson(requestObj, request));
      CHECK(request->getType() == Request::Type::GET_JOB);
      CHECK(request->getId() == 91);

      Optional<system::DateTime> endTime, startTime;
      Optional<std::set<Job::State> > statuses;

      std::shared_ptr<JobStateRequest> jobRequest = std::static_pointer_cast<JobStateRequest>(request);
      CHECK(jobRequest->getUser() == user);
      CHECK(jobRequest->getRequestUsername() == USER_TWO);
      CHECK(jobRequest->getJobId() == "444");
      CHECK(jobRequest->getEncodedJobId() == "Y2x1c3Rlci0xNDIK");
      CHECK((jobRequest->getEndTime(endTime) && !endTime));
      CHECK(!jobRequest->getFieldSet());
      CHECK((!jobRequest->getStartTime(startTime) && !startTime));
      CHECK((!jobRequest->getStatusSet(statuses) && !statuses));
      CHECK_FALSE(jobRequest->getTagSet());
   }

   SECTION("Invalid status")
   {
      json::Array statusArr;
      statusArr.push_back("Running");
      statusArr.push_back("Completed");
      statusArr.push_back("NotAStatus");
      statusArr.push_back("Failed");

      requestObj[FIELD_JOB_ID] = "444";
      requestObj[FIELD_JOB_STATUSES] = statusArr;

      std::shared_ptr<Request> request;
      CHECK_FALSE(Request::fromJson(requestObj, request));
      CHECK(request->getType() == Request::Type::GET_JOB);
      CHECK(request->getId() == 91);

      Optional<system::DateTime> endTime, startTime;
      Optional<std::set<Job::State> > statuses;

      std::shared_ptr<JobStateRequest> jobRequest = std::static_pointer_cast<JobStateRequest>(request);
      CHECK(jobRequest->getUser() == user);
      CHECK(jobRequest->getRequestUsername() == USER_TWO);
      CHECK(jobRequest->getJobId() == "444");
      CHECK(jobRequest->getEncodedJobId() == "Y2x1c3Rlci0xNDIK");
      CHECK((!jobRequest->getEndTime(endTime) && !endTime));
      CHECK(!jobRequest->getFieldSet());
      CHECK((!jobRequest->getStartTime(startTime) && !startTime));
      CHECK((jobRequest->getStatusSet(statuses) && !statuses));
      CHECK_FALSE(jobRequest->getTagSet());
   }

   SECTION("Invalid tags (not a json::Array)")
   {
      requestObj[FIELD_JOB_ID] = "444";
      requestObj[FIELD_JOB_TAGS] = 32;

      std::shared_ptr<Request> request;
      CHECK(Request::fromJson(requestObj, request));
   }
}

TEST_CASE("Parse JobStatusRequest")
{
   system::User user5;
   REQUIRE_FALSE(system::User::getUserFromIdentifier(USER_FIVE, user5));

   json::Object requestObj;
   requestObj[FIELD_MESSAGE_TYPE] = static_cast<int>(Request::Type::GET_JOB_STATUS);
   requestObj[FIELD_REQUEST_ID] = 8;

   SECTION("Specific user, no cancel, all jobs")
   {
      requestObj[FIELD_REAL_USER] = USER_FIVE;
      requestObj[FIELD_JOB_ID] = "*";
      requestObj[FIELD_ENCODED_JOB_ID] = "";

      std::shared_ptr<Request> request;
      REQUIRE_FALSE(Request::fromJson(requestObj, request));
      REQUIRE(request->getType() == Request::Type::GET_JOB_STATUS);

      std::shared_ptr<JobStatusRequest> jobStatusRequest = std::static_pointer_cast<JobStatusRequest>(request);
      CHECK(jobStatusRequest->getId() == 8);
      CHECK(jobStatusRequest->getUser() == user5);
      CHECK(jobStatusRequest->getRequestUsername().empty());
      CHECK(jobStatusRequest->getJobId() == "*");
      CHECK(jobStatusRequest->getEncodedJobId().empty());
      CHECK_FALSE(jobStatusRequest->isCancelRequest());
   }

   SECTION("All users, cancel (false), specific job")
   {
      requestObj[FIELD_REAL_USER] = "*";
      requestObj[FIELD_REQUEST_USERNAME] = USER_FOUR;
      requestObj[FIELD_JOB_ID] = "job-182";
      requestObj[FIELD_ENCODED_JOB_ID] = "Q2x1c3Rlci1qb2ItMTgyCg==";
      requestObj[FIELD_CANCEL_STREAM] = false;

      std::shared_ptr<Request> request;
      REQUIRE_FALSE(Request::fromJson(requestObj, request));
      REQUIRE(request->getType() == Request::Type::GET_JOB_STATUS);

      std::shared_ptr<JobStatusRequest> jobStatusRequest = std::static_pointer_cast<JobStatusRequest>(request);
      CHECK(jobStatusRequest->getId() == 8);
      CHECK(jobStatusRequest->getUser().isAllUsers());
      CHECK(jobStatusRequest->getRequestUsername() == USER_FOUR);
      CHECK(jobStatusRequest->getJobId() == "job-182");
      CHECK(jobStatusRequest->getEncodedJobId() == "Q2x1c3Rlci1qb2ItMTgyCg==");
      CHECK_FALSE(jobStatusRequest->isCancelRequest());
   }

   SECTION("All users, cancel (true), all jobs")
   {
      requestObj[FIELD_REAL_USER] = "*";
      requestObj[FIELD_REQUEST_USERNAME] = USER_FOUR;
      requestObj[FIELD_JOB_ID] = "*";
      requestObj[FIELD_ENCODED_JOB_ID] = "";
      requestObj[FIELD_CANCEL_STREAM] = true;

      std::shared_ptr<Request> request;
      REQUIRE_FALSE(Request::fromJson(requestObj, request));
      REQUIRE(request->getType() == Request::Type::GET_JOB_STATUS);

      std::shared_ptr<JobStatusRequest> jobStatusRequest = std::static_pointer_cast<JobStatusRequest>(request);
      CHECK(jobStatusRequest->getId() == 8);
      CHECK(jobStatusRequest->getUser().isAllUsers());
      CHECK(jobStatusRequest->getRequestUsername() == USER_FOUR);
      CHECK(jobStatusRequest->getJobId() == "*");
      CHECK(jobStatusRequest->getEncodedJobId().empty());
      CHECK(jobStatusRequest->isCancelRequest());
   }
}

} // namespace api
} // namespace launcher_plugins
} // namespace rstudio