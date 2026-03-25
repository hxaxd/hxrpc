#include "rpc_dispatcher.h"
#include "controller.h"
#include <google/protobuf/stubs/common.h>

namespace {

class InlineDoneClosure : public google::protobuf::Closure {
public:
  explicit InlineDoneClosure(bool &done_called) : done_called_(done_called) {}
  void Run() override {
    done_called_ = true;
    called_ = true;
  }
  [[nodiscard]] bool Called() const { return called_; }

private:
  bool &done_called_;
  bool called_{false};
};

} // namespace

namespace hxrpc {

RpcDispatcher::RpcDispatcher(const ServiceRegistry &registry,
                             std::shared_ptr<Serializer> serializer,
                             std::shared_ptr<MessagePool> message_pool)
    : registry_(registry), serializer_(std::move(serializer)),
      message_pool_(std::move(message_pool)) {}

std::expected<std::string, RpcError>
RpcDispatcher::HandleFrame(std::string_view frame) const {
  auto request_result = RpcCodec::DecodeRequest(frame);
  if (!request_result) {
    return std::unexpected(request_result.error());
  }

  const auto &request = request_result.value();
  auto method_result = registry_.Find(request.service_name, request.method_name);
  if (!method_result) {
    return std::unexpected(method_result.error());
  }

  const auto &registered = method_result.value();
  SerializationContext context{request.service_name, request.method_name};

  auto request_message =
      message_pool_->Acquire(registered.service->GetRequestPrototype(registered.method));
  auto deserialize_result =
      serializer_->Deserialize(request.payload, *request_message, context);
  if (!deserialize_result) {
    return std::unexpected(deserialize_result.error());
  }

  auto response_message =
      message_pool_->Acquire(registered.service->GetResponsePrototype(registered.method));
  bool done_called = false;
  auto *done = new InlineDoneClosure(done_called);
  hxrpccontroller controller;
  controller.SetRequestMetadata(request.metadata);
  registered.service->CallMethod(registered.method, &controller, request_message.get(),
                                 response_message.get(), done);

  if (!done->Called() || !done_called) {
    // 这里故意泄露 closure，而不是在“业务层可能异步保存了 done”的情况下
    // 立即释放造成悬垂指针。教学版明确规定服务端方法必须同步调用 done->Run()。
    return std::unexpected(RpcError{RpcStatusCode::kInternalError,
                                    "asynchronous server callbacks are unsupported"});
  }
  delete done;

  auto serialize_result = serializer_->Serialize(*response_message, context);
  if (!serialize_result) {
    return std::unexpected(serialize_result.error());
  }

  RpcResponse response{request.request_id, RpcStatusCode::kOk, {},
                       serialize_result.value()};
  return RpcCodec::EncodeResponse(response);
}

} // namespace hxrpc
