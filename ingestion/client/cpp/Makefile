HOST_SYSTEM = $(shell uname | cut -f 1 -d_)
SYSTEM ?= $(HOST_SYSTEM)
CXX = gcc
CPPFLAGS += `pkg-config --cflags protobuf grpc libglog gflags`
CXXFLAGS += -std=c++11
LDFLAGS +=  -L/usr/local/lib `pkg-config --libs protobuf grpc++ grpc libglog gflags`\
           -Wl,--no-as-needed -lgrpc++_reflection -Wl,--as-needed \
           -ldl
PROTOC = protoc
GRPC_CPP_PLUGIN = grpc_cpp_plugin
GRPC_CPP_PLUGIN_PATH ?= `which $(GRPC_CPP_PLUGIN)`

PROTOS_PATH = ../../proto

vpath %.proto $(PROTOS_PATH)
SRCS=status.pb.cc status.grpc.pb.cc video_intelligence_streaming.pb.cc video_intelligence_streaming.grpc.pb.cc file_reader.cc file_writer.cc pipe_reader.cc proto_writer.cc proto_writer.cc proto_reader.cc proto_processor.cc streaming_client.cc streaming_client_main.cc

OBJS=$(subst .cc,.o,$(SRCS))

all: aistreamer

aistreamer: $(OBJS)
	$(CXX) $^ $(LDFLAGS) -lstdc++ -o $@

file_reader.o: file_reader.cc file_reader.h io_reader.h
	gcc $(CPPFLAGS) -c file_reader.cc

file_writer.o: file_writer.cc file_writer.h io_writer.h
	gcc $(CPPFLAGS) -c file_writer.cc

pipe_reader.o: pipe_reader.cc pipe_reader.h
	gcc $(CPPFLAGS) -c pipe_reader.cc

proto_reader.o: proto_reader.cc proto_reader.h
	gcc $(CPPFLAGS) -c proto_reader.cc

proto_writer.o: proto_writer.cc proto_writer.h
	gcc $(CPPFLAGS) -c proto_writer.cc

proto_processor.o: proto_processor.cc proto_processor.h
	gcc $(CPPFLAGS) -c proto_processor.cc

streaming_client.o: streaming_client.cc streaming_client.h
	gcc $(CPPFLAGS) -c streaming_client.cc
	
streaming_client_main.o: streaming_client_main.cc streaming_client.h
	gcc $(CPPFLAGS) -c streaming_client_main.cc

.PRECIOUS: %.grpc.pb.cc
%.grpc.pb.cc: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) $<

.PRECIOUS: %.pb.cc
%.pb.cc: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --cpp_out=. $<

clean:
	rm -f *.o *.pb.cc *.pb.h aistreamer


# The following is to test your system and ensure a smoother experience.
# They are by no means necessary to actually compile a grpc-enabled software.

PROTOC_CMD = which $(PROTOC)
PROTOC_CHECK_CMD = $(PROTOC) --version | grep -q libprotoc.3
PLUGIN_CHECK_CMD = which $(GRPC_CPP_PLUGIN)
HAS_PROTOC = $(shell $(PROTOC_CMD) > /dev/null && echo true || echo false)
ifeq ($(HAS_PROTOC),true)
HAS_VALID_PROTOC = $(shell $(PROTOC_CHECK_CMD) 2> /dev/null && echo true || echo false)
endif
HAS_PLUGIN = $(shell $(PLUGIN_CHECK_CMD) > /dev/null && echo true || echo false)

SYSTEM_OK = false
ifeq ($(HAS_VALID_PROTOC),true)
ifeq ($(HAS_PLUGIN),true)
SYSTEM_OK = true
endif
endif

system-check:
ifneq ($(HAS_VALID_PROTOC),true)
	@echo " DEPENDENCY ERROR"
	@echo
	@echo "You don't have protoc 3.0.0 installed in your path."
	@echo "Please install Google protocol buffers 3.0.0 and its compiler."
	@echo "You can find it here:"
	@echo
	@echo "   https://github.com/google/protobuf/releases/tag/v3.0.0"
	@echo
	@echo "Here is what I get when trying to evaluate your version of protoc:"
	@echo
	-$(PROTOC) --version
	@echo
	@echo
endif
ifneq ($(HAS_PLUGIN),true)
	@echo " DEPENDENCY ERROR"
	@echo
	@echo "You don't have the grpc c++ protobuf plugin installed in your path."
	@echo "Please install grpc. You can find it here:"
	@echo
	@echo "   https://github.com/grpc/grpc"
	@echo
	@echo "Here is what I get when trying to detect if you have the plugin:"
	@echo
	-which $(GRPC_CPP_PLUGIN)
	@echo
	@echo
endif
ifneq ($(SYSTEM_OK),true)
	@false
endif
