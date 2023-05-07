// Copyright (c) 2019 Google LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "streaming_client.h"

#include <google/protobuf/util/json_util.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "file_reader.h"
#include "file_writer.h"
#include "pipe_reader.h"
#include "proto_processor.h"
#include "proto_writer.h"
#include "gflags/gflags.h"
#include "glog/logging.h"

DEFINE_string(config, "", "Config request JSON object.");
DEFINE_string(endpoint, "dns:///videointelligence.googleapis.com",
              "API endpoint to connect to.");
DEFINE_string(local_storage_annotation_result, "",
              "Local Storage: annotation result path.");
DEFINE_string(local_storage_video, "", "Local Storage: video path.");
DEFINE_int32(timeout, 3600, "GRPC deadline (default: 1 hour).");
DEFINE_bool(use_pipe, false, "Whether reading video contents from a pipe.");
DEFINE_string(video_path, "", "Input video path.");
DEFINE_string(
    font_type, "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
    "Font type of annotation results that are onverlayed on original video");

namespace api {
namespace video {

namespace {
using ::google::cloud::videointelligence::v1p3beta1::ObjectTrackingAnnotation;
using ::google::cloud::videointelligence::v1p3beta1::STREAMING_LABEL_DETECTION;
using ::google::cloud::videointelligence::v1p3beta1::
    STREAMING_SHOT_CHANGE_DETECTION;
using ::google::cloud::videointelligence::v1p3beta1::
    StreamingAnnotateVideoRequest;
using ::google::cloud::videointelligence::v1p3beta1::
    StreamingAnnotateVideoResponse;
using ::google::cloud::videointelligence::v1p3beta1::StreamingFeature;
using ::google::cloud::videointelligence::v1p3beta1::StreamingVideoConfig;
using ::google::cloud::videointelligence::v1p3beta1::
    StreamingVideoIntelligenceService;
using ::google::protobuf::util::JsonStringToMessage;
using ::grpc::ClientContext;
using ::grpc::ClientReaderWriter;


}  // namespace

// Maximum data chunks read: 1 MByte.
constexpr int kDataChunk = 1 * 1024 * 1024;

bool StreamingClient::Init(int* argc_ptr, char*** argv_ptr) {
  gflags::ParseCommandLineFlags(argc_ptr, argv_ptr, true);

  auto ssl_credentials = grpc::GoogleDefaultCredentials();
  channel_ = grpc::CreateChannel(FLAGS_endpoint, ssl_credentials);
  LOG(INFO) << "Connecting to " << FLAGS_endpoint << "...";

  // Creates a stub call.
  stub_ = StreamingVideoIntelligenceService::NewStub(channel_);

  std::chrono::system_clock::time_point timeout =
      std::chrono::system_clock::now() + std::chrono::seconds(FLAGS_timeout);
  context_.set_deadline(timeout);

  // Inits and starts a gRPC client.
  stream_ = std::shared_ptr<ClientReaderWriter<StreamingAnnotateVideoRequest,
                                               StreamingAnnotateVideoResponse>>(
      stub_->StreamingAnnotateVideo(&context_));
  grpc_connectivity_state state = channel_->GetState(/*try_to_connect*/ true);
  if (state != GRPC_CHANNEL_READY) {
    LOG(ERROR) << "grpc_connectivity_state error: " << std::to_string(state);
    return false;
  }


  return true;
}

StreamingClient::~StreamingClient() {
}

bool StreamingClient::Run() {
  if (!SendConfig()) {
    return false;
  }
  std::thread reader([this] { ReadResponse(); });
  bool status = SendContent();
  reader.join();

  auto grpc_status = stream_->Finish();
  if (!grpc_status.ok()) {
    LOG(ERROR) << "StreamingAnnotateVideo RPC failed: Code("
               << grpc_status.error_code()
               << "): " << grpc_status.error_message();
    status = false;
  }
  return status;
}

void StreamingClient::ReadResponse() {
  StreamingAnnotateVideoResponse resp;
  int total_responses_received = 0;
  bool enable_local_storage_annotation_result =
      (FLAGS_local_storage_annotation_result != "");
  std::unique_ptr<ProtoWriter> writer;
  if (enable_local_storage_annotation_result) {
    writer.reset(new ProtoWriter(FLAGS_local_storage_annotation_result));
    CHECK(writer->Open()) << "Failed to write to "
                          << FLAGS_local_storage_annotation_result;
  }

  while (stream_->Read(&resp)) {
    // Start playing video when first response is received.
    
    total_responses_received++;
    ProtoProcessor::Process(feature_, resp.annotation_results());

    
    if (resp.has_error()) {
      LOG(ERROR) << "Received an error: " << resp.error().message();
    } else if (enable_local_storage_annotation_result) {
      writer->WriteProto(resp.annotation_results());
    }
  }
  LOG(INFO) << "Received " << total_responses_received << " responses.";
  
  if (enable_local_storage_annotation_result) {
    writer->Close();
  }
}

bool StreamingClient::SendConfig() {
  std::ifstream input(FLAGS_config);
  std::stringstream config_req_json;
  while (input >> config_req_json.rdbuf()) {
  }

  // All the config details must be sent in the first request.
  StreamingAnnotateVideoRequest config_req;
  JsonStringToMessage(config_req_json.str(), &config_req);
  feature_ = config_req.video_config().feature();
  if (!stream_->Write(config_req)) {
    LOG(ERROR) << "Failed to send config: " << config_req.ShortDebugString();
    return false;
  }
  return true;
}

bool StreamingClient::SendContent() {
  bool status = true;

  std::unique_ptr<IOReader> reader;
  if (FLAGS_use_pipe) {
    reader.reset(new PipeReader(FLAGS_video_path));
  } else {
    reader.reset(new FileReader(FLAGS_video_path));
  }
  CHECK(reader->Open()) << "Failed to read from " << FLAGS_video_path;

  std::unique_ptr<IOWriter> writer;
  bool enable_local_storage_video = (FLAGS_local_storage_video != "");
  if (enable_local_storage_video) {
    writer.reset(new FileWriter(FLAGS_local_storage_video));
    CHECK(writer->Open()) << "Failed to write to " << FLAGS_local_storage_video;
  }

  int requests_sent = 0;
  long total_bytes_read = 0;
  std::vector<char> buffer(kDataChunk + 1, 0);

  while (true) {
    size_t num_bytes_read = reader->ReadBytes(kDataChunk, buffer.data());
    if (num_bytes_read == 0) {
      break;
    }
    std::string data(buffer.data(), num_bytes_read);
   
    StreamingAnnotateVideoRequest req;
    req.set_input_content(data);
    if (!stream_->Write(req)) {
      LOG(ERROR) << "Failed to send content: " << req.ShortDebugString();
      status = false;
      break;
    }
    if (enable_local_storage_video) {
      writer->WriteBytes(num_bytes_read, buffer.data());
    }
    total_bytes_read += num_bytes_read;
    requests_sent++;
  }

  if (!stream_->WritesDone()) {
    LOG(ERROR) << "Failed to mark WritesDone in gRPC stream.";
    status = false;
  }

  reader->Close();
  if (enable_local_storage_video) {
    writer->Close();
  }

  LOG(INFO) << "Sent " << requests_sent << " requests consisting of "
            << total_bytes_read << " bytes of video data in total.";
  return status;
}

}  // namespace video
}  // namespace api
