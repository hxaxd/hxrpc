#include "../user.pb.h"
#include "application.h"
#include "provider.h"
#include <iostream>
#include <string>

class UserService : public Kuser::UserServiceRpc {
public:
  bool Login(std::string name, std::string pwd) {
    return true; 
  }

  void Login(::google::protobuf::RpcController *controller,
             const ::Kuser::LoginRequest *request,
             ::Kuser::LoginResponse *response,
             ::google::protobuf::Closure *done) {
    // 提取参数
    std::string name = request->name();
    std::string pwd = request->pwd();

    bool login_result = Login(name, pwd);

    Kuser::ResultCode *code = response->mutable_result();
    code->set_errcode(0);                
    code->set_errmsg("");                
    response->set_success(login_result); 

    done->Run();
  }
};

int main(int argc, char **argv) {
  hxrpcApplication::Init(argc, argv);
  hxrpcProvider provider;
  provider.NotifyService(new UserService());
  provider.Run();
  return 0;
}