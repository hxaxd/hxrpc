#include "message_pool.h"
#include "../benchmark/user.pb.h"
#include <cassert>

int main() {
  hxrpc::MessagePool pool(2);

  auto first = pool.Acquire(Kuser::LoginRequest{});
  auto *first_ptr = first.get();
  auto *first_request = dynamic_cast<Kuser::LoginRequest *>(first.get());
  assert(first_request != nullptr);
  first_request->set_name("alice");
  first_request->set_pwd("123456");
  first = {};

  auto second = pool.Acquire(Kuser::LoginRequest{});
  auto *second_ptr = second.get();
  auto *second_request = dynamic_cast<Kuser::LoginRequest *>(second.get());
  assert(second_request != nullptr);
  assert(second_ptr == first_ptr);
  assert(second_request->name().empty());
  assert(second_request->pwd().empty());

  return 0;
}
