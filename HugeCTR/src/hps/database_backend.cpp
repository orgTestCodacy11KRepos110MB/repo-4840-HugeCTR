/*
 * Copyright (c) 2021, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <rocksdb/sst_file_reader.h>

#include <base/debug/logger.hpp>
#include <fstream>
#include <hps/database_backend.hpp>
#include <sstream>

// TODO: Remove me!
#pragma GCC diagnostic error "-Wconversion"

namespace HugeCTR {

template <typename Key>
DatabaseBackendBase<Key>::DatabaseBackendBase(size_t max_set_batch_size)
    : max_set_batch_size_{max_set_batch_size} {}

template <typename Key>
size_t DatabaseBackendBase<Key>::contains(const std::string& table_name, const size_t num_keys,
                                          const Key* keys,
                                          const std::chrono::nanoseconds& time_budget) const {
  return 0;
}

template <typename Key>
size_t DatabaseBackendBase<Key>::fetch(const std::string& table_name, const size_t num_keys,
                                       const Key* const keys, const DatabaseHitCallback& on_hit,
                                       const DatabaseMissCallback& on_miss,
                                       const std::chrono::nanoseconds& time_budget) {
  for (size_t i = 0; i < num_keys; i++) {
    on_miss(i);
  }
  return 0;
}

template <typename Key>
size_t DatabaseBackendBase<Key>::fetch(const std::string& table_name, const size_t num_indices,
                                       const size_t* indices, const Key* const keys,
                                       const DatabaseHitCallback& on_hit,
                                       const DatabaseMissCallback& on_miss,
                                       const std::chrono::nanoseconds& time_budget) {
  const size_t* const indices_end = &indices[num_indices];
  for (; indices != indices_end; indices++) {
    on_miss(*indices);
  }
  return 0;
}

template <typename Key>
size_t DatabaseBackendBase<Key>::evict(const std::vector<std::string>& table_names) {
  size_t n = 0;
  for (const std::string& table_name : table_names) {
    n += evict(table_name);
  }
  return n;
}

template <typename Key>
void DatabaseBackendBase<Key>::dump(const std::string& table_name, const std::string& path,
                                    DatabaseTableDumpFormat_t format) {
  // Resolve format if none specificed.
  if (format == DatabaseTableDumpFormat_t::Automatic) {
    const std::string ext = std::filesystem::path(path).extension();
    if (ext == ".bin") {
      format = DatabaseTableDumpFormat_t::Raw;
    } else if (ext == ".sst") {
      format = DatabaseTableDumpFormat_t::SST;
    } else {
      HCTR_DIE("Unsupported file extension!");
    }
  }

  switch (format) {
    case DatabaseTableDumpFormat_t::Raw: {
      std::ofstream file(path, std::ios::binary);

      // Write header.
      static constexpr char magic[] = {'b', 'i', 'n', '\0'};
      file.write(magic, sizeof(magic));

      const uint32_t version = 1;
      file.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));

      const uint32_t key_size = sizeof(Key);
      file.write(reinterpret_cast<const char*>(&key_size), sizeof(uint32_t));

      // Write data.
      dump_bin(table_name, file);
    } break;

    case DatabaseTableDumpFormat_t::SST: {
      rocksdb::Options options;
      rocksdb::EnvOptions env_options;
      rocksdb::SstFileWriter file(env_options, options);
      HCTR_ROCKSDB_CHECK(file.Open(path));

      // Write data.
      dump_sst(table_name, file);

      HCTR_ROCKSDB_CHECK(file.Finish());
    } break;

    default: {
      HCTR_DIE("Unsupported DB table dump format!");
    } break;
  }
}

template <typename Key>
void DatabaseBackendBase<Key>::load_dump(const std::string& table_name, const std::string& path) {
  const std::string ext = std::filesystem::path(path).extension();
  if (ext == ".bin") {
    load_dump_bin(table_name, path);
  } else if (ext == ".sst") {
    load_dump_sst(table_name, path);
  } else {
    HCTR_DIE("Unsupported file extension!");
  }
}

template <typename Key>
void DatabaseBackendBase<Key>::load_dump_bin(const std::string& table_name,
                                             const std::string& path) {
  std::ifstream file{path, std::ios::binary};
  HCTR_CHECK(file.is_open());

  // Parse header.
  char magic[4];
  file.read(magic, sizeof(magic));
  HCTR_CHECK(file);
  HCTR_CHECK(magic[0] == 'b' && magic[1] == 'i' && magic[2] == 'n' && magic[3] == '\0');

  uint32_t version;
  file.read(reinterpret_cast<char*>(&version), sizeof(uint32_t));
  HCTR_CHECK(file);
  HCTR_CHECK(version == 1);

  uint32_t key_size;
  file.read(reinterpret_cast<char*>(&key_size), sizeof(uint32_t));
  HCTR_CHECK(file);
  HCTR_CHECK(key_size == sizeof(Key));

  uint32_t value_size;
  file.read(reinterpret_cast<char*>(&value_size), sizeof(uint32_t));
  if (file.eof()) {
    return;
  }
  HCTR_CHECK(file);

  std::vector<Key> keys;
  keys.reserve(max_set_batch_size_);
  std::vector<char> values;
  values.reserve(max_set_batch_size_ * value_size);

  std::vector<char> tmp(std::max(sizeof(Key), static_cast<size_t>(value_size)));
  while (!file.eof()) {
    // Read key.
    file.read(tmp.data(), sizeof(Key));
    if (file.eof()) {
      break;
    }
    HCTR_CHECK(file.good());
    keys.emplace_back(*reinterpret_cast<Key*>(tmp.data()));

    // Read value.
    file.read(tmp.data(), tmp.size());
    HCTR_CHECK(file.good() || file.eof());
    values.insert(values.end(), tmp.begin(), tmp.end());

    // Put batch into table.
    if (keys.size() >= max_set_batch_size_) {
      insert(table_name, keys.size(), keys.data(), values.data(), value_size);
      keys.clear();
      values.clear();
    }
  }

  // Fill remaining KVs into table.
  if (!keys.empty()) {
    insert(table_name, keys.size(), keys.data(), values.data(), value_size);
  }
}

template <typename Key>
void DatabaseBackendBase<Key>::load_dump_sst(const std::string& table_name,
                                             const std::string& path) {
  rocksdb::Options options;
  rocksdb::SstFileReader file{options};
  HCTR_ROCKSDB_CHECK(file.Open(path));

  rocksdb::ReadOptions read_options;
  std::unique_ptr<rocksdb::Iterator> it{file.NewIterator(read_options)};
  it->SeekToFirst();

  size_t value_size = 0;
  std::vector<Key> keys;
  std::vector<char> values;

  for (; it->Valid(); it->Next()) {
    // Parse key.
    const rocksdb::Slice& k_view = it->key();
    HCTR_CHECK(k_view.size() == sizeof(Key));
    keys.emplace_back(*reinterpret_cast<const Key*>(k_view.data()));

    // Parse value.
    const rocksdb::Slice& v_view = it->value();
    if (value_size == 0) {
      HCTR_CHECK(v_view.size() != 0);
      value_size = v_view.size();
    } else {
      HCTR_CHECK(v_view.size() == value_size);
    }
    const char* v = v_view.data();
    values.insert(values.end(), v, &v[value_size]);

    // If buffer full, insert.
    if (keys.size() >= max_set_batch_size_) {
      insert(table_name, keys.size(), keys.data(), values.data(), value_size);
      keys.clear();
      values.clear();
    }
  }

  // If buffer not yet empty.
  if (!keys.empty()) {
    insert(table_name, keys.size(), keys.data(), values.data(), value_size);
  }
}

template class DatabaseBackendBase<unsigned int>;
template class DatabaseBackendBase<long long>;

DatabaseBackendError::DatabaseBackendError(const std::string& backend, const size_t partition,
                                           const std::string& what)
    : backend_{backend}, partition_{partition}, what_{what} {}

std::string DatabaseBackendError::to_string() const {
  std::ostringstream os;
  os << backend_ << " DB Backend error (partition = " << partition_ << "): " << what_;
  return os.str();
}

}  // namespace HugeCTR