/**
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

#include <regex.h>
#include <uuid/uuid.h>
#include <memory>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "PcapLiveDeviceList.h"
#include "PcapFilter.h"
#include "PcapPlusPlusVersion.h"
#include "PcapFileDevice.h"
#include "PlatformSpecificUtils.h"
#include "core/FlowFile.h"
#include "core/logging/Logger.h"
#include "core/ProcessContext.h"
#include "core/Relationship.h"
#include "CapturePacket.h"
#include "ResourceClaim.h"
#include "utils/StringUtils.h"
#include "utils/ByteArrayCallback.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace processors {

std::shared_ptr<utils::IdGenerator> CapturePacket::id_generator_ = utils::IdGenerator::getIdGenerator();
core::Property CapturePacket::BaseDir("Base Directory", "Scratch directory for PCAP files", "/tmp/");
core::Property CapturePacket::BatchSize("Batch Size", "The number of packets to combine within a given PCAP", "50");
core::Property CapturePacket::CaptureBluetooth("Capture Bluetooth", "True indicates that we support bluetooth interfaces", "false");

const char *CapturePacket::ProcessorName = "CapturePacket";

std::string CapturePacket::generate_new_pcap(const std::string &base_path) {
  std::string path = base_path;
  // can use relaxed for a counter
  int cnt = num_.fetch_add(1, std::memory_order_relaxed);
  std::string filename = std::to_string(cnt);
  path.append(filename);
  return path;
}

void CapturePacket::packet_callback(pcpp::RawPacket* packet, pcpp::PcapLiveDevice* dev, void* data) {
  // parse the packet
  PacketMovers* capture_mechanism = (PacketMovers*) data;

  CapturePacketMechanism *capture;

  if (capture_mechanism->source.try_dequeue(capture)) {

    // if needed - write the packet to the output pcap file

    if (capture->writer_ != nullptr) {
      capture->writer_->writePacket(*packet);

      if (capture->incrementAndCheck()) {

        capture->writer_->close();

        capture_mechanism->sink.enqueue(capture);

        capture_mechanism->source.enqueue(create_new_capture(capture->getBasePath(), capture->getMaxSize()));
      } else {
        capture_mechanism->source.enqueue(capture);
      }

    }
  }
}

CapturePacketMechanism *CapturePacket::create_new_capture(const std::string &base_path, int64_t *max_size) {
  CapturePacketMechanism *new_capture = new CapturePacketMechanism(base_path, generate_new_pcap(base_path), max_size);
  new_capture->writer_ = new pcpp::PcapFileWriterDevice(new_capture->getFile().c_str());
  if (!new_capture->writer_->open())
    throw std::exception();

  return new_capture;
}

std::atomic<int> CapturePacket::num_(0);
core::Relationship CapturePacket::Success("success", "All files are routed to success");
void CapturePacket::initialize() {
  logger_->log_info("Initializing CapturePacket");

  // Set the supported properties
  std::set<core::Property> properties;
  properties.insert(BatchSize);
  properties.insert(BaseDir);
  properties.insert(CaptureBluetooth);
  setSupportedProperties(properties);
  // Set the supported relationships
  std::set<core::Relationship> relationships;
  relationships.insert(Success);
  setSupportedRelationships(relationships);
}

void CapturePacket::onSchedule(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSessionFactory> &sessionFactory) {
  std::string value;
  if (context->getProperty(BatchSize.getName(), value)) {
    core::Property::StringToInt(value, pcap_batch_size_);
  }
  value = "";
  if (context->getProperty(BaseDir.getName(), value)) {
    base_dir_ = value;
  }

  value = "";
  if (context->getProperty(CaptureBluetooth.getName(), value)) {
    utils::StringUtils::StringToBool(value, capture_bluetooth_);
  }
  if (IsNullOrEmpty(base_dir_)) {
    base_dir_ = "/tmp/";
  }

  uuid_t dir_ext;

  id_generator_->generate(dir_ext);

  char id[37];
  uuid_unparse(dir_ext, id);
  base_path_ = id;

  const std::vector<pcpp::PcapLiveDevice*>& devList = pcpp::PcapLiveDeviceList::getInstance().getPcapLiveDevicesList();
  for (auto iter : devList) {
    const std::string name = iter->getName();

    if (!iter->open()) {
      logger_->log_error("Could not open device %s", name);
      continue;
    }

    if (!capture_bluetooth_) {
      if (name.find("bluetooth") != std::string::npos) {
        logger_->log_error("Skipping %s because blue tooth capture is not enabled", name);
        continue;
      }
    }

    if (name.find("dbus") != std::string::npos) {
      logger_->log_error("Skipping %s because dbus capture is disabled", name);
      continue;
    }

    if (iter->startCapture(packet_callback, mover.get())) {
      logger_->log_debug("Starting capture on %s", iter->getName());
      CapturePacketMechanism *aa = create_new_capture(getPath(), &pcap_batch_size_);
      logger_->log_trace("Creating packet capture in %s", aa->getFile());
      mover->source.enqueue(aa);
      device_list_.push_back(iter);
    }
  }

  if (IsNullOrEmpty(devList)) {
    logger_->log_error("Could not open any devices");
    throw std::exception();
  }
}

CapturePacket::~CapturePacket() {
}

void CapturePacket::onTrigger(const std::shared_ptr<core::ProcessContext> &context, const std::shared_ptr<core::ProcessSession> &session) {
  CapturePacketMechanism *capture;
  if (mover->sink.try_dequeue(capture)) {
    logger_->log_debug("Received packet capture in file %s", capture->getFile());
    auto ff = session->create();
    session->import(capture->getFile(), ff, false, 0);
    session->transfer(ff, Success);
    delete capture;
  } else {
    context->yield();
  }
}

}
/* namespace processors */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */
