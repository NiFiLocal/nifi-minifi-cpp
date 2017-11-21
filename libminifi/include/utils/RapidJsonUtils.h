/**
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
#ifndef LIBMINIFI_INCLUDE_UTILS_RAPIDJSONUTILS_H_
#define LIBMINIFI_INCLUDE_UTILS_RAPIDJSONUTILS_H_

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"

namespace org {
namespace apache {
namespace nifi {
namespace minifi {
namespace utils {

/**
 * Stateless String utility class.
 *
 * Design: Static class, with no member variables
 *
 * Purpose: Houses many useful string utilities.
 */
class RapidJsonUtils {
public:

  static void setJsonStr(const std::string& key, const std::string& value, rapidjson::Value& parent, rapidjson::Document::AllocatorType& alloc) { // NOLINT
    rapidjson::Value keyVal;
    rapidjson::Value valueVal;
    const char* c_key = key.c_str();
    const char* c_val = value.c_str();
  
    keyVal.SetString(c_key, key.length()), alloc;
    valueVal.SetString(c_val, value.length(), alloc);
  
    parent.AddMember(keyVal, valueVal, alloc);
  }
  
  static rapidjson::Value getStringValue(const std::string& value, rapidjson::Document::AllocatorType& alloc) { // NOLINT
    rapidjson::Value Val;
    Val.SetString(value.c_str(), value.length(), alloc);
    return Val;
  }
  
  static void appendJsonStr(const std::string& value, rapidjson::Value& parent, rapidjson::Document::AllocatorType& alloc) { // NOLINT
    rapidjson::Value valueVal;
    const char* c_val = value.c_str();
    valueVal.SetString(c_val, value.length(), alloc);
    parent.PushBack(valueVal, alloc);
  }

};

} /* namespace utils */
} /* namespace minifi */
} /* namespace nifi */
} /* namespace apache */
} /* namespace org */

#endif /* LIBMINIFI_INCLUDE_UTILS_RAPIDJSONUTILS_H_ */
