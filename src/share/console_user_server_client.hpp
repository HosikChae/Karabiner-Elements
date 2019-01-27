#pragma once

#include "constants.hpp"
#include "logger.hpp"
#include "types.hpp"
#include <nod/nod.hpp>
#include <pqrs/dispatcher.hpp>
#include <pqrs/local_datagram.hpp>
#include <pqrs/osx/input_source_selector.hpp>
#include <pqrs/osx/input_source_selector/extra/nlohmann_json.hpp>
#include <pqrs/osx/session.hpp>
#include <sstream>
#include <unistd.h>
#include <vector>

namespace krbn {
class console_user_server_client final : public pqrs::dispatcher::extra::dispatcher_client {
public:
  // Signals (invoked from the shared dispatcher thread)

  nod::signal<void(void)> connected;
  nod::signal<void(const asio::error_code&)> connect_failed;
  nod::signal<void(void)> closed;

  // Methods

  console_user_server_client(const console_user_server_client&) = delete;

  console_user_server_client(void) : dispatcher_client() {
    session_monitor_ = std::make_unique<pqrs::osx::session::monitor>(weak_dispatcher_);

    session_monitor_->console_user_id_changed.connect([this](std::optional<uid_t> uid) {
      if (uid) {
        client_ = nullptr;

        auto socket_file_path = make_console_user_server_socket_file_path(*uid);
        client_ = std::make_unique<pqrs::local_datagram::client>(weak_dispatcher_,
                                                                 socket_file_path);
        client_->set_server_check_interval(std::chrono::milliseconds(3000));
        client_->set_reconnect_interval(std::chrono::milliseconds(1000));

        client_->connected.connect([this, uid] {
          logger::get_logger()->info("console_user_server_client is connected. (uid:{0})", *uid);

          enqueue_to_dispatcher([this] {
            connected();
          });
        });

        client_->connect_failed.connect([this](auto&& error_code) {
          enqueue_to_dispatcher([this, error_code] {
            connect_failed(error_code);
          });
        });

        client_->closed.connect([this, uid] {
          logger::get_logger()->info("console_user_server_client is closed. (uid:{0})", *uid);

          enqueue_to_dispatcher([this] {
            closed();
          });
        });

        client_->async_start();
      }
    });
  }

  virtual ~console_user_server_client(void) {
    detach_from_dispatcher([this] {
      session_monitor_ = nullptr;

      client_ = nullptr;
    });
  }

  void async_start(void) {
    enqueue_to_dispatcher([this] {
      session_monitor_->async_start(std::chrono::milliseconds(1000));
    });
  }

  void async_stop(void) {
    enqueue_to_dispatcher([this] {
      session_monitor_->async_stop();

      client_ = nullptr;
    });
  }

  void async_shell_command_execution(const std::string& shell_command) const {
    enqueue_to_dispatcher([this, shell_command] {
      nlohmann::json json{
          {"operation_type", operation_type::shell_command_execution},
          {"shell_command", shell_command},
      };

      if (client_) {
        client_->async_send(nlohmann::json::to_msgpack(json));
      }
    });
  }

  void async_select_input_source(std::shared_ptr<std::vector<pqrs::osx::input_source_selector::specifier>> input_source_specifiers) {
    enqueue_to_dispatcher([this, input_source_specifiers] {
      if (input_source_specifiers) {
        nlohmann::json json{
            {"operation_type", operation_type::select_input_source},
            {"input_source_specifiers", *input_source_specifiers},
        };

        if (client_) {
          client_->async_send(nlohmann::json::to_msgpack(json));
        }
      }
    });
  }

  static std::string make_console_user_server_socket_directory(uid_t uid) {
    std::stringstream ss;
    ss << constants::get_console_user_server_socket_directory() << "/" << uid;
    return ss.str();
  }

  static std::string make_console_user_server_socket_file_path(uid_t uid) {
    return make_console_user_server_socket_directory(uid) + "/receiver";
  }

private:
  void async_send(const uint8_t* _Nonnull p, size_t length) const {
    if (client_) {
      client_->async_send(p, length);
    }
  }

  std::unique_ptr<pqrs::osx::session::monitor> session_monitor_;
  std::unique_ptr<pqrs::local_datagram::client> client_;
};
} // namespace krbn
