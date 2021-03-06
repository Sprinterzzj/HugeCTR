/*
 * Copyright (c) 2019, NVIDIA CORPORATION.
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


#pragma once

#include "HugeCTR/include/common.hpp"
#include "HugeCTR/include/csr_chunk.hpp"
#include "HugeCTR/include/heap.hpp"

#include <math.h>
#include <stdlib.h>

#include "utest/embedding/cpu_hashtable.hpp"

using namespace HugeCTR;

template <typename TypeHashKey>
class SparseEmbeddingHashCpu {
  using TypeHashValueIndex = TypeHashKey;

 private:
  int batchsize_;
  int max_feature_num_;
  int vocabulary_size_;
  int embedding_vec_size_;
  int slot_num_;
  int combiner_;
  int optimizer_;
  float lr_;
  uint64_t times_;
  const float adam_beta1_ = 0.9f;
  const float adam_beta2_ = 0.999f;
  const float adam_epsilon_ = 1e-8f;
  const float momentum_factor_ = 0.9f;
  const float nesterov_mu_ = 0.9f;

  TypeHashKey *row_offset_;
  TypeHashKey *hash_key_;
  TypeHashValueIndex *hash_value_index_;
  TypeHashValueIndex *hash_value_index_undup_;
  TypeHashValueIndex *hash_value_index_undup_offset_;
  TypeHashKey *sample_id_;
  TypeHashKey *hash_table_key_;
  TypeHashValueIndex *hash_table_value_index_;
  float *hash_table_value_;
  float *embedding_feature_;
  float *wgrad_;
  float *opt_m_;
  float *opt_v_;
  float *opt_momentum_;
  float *opt_accm_;

  std::ifstream &csr_stream_;
  long long csr_stream_offset_ = 0;
  int label_dim_;

  HashTableCpu<TypeHashKey, TypeHashValueIndex> *hash_table_;

 public:
  SparseEmbeddingHashCpu(int batchsize, int max_feature_num, int vocabulary_size,
                         int embedding_vec_size, int slot_num, int combiner, int optimizer,
                         float lr, std::ifstream &hash_table_stream, std::ifstream &csr_stream,
                         int label_dim);
  ~SparseEmbeddingHashCpu();
  void load_data_from_csr();
  void forward();
  void backward();
  void update_params();

  void cpu_forward_sum(const int batchsize,            // in
                       const int slot_num,             // in
                       const int embedding_vec_size,   // in
                       const TypeHashKey *row_offset,  // in  the row offsets in CSR format
                       const TypeHashValueIndex *hash_value_index,  // in
                       const float *hash_table_value,               // in
                       float *embedding_feature                     // out
  );

  void cpu_forward_mean(const int batchsize,            // in
                        const int slot_num,             // in
                        const int embedding_vec_size,   // in
                        const TypeHashKey *row_offset,  // in  the row offsets in CSR format
                        const TypeHashValueIndex *hash_value_index,  // in
                        const float *hash_table_value,               // in
                        float *embedding_feature                     // out
  );

  void cpu_backward_sum(const int batchsize,           // in
                        const int slot_num,            // in
                        const int embedding_vec_size,  // in
                        const float *top_grad,         // in
                        float *wgrad                   // out  size: row_offset[nBatch]
  );

  void cpu_backward_mean(const int batchsize,            // in
                         const int slot_num,             // in
                         const int embedding_vec_size,   // in
                         const TypeHashKey *row_offset,  // in   the row offsets in CSR format
                         const float *top_grad,          // in
                         float *wgrad                    // out  size: row_offset[nBatch]
  );

  void cpu_csr_extend(const int batchsize, const int slot_num, const TypeHashKey *row_offset,
                      TypeHashKey *sample_id);

  void cpu_swap(TypeHashKey &a, TypeHashKey &b);

  void cpu_csr_sort(const int nnz, TypeHashKey *hash_value_index,
                    TypeHashKey *hash_value_index_pair);

  int cpu_csr_unduplicate(const int nnz, const TypeHashValueIndex *hash_value_index,
                          TypeHashValueIndex *hash_value_index_undup,
                          TypeHashValueIndex *hash_value_index_undup_offset);

  void cpu_optimizer_adam(const int feature_num_undup, const int embedding_vec_size,
                          const TypeHashValueIndex *hash_value_index_undup,
                          const TypeHashValueIndex *hash_value_index_undup_offset,
                          const TypeHashKey *sample_id, const float *wgrad, float *hash_table_value,
                          float *m, float *v, const float alpha_t, const float beta1,
                          const float beta2, const float epsilon);

  void cpu_optimizer_momentum(const int feature_num_undup, const int embedding_vec_size,
                              const TypeHashValueIndex *hash_value_index_undup,
                              const TypeHashValueIndex *hash_value_index_undup_offset,
                              const TypeHashKey *sample_id, const float *wgrad,
                              float *hash_table_value, float *momentum_ptr, const float factor,
                              const float lr);

  void cpu_optimizer_nesterov(const int feature_num_undup, const int embedding_vec_size,
                              const TypeHashValueIndex *hash_value_index_undup,
                              const TypeHashValueIndex *hash_value_index_undup_offset,
                              const TypeHashKey *sample_id, const float *wgrad,
                              float *hash_table_value, float *accm_ptr, const float mu,
                              const float lr);

  // only used for results check
  float *get_embedding_feature_ptr() { return embedding_feature_; }
  float *get_wgrad_ptr() { return wgrad_; }
  TypeHashKey *get_hash_table_key_ptr() { return hash_table_key_; }
  TypeHashValueIndex *get_hash_table_value_index_ptr() { return hash_table_value_index_; }
  float *get_hash_table_value_ptr() { return hash_table_value_; }

};  // end of class SparseEmbeddingHashCpu

template <typename TypeHashKey>
SparseEmbeddingHashCpu<TypeHashKey>::SparseEmbeddingHashCpu(
    int batchsize, int max_feature_num, int vocabulary_size, int embedding_vec_size, int slot_num,
    int combiner, int optimizer, float lr, std::ifstream &hash_table_stream,
    std::ifstream &csr_stream, int label_dim)
    : batchsize_(batchsize),
      max_feature_num_(max_feature_num),
      vocabulary_size_(vocabulary_size),
      embedding_vec_size_(embedding_vec_size),
      slot_num_(slot_num),
      combiner_(combiner),
      optimizer_(optimizer),
      lr_(lr),
      csr_stream_(csr_stream),
      label_dim_(label_dim) {
#ifndef NDEBUG
  PRINT_FUNC_NAME_();
#endif

  // load hash_table_key and hash_table_value from file
  if (!hash_table_stream.is_open()) {
    ERROR_MESSAGE_("Error: hash table file open failed");
    return;
  }

  // define size
  long long hash_table_key_size_in_B = (long long)vocabulary_size_ * sizeof(TypeHashKey);
  long long hash_table_value_size_in_B =
      (long long)vocabulary_size_ * (long long)embedding_vec_size_ * sizeof(float);
  long long hash_table_size_in_B = hash_table_key_size_in_B + hash_table_value_size_in_B;
  long long embedding_feature_size_in_B =
      batchsize_ * slot_num_ * embedding_vec_size_ * sizeof(float);

  // malloc memory
  hash_table_ = new HashTableCpu<TypeHashKey, TypeHashValueIndex>();
  hash_table_value_ = (float *)malloc(hash_table_value_size_in_B);  // embedding table
  hash_table_key_ = (TypeHashKey *)malloc(hash_table_key_size_in_B);
  row_offset_ = (TypeHashKey *)malloc((batchsize_ * slot_num_ + 1) * sizeof(TypeHashKey));
  hash_key_ = (TypeHashKey *)malloc(batchsize_ * max_feature_num_ * sizeof(TypeHashKey));
  hash_value_index_ =
      (TypeHashValueIndex *)malloc(batchsize_ * max_feature_num_ * sizeof(TypeHashKey));
  hash_value_index_undup_ =
      (TypeHashValueIndex *)malloc(batchsize_ * max_feature_num_ * sizeof(TypeHashKey));
  hash_value_index_undup_offset_ =
      (TypeHashValueIndex *)malloc((batchsize_ * max_feature_num_ + 1) * sizeof(TypeHashKey));
  sample_id_ = (TypeHashKey *)malloc(batchsize_ * max_feature_num_ * sizeof(TypeHashKey));
  embedding_feature_ = (float *)malloc(embedding_feature_size_in_B);
  wgrad_ = (float *)malloc(embedding_feature_size_in_B);
  opt_m_ = (float *)malloc(hash_table_value_size_in_B);
  memset(opt_m_, 0, hash_table_value_size_in_B);
  opt_v_ = (float *)malloc(hash_table_value_size_in_B);
  memset(opt_v_, 0, hash_table_value_size_in_B);
  opt_momentum_ = (float *)malloc(hash_table_value_size_in_B);
  memset(opt_momentum_, 0, hash_table_value_size_in_B);
  opt_accm_ = (float *)malloc(hash_table_value_size_in_B);
  memset(opt_accm_, 0, hash_table_value_size_in_B);

  hash_table_value_index_ = (TypeHashValueIndex *)malloc(hash_table_key_size_in_B);
  for (TypeHashValueIndex i = 0; i < vocabulary_size_; i++) {
    hash_table_value_index_[i] = (TypeHashValueIndex)i;
  }

  int hash_table_tile_size_in_B = (sizeof(TypeHashKey) + sizeof(float) * embedding_vec_size_);
  char *hash_table_tile = (char *)malloc(hash_table_tile_size_in_B);

  // read hash table
  hash_table_stream.seekg(0, std::ios::end);
  long long file_size_in_B = hash_table_stream.tellg();
  hash_table_stream.seekg(0, std::ios::beg);
  if (file_size_in_B < hash_table_size_in_B) {
    ERROR_MESSAGE_("Error: hash table file size is smaller than embedding_table_size required");
    return;
  }

  long long tile_num = 0;
  while (hash_table_stream.peek() != EOF) {
    hash_table_stream.read(hash_table_tile, hash_table_tile_size_in_B);

    // get hash_table_key and hash_table_value
    memcpy(hash_table_key_ + tile_num, hash_table_tile, sizeof(TypeHashKey));
    memcpy(hash_table_value_ + tile_num * embedding_vec_size_,
           hash_table_tile + sizeof(TypeHashKey), embedding_vec_size_ * sizeof(float));

    tile_num++;
  }
  hash_table_stream.close();

  // insert <key,value_index> into HashTableCpu
  hash_table_->insert(hash_table_key_, hash_table_value_index_, vocabulary_size_);

  // for csr file reading
  if (!csr_stream.is_open()) {
    ERROR_MESSAGE_("Error: csr file open failed");
  }
  csr_stream_offset_ = sizeof(DataSetHeader);

  // for optimizer
  times_ = 0;

  // release
  free(hash_table_tile);

  return;
}

template <typename TypeHashKey>
SparseEmbeddingHashCpu<TypeHashKey>::~SparseEmbeddingHashCpu() {
#ifndef NDEBUG
  PRINT_FUNC_NAME_();
#endif

  free(row_offset_);
  free(hash_key_);
  free(hash_value_index_);
  free(hash_value_index_undup_);
  free(hash_value_index_undup_offset_);
  free(sample_id_);
  free(hash_table_value_);
  free(hash_table_key_);
  free(hash_table_value_index_);
  free(embedding_feature_);
  free(wgrad_);
  free(opt_m_);
  free(opt_v_);
  free(opt_momentum_);
  free(opt_accm_);
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::load_data_from_csr() {
#ifndef NDEBUG
  PRINT_FUNC_NAME_();
#endif

  // read csr from file
  if (!csr_stream_.is_open()) {
    ERROR_MESSAGE_("Error: csr_stream can not be opened for read");
  }

  // read 1 batch
  row_offset_[0] = 0;
  for (int row_num = 0; row_num < batchsize_; row_num++) {
    csr_stream_offset_ += label_dim_ * sizeof(int);
    csr_stream_.seekg(csr_stream_offset_);

    for (int slot = 0; slot < slot_num_; slot++) {
      int nnz;
      csr_stream_.read((char *)&nnz, sizeof(int));
      csr_stream_offset_ += sizeof(int);
      row_offset_[row_num * slot_num_ + slot + 1] = row_offset_[row_num * slot_num_ + slot] + nnz;
      csr_stream_.read((char *)(hash_key_ + row_offset_[row_num * slot_num_ + slot]),
                       sizeof(TypeHashKey) * nnz);
      csr_stream_offset_ += nnz * sizeof(TypeHashKey);
    }
  }
}

// CPU implementation of forward computation of embedding lookup and sum reduction with sparse
// matrix as input
template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::cpu_forward_sum(
    const int batchsize,                         // in
    const int slot_num,                          // in
    const int embedding_vec_size,                // in
    const TypeHashKey *row_offset,               // in  the row offsets in CSR format
    const TypeHashValueIndex *hash_value_index,  // in
    const float *hash_table_value,               // in
    float *embedding_feature                     // out
) {
  for (int user = 0; user < batchsize * slot_num; user++) {
    int feature_num = row_offset[user + 1] - row_offset[user];

    for (int vec = 0; vec < embedding_vec_size; vec++) {
      float sum = 0.0f;

      for (int item = 0; item < feature_num; item++) {
        TypeHashValueIndex nFeatureIndex = hash_value_index[row_offset[user] + item];

        sum += hash_table_value[nFeatureIndex * embedding_vec_size + vec];

        // printf("user=%d, vec=%d, feature_num=%d, item=%d, nFeatureIndex=%d, value=%f\n", user,
        // vec, feature_num, item, nFeatureIndex,  hash_table_value[nFeatureIndex *
        // embedding_vec_size + vec]);
      }

      embedding_feature[user * embedding_vec_size + vec] = sum;
    }
  }
}

// CPU implementation of forward computation of embedding lookup and mean reduction with sparse
// matrix as input
template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::cpu_forward_mean(
    const int batchsize,                         // in
    const int slot_num,                          // in
    const int embedding_vec_size,                // in
    const TypeHashKey *row_offset,               // in  the row offsets in CSR format
    const TypeHashValueIndex *hash_value_index,  // in
    const float *hash_table_value,               // in
    float *embedding_feature                     // out
) {
  for (int user = 0; user < batchsize * slot_num; user++) {
    int feature_num = row_offset[user + 1] - row_offset[user];

    for (int vec = 0; vec < embedding_vec_size; vec++) {
      float mean = 0.0f;

      for (int item = 0; item < feature_num; item++) {
        TypeHashValueIndex nFeatureIndex = hash_value_index[row_offset[user] + item];

        mean += hash_table_value[nFeatureIndex * embedding_vec_size + vec];
      }

      if (feature_num > 1) {
        mean /= (float)feature_num;
      }

      embedding_feature[user * embedding_vec_size + vec] = mean;
    }
  }
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::forward() {
#ifndef NDEBUG
  PRINT_FUNC_NAME_();
#endif

  load_data_from_csr();

  if (combiner_ == 0) {
    // do hash_table get() value_index by key
    hash_table_->get(hash_key_, hash_value_index_, row_offset_[batchsize_ * slot_num_]);

    cpu_forward_sum(batchsize_, slot_num_, embedding_vec_size_, row_offset_, hash_value_index_,
                    hash_table_value_, embedding_feature_);
  } else if (combiner_ == 1) {
    cpu_forward_mean(batchsize_, slot_num_, embedding_vec_size_, row_offset_, hash_key_,
                     hash_table_value_, embedding_feature_);
  } else {
  }
}

// CPU implementation of backward computation of embedding layer with sparse matrix as input
template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::cpu_backward_sum(
    const int batchsize,           // in
    const int slot_num,            // in
    const int embedding_vec_size,  // in
    const float *top_grad,         // in
    float *wgrad                   // out  size: row_offset[nBatch]
) {
  for (int user = 0; user < batchsize * slot_num; user++) {
    for (int vec = 0; vec < embedding_vec_size; vec++) {
      wgrad[user * embedding_vec_size + vec] = top_grad[user * embedding_vec_size + vec];
    }
  }
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::cpu_backward_mean(
    const int batchsize,            // in
    const int slot_num,             // in
    const int embedding_vec_size,   // in
    const TypeHashKey *row_offset,  // in   the row offsets in CSR format
    const float *top_grad,          // in
    float *wgrad                    // out  size: row_offset[nBatch]
) {
  for (int user = 0; user < batchsize * slot_num; user++) {
    int feature_num = row_offset[user + 1] - row_offset[user];
    float scaler = 1.0f;
    if (feature_num > 1) {
      scaler = 1.0f / (float)feature_num;
    }

    for (int vec = 0; vec < embedding_vec_size; vec++) {
      wgrad[user * embedding_vec_size + vec] = top_grad[user * embedding_vec_size + vec] * scaler;
    }
  }
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::backward() {
#ifndef NDEBUG
  PRINT_FUNC_NAME_();
#endif

  if (combiner_ == 0) {
    cpu_backward_sum(batchsize_, slot_num_, embedding_vec_size_, embedding_feature_, wgrad_);
  } else if (combiner_ == 1) {
    cpu_backward_mean(batchsize_, slot_num_, embedding_vec_size_, row_offset_, embedding_feature_,
                      wgrad_);
  } else {
  }
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::cpu_csr_extend(const int batchsize, const int slot_num,
                                                         const TypeHashKey *row_offset,
                                                         TypeHashKey *sample_id) {
  for (int i = 0; i < batchsize * slot_num; i++) {  // loop of sample id
    int feature_num = row_offset[i + 1] - row_offset[i];
    for (int j = 0; j < feature_num; j++) {
      sample_id[row_offset[i] + j] = i;  // record sample id coresponding to each feature
    }
  }
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::cpu_swap(TypeHashKey &a, TypeHashKey &b) {
  TypeHashKey temp = a;
  a = b;
  b = temp;
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::cpu_csr_sort(const int nnz,
                                                       TypeHashValueIndex *hash_value_index,
                                                       TypeHashValueIndex *hash_value_index_pair) {
  // odd even sort
  for (int i = 0; i < nnz; i++) {
    if (i % 2 == 0) {  // even
      for (int j = 1; j < nnz; j += 2) {
        if (hash_value_index[j] < hash_value_index[j - 1]) {
          cpu_swap(hash_value_index[j], hash_value_index[j - 1]);
          cpu_swap(hash_value_index_pair[j], hash_value_index_pair[j - 1]);
        }
      }
    } else {  // odd
      for (int j = 1; j < nnz - 1; j += 2) {
        if (hash_value_index[j] > hash_value_index[j + 1]) {
          cpu_swap(hash_value_index[j], hash_value_index[j + 1]);
          cpu_swap(hash_value_index_pair[j], hash_value_index_pair[j + 1]);
        }
      }
    }
  }
}

template <typename TypeHashKey>
int SparseEmbeddingHashCpu<TypeHashKey>::cpu_csr_unduplicate(
    const int nnz, const TypeHashValueIndex *hash_value_index,
    TypeHashValueIndex *hash_value_index_undup, TypeHashValueIndex *hash_value_index_undup_offset) {
  hash_value_index_undup_offset[0] = 0;

  int counter = 0;
  for (int i = 0; i < nnz - 1; i++) {
    if (hash_value_index[i] != hash_value_index[i + 1]) {
      hash_value_index_undup[counter] = hash_value_index[i];
      hash_value_index_undup_offset[counter + 1] = i + 1;
      counter++;
    }
  }

  hash_value_index_undup[counter] = hash_value_index[nnz - 1];
  hash_value_index_undup_offset[counter + 1] = nnz;
  counter++;

  return counter;
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::cpu_optimizer_adam(
    const int feature_num_undup, const int embedding_vec_size,
    const TypeHashValueIndex *hash_value_index_undup,
    const TypeHashValueIndex *hash_value_index_undup_offset, const TypeHashKey *sample_id,
    const float *wgrad, float *hash_table_value, float *m, float *v, const float alpha_t,
    const float beta1, const float beta2, const float epsilon) {
  for (int i = 0; i < feature_num_undup; i++) {
    TypeHashValueIndex cur_offset = hash_value_index_undup_offset[i];
    TypeHashValueIndex sample_num = hash_value_index_undup_offset[i + 1] - cur_offset;
    TypeHashValueIndex row_index = hash_value_index_undup[i];

    for (int j = 0; j < embedding_vec_size; j++) {
      float gi = 0.0f;
      for (int k = 0; k < sample_num; k++) {
        int sample_index = sample_id[cur_offset + k];
        gi += wgrad[sample_index * embedding_vec_size + j];
      }

      TypeHashValueIndex feature_index = row_index * embedding_vec_size + j;
      float mi = beta1 * m[feature_index] + (1.0f - beta1) * gi;
      float vi = beta2 * v[feature_index] + (1.0f - beta2) * gi * gi;
      m[feature_index] = mi;
      v[feature_index] = vi;

      float weight_diff = -alpha_t * mi / (sqrtf(vi) + epsilon);

      hash_table_value[feature_index] += weight_diff;
    }
  }
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::cpu_optimizer_momentum(
    const int feature_num_undup, const int embedding_vec_size,
    const TypeHashValueIndex *hash_value_index_undup,
    const TypeHashValueIndex *hash_value_index_undup_offset, const TypeHashKey *sample_id,
    const float *wgrad, float *hash_table_value, float *momentum_ptr, const float factor,
    const float lr) {
  for (int i = 0; i < feature_num_undup; i++) {
    TypeHashValueIndex cur_offset = hash_value_index_undup_offset[i];
    TypeHashValueIndex sample_num = hash_value_index_undup_offset[i + 1] - cur_offset;
    TypeHashValueIndex row_index = hash_value_index_undup[i];

    for (int j = 0; j < embedding_vec_size; j++) {
      float gi = 0.0f;
      for (int k = 0; k < sample_num; k++) {
        int sample_index = sample_id[cur_offset + k];
        gi += wgrad[sample_index * embedding_vec_size + j];
      }

      TypeHashValueIndex feature_index = row_index * embedding_vec_size + j;
      float mo = factor * momentum_ptr[feature_index] - lr * gi;
      momentum_ptr[feature_index] = mo;

      hash_table_value[feature_index] += mo;
    }
  }
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::cpu_optimizer_nesterov(
    const int feature_num_undup, const int embedding_vec_size,
    const TypeHashValueIndex *hash_value_index_undup,
    const TypeHashValueIndex *hash_value_index_undup_offset, const TypeHashKey *sample_id,
    const float *wgrad, float *hash_table_value, float *accm_ptr, const float mu, const float lr) {
  for (int i = 0; i < feature_num_undup; i++) {
    TypeHashValueIndex cur_offset = hash_value_index_undup_offset[i];
    TypeHashValueIndex sample_num = hash_value_index_undup_offset[i + 1] - cur_offset;
    TypeHashValueIndex row_index = hash_value_index_undup[i];

    for (int j = 0; j < embedding_vec_size; j++) {
      float gi = 0.0f;
      for (int k = 0; k < sample_num; k++) {
        int sample_index = sample_id[cur_offset + k];
        gi += wgrad[sample_index * embedding_vec_size + j];
      }

      TypeHashValueIndex feature_index = row_index * embedding_vec_size + j;
      float accm_old = accm_ptr[feature_index];
      float accm_new = mu * accm_old - lr * gi;
      accm_ptr[feature_index] = accm_new;
      float weight_diff = -mu * accm_old + (1.0f + mu) * accm_new;

      hash_table_value[feature_index] += weight_diff;
    }
  }
}

template <typename TypeHashKey>
void SparseEmbeddingHashCpu<TypeHashKey>::update_params() {
#ifndef NDEBUG
  PRINT_FUNC_NAME_();
#endif

  // step1: extend sample IDs
  cpu_csr_extend(batchsize_, slot_num_, row_offset_, sample_id_);

  // step2: do hash table get() value_index by key
  int nnz = row_offset_[batchsize_ * slot_num_];
  hash_table_->get(hash_key_, hash_value_index_, nnz);

  // step3: sort by value_index
  cpu_csr_sort(nnz, hash_value_index_, sample_id_);

  // step4: unduplicate by value_index
  int feature_num_undup = cpu_csr_unduplicate(nnz, hash_value_index_, hash_value_index_undup_,
                                              hash_value_index_undup_offset_);

  // step5: sort by value_index   no need to do this for CPU
  cpu_csr_sort(feature_num_undup, hash_value_index_undup_, hash_value_index_undup_offset_);

  // step6: update params
  if (optimizer_ == 0) {
    times_++;
    const float alpha_t =
        lr_ * sqrt(1.0f - pow(adam_beta2_, times_)) / (1.0f - pow(adam_beta1_, times_));
    cpu_optimizer_adam(feature_num_undup, embedding_vec_size_, hash_value_index_undup_,
                       hash_value_index_undup_offset_, sample_id_, wgrad_, hash_table_value_,
                       opt_m_, opt_v_, alpha_t, adam_beta1_, adam_beta2_, adam_epsilon_);
  } else if (optimizer_ == 1) {
    cpu_optimizer_momentum(feature_num_undup, embedding_vec_size_, hash_value_index_undup_,
                           hash_value_index_undup_offset_, sample_id_, wgrad_, hash_table_value_,
                           opt_momentum_, momentum_factor_, lr_);

  } else if (optimizer_ == 2) {
    cpu_optimizer_nesterov(feature_num_undup, embedding_vec_size_, hash_value_index_undup_,
                           hash_value_index_undup_offset_, sample_id_, wgrad_, hash_table_value_,
                           opt_accm_, nesterov_mu_, lr_);

  } else {
    printf("Error: optimizer not support in CPU version\n");
    return;
  }
}
