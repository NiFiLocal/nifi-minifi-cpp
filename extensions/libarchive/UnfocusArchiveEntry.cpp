/**
 * @file UnfocusArchiveEntry.cpp
 * UnfocusArchiveEntry class implementation
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
#include "processors/UnfocusArchiveEntry.h"

#include <string.h>
#include <archive.h>
#include <archive_entry.h>

#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <set>

#include "utils/file/FileManager.h"
#include "core/ProcessContext.h"
#include "core/ProcessSession.h"
#include "json/json.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace processors {

core::Relationship UnfocusArchiveEntry::Success("success", "success operational on the flow record");

bool UnfocusArchiveEntry::set_or_update_attr(std::shared_ptr<core::FlowFile> flowFile, const std::string& key, const std::string& value) const {
  if (flowFile->updateAttribute(key, value))
    return true;
  else
    return flowFile->addAttribute(key, value);
}

void UnfocusArchiveEntry::initialize() {
  //! Set the supported properties
  std::set<core::Property> properties;
  setSupportedProperties(properties);
  //! Set the supported relationships
  std::set<core::Relationship> relationships;
  relationships.insert(Success);
  setSupportedRelationships(relationships);
}

void UnfocusArchiveEntry::onTrigger(core::ProcessContext *context, core::ProcessSession *session) {
  auto flowFile = session->get();

  if (!flowFile) {
    return;
  }

  fileutils::FileManager file_man;

  // Get lens stack from attribute
  ArchiveMetadata lensArchiveMetadata;

  Json::Value lensStack;
  Json::Reader reader;

  std::string existingLensStack;

  if (flowFile->getAttribute("lens.archive.stack", existingLensStack)) {
    logger_->log_info("UnfocusArchiveEntry loading existing lens context");

    // TODO(calebj) handle any exceptions that might arise from working with JSON data
    if (!reader.parse(existingLensStack, lensStack)) {
      logger_->log_error("UnfocusArchiveEntry JSON parse error: %s", reader.getFormattedErrorMessages());
      context->yield();
      return;
    }

  } else {
    logger_->log_error("UnfocusArchiveEntry lens metadata not found");
    context->yield();
    return;
  }

  Json::Value metadataDoc = lensStack[lensStack.size() - 1];
  lensStack.resize(lensStack.size() - 1);

  lensArchiveMetadata.archiveFormatName.assign(metadataDoc["archive_format_name"].asString());
  lensArchiveMetadata.archiveFormat = metadataDoc["archive_format"].asUInt64();
  lensArchiveMetadata.focusedEntry = metadataDoc["focused_entry"].asString();

  for (auto itr = metadataDoc["archive_structure"].begin(); itr != metadataDoc["archive_structure"].end(); ++itr) {
    const auto &entryVal = *itr;
    ArchiveEntryMetadata metadata;
    metadata.tmpFileName = file_man.unique_file("/tmp/", true);
    metadata.entryName.assign(entryVal["entry_name"].asString());
    metadata.entryType = entryVal["entry_type"].asUInt64();
    metadata.entryPerm = entryVal["entry_perm"].asUInt64();
    metadata.entrySize = entryVal["entry_size"].asUInt64();
    metadata.entryUID = entryVal["entry_uid"].asUInt64();
    metadata.entryGID = entryVal["entry_gid"].asUInt64();
    metadata.entryMTime = entryVal["entry_mtime"].asUInt64();
    metadata.entryMTimeNsec = entryVal["entry_mtime_nsec"].asInt64();

    if (metadata.entryType == AE_IFREG)
      metadata.stashKey.assign(entryVal["stash_key"].asString());

    lensArchiveMetadata.entryMetadata.push_back(metadata);
  }

  // Export focused entry to tmp file
  for (const auto &entry : lensArchiveMetadata.entryMetadata) {
    if (entry.entryType != AE_IFREG || entry.entrySize == 0) {
      continue;
    }

    if (entry.entryName == lensArchiveMetadata.focusedEntry) {
      logger_->log_debug("UnfocusArchiveEntry exporting focused entry to %s", entry.tmpFileName);
      session->exportContent(entry.tmpFileName, flowFile, false);
    }
  }

  // Restore/export entries from stash, one-by-one, to tmp files
  for (const auto &entry : lensArchiveMetadata.entryMetadata) {
    if (entry.entryType != AE_IFREG || entry.entrySize == 0) {
      continue;
    }

    if (entry.entryName == lensArchiveMetadata.focusedEntry) {
      continue;
    }

    logger_->log_debug("UnfocusArchiveEntry exporting entry %s to %s", entry.stashKey, entry.tmpFileName);
    session->restore(entry.stashKey, flowFile);
    // TODO(calebj) implement copy export/don't worry about multiple claims/optimal efficiency for *now*
    session->exportContent(entry.tmpFileName, flowFile, false);
  }

  // Create archive by restoring each entry in the archive from tmp files
  WriteCallback cb(&lensArchiveMetadata);
  session->write(flowFile, &cb);

  // Set new/updated lens stack to attribute (already 'popped' with resize())
  Json::FastWriter writer;
  std::string stackStr = writer.write(lensStack);

  if (!flowFile->updateAttribute("lens.archive.stack", stackStr)) {
    flowFile->addAttribute("lens.archive.stack", stackStr);
  }

  // Update filename to that of original archive
  Json::Value archiveName = metadataDoc.get("archive_name", Json::Value(Json::nullValue));

  if (archiveName.isNull()) {
    flowFile->removeAttribute("filename");
    flowFile->removeAttribute("path");
    flowFile->removeAttribute("absolute.path");
  } else {
    std::string abs_path = archiveName.asString();
    std::size_t found = abs_path.find_last_of("/\\");
    std::string path = abs_path.substr(0, found);
    std::string name = abs_path.substr(found + 1);
    set_or_update_attr(flowFile, "filename", name);
    set_or_update_attr(flowFile, "path", path);
    set_or_update_attr(flowFile, "absolute.path", abs_path);
  }

  // Transfer to the relationship
  session->transfer(flowFile, Success);
}

UnfocusArchiveEntry::WriteCallback::WriteCallback(ArchiveMetadata *archiveMetadata) {
  logger_ = logging::LoggerFactory<UnfocusArchiveEntry>::getLogger();
  _archiveMetadata = archiveMetadata;
}

typedef struct {
  std::shared_ptr<io::BaseStream> stream;
} UnfocusArchiveEntryWriteData;

ssize_t UnfocusArchiveEntry::WriteCallback::write_cb(struct archive *, void *d, const void *buffer, size_t length) {
  auto data = static_cast<UnfocusArchiveEntryWriteData *>(d);
  const uint8_t *ui_buffer = static_cast<const uint8_t*>(buffer);
  return data->stream->writeData(const_cast<uint8_t*>(ui_buffer), length);
}

int64_t UnfocusArchiveEntry::WriteCallback::process(std::shared_ptr<io::BaseStream> stream) {
  auto outputArchive = archive_write_new();
  int64_t nlen = 0;

  archive_write_set_format(outputArchive, _archiveMetadata->archiveFormat);

  UnfocusArchiveEntryWriteData data;
  data.stream = stream;

  archive_write_open(outputArchive, &data, ok_cb, write_cb, ok_cb);

  // Iterate entries & write from tmp file to archive
  char buf[8192];
  struct stat st;
  struct archive_entry* entry;

  for (const auto &entryMetadata : _archiveMetadata->entryMetadata) {
    entry = archive_entry_new();
    logger_->log_info("UnfocusArchiveEntry writing entry %s", entryMetadata.entryName.c_str());

    if (entryMetadata.entryType == AE_IFREG && entryMetadata.entrySize > 0) {
      size_t stat_ok = stat(entryMetadata.tmpFileName.c_str(), &st);
      if (stat_ok != 0) {
        logger_->log_error("Error statting %s: %d", entryMetadata.tmpFileName, stat_ok);
      }
      archive_entry_copy_stat(entry, &st);
    }

    archive_entry_set_filetype(entry, entryMetadata.entryType);
    archive_entry_set_pathname(entry, entryMetadata.entryName.c_str());
    archive_entry_set_perm(entry, entryMetadata.entryPerm);
    archive_entry_set_size(entry, entryMetadata.entrySize);
    archive_entry_set_uid(entry, entryMetadata.entryUID);
    archive_entry_set_gid(entry, entryMetadata.entryGID);
    archive_entry_set_mtime(entry, entryMetadata.entryMTime, entryMetadata.entryMTimeNsec);

    logger_->log_info("Writing %s with type %d, perms %d, size %d, uid %d, gid %d, mtime %d,%d", entryMetadata.entryName.c_str(), entryMetadata.entryType, entryMetadata.entryPerm,
                      entryMetadata.entrySize, entryMetadata.entryUID, entryMetadata.entryGID, entryMetadata.entryMTime, entryMetadata.entryMTimeNsec);

    archive_write_header(outputArchive, entry);

    // If entry is regular file, copy entry contents
    if (entryMetadata.entryType == AE_IFREG && entryMetadata.entrySize > 0) {
      logger_->log_info("UnfocusArchiveEntry writing %d bytes of "
                        "data from tmp file %s to archive entry %s",
                        st.st_size, entryMetadata.tmpFileName.c_str(), entryMetadata.entryName.c_str());
      std::ifstream ifs(entryMetadata.tmpFileName, std::ifstream::in | std::ios::binary);

      while (ifs.good()) {
        ifs.read(buf, sizeof(buf));
        auto len = ifs.gcount();
        int64_t written = archive_write_data(outputArchive, buf, len);
        if (written < 0) {
          logger_->log_error("UnfocusArchiveEntry failed to write data to "
                             "archive entry %s due to error: %s",
                             entryMetadata.entryName.c_str(), archive_error_string(outputArchive));
        } else {
          nlen += written;
        }
      }

      ifs.close();

      // Remove the tmp file as we are through with it
      std::remove(entryMetadata.tmpFileName.c_str());
    }

    archive_entry_clear(entry);
  }

  archive_write_close(outputArchive);
  archive_entry_free(entry);
  archive_write_free(outputArchive);
  return nlen;
}

} /* namespace processors */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */
