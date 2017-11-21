/**
 * @file SiteToSiteProvenanceReportingTask.cpp
 * SiteToSiteProvenanceReportingTask class implementation
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <vector>
#include <queue>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <sstream>
#include <functional>
#include <iostream>
#include <utility>
#include "core/Repository.h"
#include "core/reporting/SiteToSiteProvenanceReportingTask.h"
#include "../include/io/StreamFactory.h"
#include "io/ClientSocket.h"
#include "utils/TimeUtil.h"
#include "utils/RapidJsonUtils.h"
#include "core/ProcessContext.h"
#include "core/ProcessSession.h"
#include "provenance/Provenance.h"
#include "FlowController.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

using utils::RapidJsonUtils;

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace core {
namespace reporting {

const char *SiteToSiteProvenanceReportingTask::ProvenanceAppStr = "MiNiFi Flow";

void SiteToSiteProvenanceReportingTask::initialize() {
  RemoteProcessorGroupPort::initialize();
}

void SiteToSiteProvenanceReportingTask::getJsonReport(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSession> &session,
                                                      std::vector<std::shared_ptr<core::SerializableComponent>> &records, std::string &report) {
  rapidjson::Document array(rapidjson::kArrayType);
  rapidjson::Document::AllocatorType &alloc = array.GetAllocator();

  for (auto sercomp : records) {
    std::shared_ptr<provenance::ProvenanceEventRecord> record = std::dynamic_pointer_cast<provenance::ProvenanceEventRecord>(sercomp);
    if (nullptr == record) {
      break;
    }

    rapidjson::Value recordJson(rapidjson::kObjectType);
    rapidjson::Value updatedAttributesJson(rapidjson::kObjectType);
    rapidjson::Value parentUuidJson(rapidjson::kArrayType);
    rapidjson::Value childUuidJson(rapidjson::kArrayType);

    recordJson.AddMember("timestampMillis", record->getEventTime(), alloc);
    recordJson.AddMember("durationMillis", record->getEventDuration(), alloc);
    recordJson.AddMember("lineageStart", record->getlineageStartDate(), alloc);
    recordJson.AddMember("entitySize", record->getFileSize(), alloc);
    recordJson.AddMember("entityOffset", record->getFileOffset(), alloc);

    recordJson.AddMember("entityType", "org.apache.nifi.flowfile.FlowFile", alloc);

    recordJson.AddMember("eventId", RapidJsonUtils::getStringValue(record->getEventId(), alloc), alloc);

    std::string eventType = provenance::ProvenanceEventRecord::ProvenanceEventTypeStr[record->getEventType()];
    recordJson.AddMember("eventType", RapidJsonUtils::getStringValue(eventType, alloc), alloc);

    recordJson.AddMember("details", RapidJsonUtils::getStringValue(record->getDetails(), alloc), alloc);
    recordJson.AddMember("componentId", RapidJsonUtils::getStringValue(record->getComponentId(), alloc), alloc);
    recordJson.AddMember("componentType", RapidJsonUtils::getStringValue(record->getComponentType(), alloc), alloc);
    recordJson.AddMember("entityId", RapidJsonUtils::getStringValue(record->getFlowFileUuid(), alloc), alloc);
    recordJson.AddMember("transitUri", RapidJsonUtils::getStringValue(record->getTransitUri(), alloc), alloc);
    recordJson.AddMember("remoteIdentifier", RapidJsonUtils::getStringValue(record->getSourceSystemFlowFileIdentifier(), alloc), alloc);
    recordJson.AddMember("alternateIdentifier", RapidJsonUtils::getStringValue(record->getAlternateIdentifierUri(), alloc), alloc);

    for (auto attr : record->getAttributes()) {
      RapidJsonUtils::setJsonStr(attr.first, attr.second, updatedAttributesJson, alloc);
    }
    recordJson.AddMember("updatedAttributes", updatedAttributesJson, alloc);

    for (auto parentUUID : record->getParentUuids()) {
      RapidJsonUtils::appendJsonStr(parentUUID, parentUuidJson, alloc);
    }
    recordJson.AddMember("parentIds", parentUuidJson, alloc);

    for (auto childUUID : record->getChildrenUuids()) {
      RapidJsonUtils::appendJsonStr(childUUID, childUuidJson, alloc);
    }
    recordJson.AddMember("childIds", childUuidJson, alloc);

    rapidjson::Value applicationVal;
    applicationVal.SetString(ProvenanceAppStr, std::strlen(ProvenanceAppStr));
    recordJson.AddMember("application", applicationVal, alloc);

    array.PushBack(recordJson, alloc);
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  array.Accept(writer);

  report = buffer.GetString();
}

void SiteToSiteProvenanceReportingTask::onSchedule(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSessionFactory> &sessionFactory) {
}

void SiteToSiteProvenanceReportingTask::onTrigger(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSession> &session) {
  logger_->log_debug("SiteToSiteProvenanceReportingTask -- onTrigger");
  std::vector<std::shared_ptr<core::SerializableComponent>> records;
  logger_->log_debug("batch size %d records", batch_size_);
  size_t deserialized = batch_size_;
  std::shared_ptr<core::Repository> repo = context->getProvenanceRepository();
  std::function<std::shared_ptr<core::SerializableComponent>()> constructor = []() {return std::make_shared<provenance::ProvenanceEventRecord>();};
  if (!repo->DeSerialize(records, deserialized, constructor) && deserialized == 0) {
    logger_->log_debug("Not sending because deserialized is %d", deserialized);
    return;
  }

  logger_->log_debug("batch size %d records", batch_size_, deserialized);

  logger_->log_debug("Captured %d records", deserialized);
  std::string jsonStr;
  this->getJsonReport(context, session, records, jsonStr);
  if (jsonStr.length() <= 0) {
    return;
  }

  auto protocol_ = getNextProtocol(true);

  if (!protocol_) {
    context->yield();
    return;
  }

  try {
    std::map<std::string, std::string> attributes;
    if (!protocol_->transmitPayload(context, session, jsonStr, attributes)) {
      context->yield();
    }
  } catch (...) {
    // if transfer bytes failed, return instead of purge the provenance records
    return;
  }

  // we transfer the record, purge the record from DB
  repo->Delete(records);
  returnProtocol(std::move(protocol_));
}

} /* namespace reporting */
} /* namespace core */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */
