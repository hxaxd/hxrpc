#include "message_pool.h"

#include <cassert>

#include "../benchmark/user.pb.h"

int main() {
  // 测试目的: 验证 MessagePool 的对象复用与重置语义
  hxrpc::MessagePool pool(2);

  // 第一次获取对象并写入字段, 随后释放回池中
  auto first = pool.Acquire(Kuser::LoginRequest{});
  auto* first_ptr = first.get();
  auto* first_request = dynamic_cast<Kuser::LoginRequest*>(first.get());
  assert(first_request != nullptr);
  first_request->set_name("alice");
  first_request->set_pwd("123456");
  first = {};

  // 第二次获取同类型对象: 应复用同一实例地址, 且字段已被清空
  auto second = pool.Acquire(Kuser::LoginRequest{});
  auto* second_ptr = second.get();
  auto* second_request = dynamic_cast<Kuser::LoginRequest*>(second.get());
  assert(second_request != nullptr);
  // 关键断言: 地址一致意味着确实发生池化复用
  assert(second_ptr == first_ptr);
  // 关键断言: 内容被 reset, 避免旧请求数据串扰
  assert(second_request->name().empty());
  assert(second_request->pwd().empty());

  return 0;
}
