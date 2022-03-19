//
// Created by wuyua on 3/12/2022.
//

#pragma once

#define FLP_VERSION "1.1.1"

#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#ifdef __EXCEPTIONS
#define FLP_THROW(ex, msg) throw ex(msg)
#else
#define FLP_THROW(ex, msg) return false
#endif
#ifndef FLP_TIMESTAMP
#define FLP_TIMESTAMP (std::chrono::duration_cast<std::chrono::milliseconds>(   \
                           std::chrono::system_clock::now().time_since_epoch()) \
                           .count())
#endif

namespace finix {
// Forward declaration
class LineProtocol;
template <typename T>
class ExchangeState;

// module functions
template <typename T>
std::function<bool(float)>& get_default_validator();

// Exceptions
class UnknownQualifierError : public std::runtime_error {
 public:
  explicit UnknownQualifierError(const std::string& arg) : runtime_error(arg) {}
};
class InvalidArgumentError : public std::runtime_error {
 public:
  explicit InvalidArgumentError(const std::string& arg) : runtime_error(arg) {}
};
class ValidatorError : public std::runtime_error {
 public:
  explicit ValidatorError(const std::string& arg) : runtime_error(arg) {}
};

// Record classes
struct ArgumentSpec {
  bool optional{true};
  bool is_float{false};
  std::function<void(float)> setter;
  std::function<bool(float)> validator{};
  explicit ArgumentSpec(int& assign_to, bool optional = true, const std::function<bool(float)>& validator = nullptr)
      : optional(optional),
        is_float(false),
        setter([&](float v) { assign_to = (int)v; }),
        validator(std::move(validator)) {}
  explicit ArgumentSpec(float& assign_to, bool optional = true, const std::function<bool(float)>& validator = nullptr)
      : optional(optional),
        is_float(true),
        setter([&](float v) { assign_to = v; }),
        validator(std::move(validator)) {}

  template <typename T>
  explicit ArgumentSpec(ExchangeState<T>& assign_to, bool optional = true, const std::function<bool(float)>& validator = nullptr);
};

using ArgumentMap = std::unordered_map<std::string, ArgumentSpec>;
using RawArgumentMap = std::unordered_map<std::string, float>;
/// Passing the predefined and undefined arguments.
using CommandCallback = std::function<void(const RawArgumentMap&, const RawArgumentMap&)>;
struct CommandSpec {
  ArgumentMap arg_map;
  CommandCallback callback;
  CommandSpec(const ArgumentMap& arg_map, const CommandCallback& callback)
      : arg_map(arg_map),
        callback(callback) {}
};
using CommandMap = std::unordered_map<std::string, CommandSpec>;

struct ExchangeStateInterface {
  ExchangeStateInterface(std::function<float(void)> getter, std::function<void(float)> setter, bool is_float) : getter(std::move(getter)),
                                                                                                                setter(std::move(setter)),
                                                                                                                is_float(is_float) {}
  std::function<float(void)> getter;
  std::function<void(float)> setter;
  bool is_float;
};

using ExchangeStateMap = std::unordered_map<std::string, ExchangeStateInterface>;

// Classes

/// Represent the states that is managed by FLP. It can be used as the argument and the setter will be automatically adapted.
/// When the setter is invoked, the state will be reported to the ostream.
/// \tparam T
template <typename T>
class ExchangeState {
  LineProtocol& flp_;

 public:
  virtual ~ExchangeState();

 private:
  std::string name_;
  T state_;

  std::function<float()> getter_ = [&]() { return (float)Get(); };
  std::function<void(float)> setter_ = [&](float v) { Set((T)v); };

 public:
  bool report_state{true};
  // affect the output precision when T is float
  int n_decimal{-1};

  explicit ExchangeState(LineProtocol& flp, const std::string& name);
  const T& Get() const { return state_; }
  [[nodiscard]] const std::function<float()>& Getter() const {
    return getter_;
  }
  [[nodiscard]] std::function<void(float)>& Setter() {
    return setter_;
  }
  [[nodiscard]] const std::string& GetName() const { return name_; }
  void Set(const T& other) {
    state_ = other;
    if (report_state) {
      ReportState();
    }
  }
  void ReportState();
  ExchangeState<T>& operator=(const T& other) {
    Set(other);
    return *this;
  }
};

class LineProtocol {
 private:
  char delim;
  std::string buf_{};
  CommandMap command_map_{};
  ExchangeStateMap exchange_state_map_{};

  std::reference_wrapper<std::ostream> ostream_;

 public:
  explicit LineProtocol(int buf_reserve = 150, char delim = '\n', std::ostream& ostream = std::cout) : delim(delim),
                                                                                                       ostream_(ostream) {
    buf_.reserve(buf_reserve);
  };

 public:
  void Feed(const char* buffer, size_t len) {
    buf_.append(buffer, len);
  }
  void Feed(const std::string& str) {
    buf_.append(str);
  }
  const std::string& GetBuffer() const {
    return buf_;
  }
  void SetOStream(std::ostream& ostream) {
    ostream_ = ostream;
  }

 public:
  bool ValidateApply(const std::string& cmd_line) {
    size_t last = cmd_line.find_first_not_of(' ');
    size_t next = 0;
    std::vector<std::string> tokens;
    while ((next = cmd_line.find(' ', last)) != std::string::npos) {
      tokens.push_back(cmd_line.substr(last, next - last));
      last = cmd_line.find_first_not_of(' ', next);
    }
    // if no more non-space, no need for the extra token (tailing space)
    if (last != std::string::npos) {
      auto last_token = cmd_line.substr(last);
      if (!last_token.empty()) {
        tokens.push_back(last_token);
      }
    }
    auto qualifier_str = tokens[0];
    // check if the qualifier is valid
    auto found = command_map_.find(qualifier_str);
    if (found == command_map_.end()) {
      FLP_THROW(UnknownQualifierError, "Unknown qualifier");
    }

    // store the unmapped arguments
    std::unordered_map<std::string, float> predefined_arg_map, undefined_arg_map;
    auto command = found->second;
    auto& arg_map = command.arg_map;
    // check if the arguments are valid
    for (int i = 1; i < tokens.size(); ++i) {
      auto token = tokens[i];
      // find '=' in the token
      auto eq_pos = token.find('=');
      if (eq_pos == std::string::npos) {
        FLP_THROW(InvalidArgumentError, "Invalid argument: " + token);
      }
      auto arg_name = token.substr(0, eq_pos);
      auto arg_value = token.substr(eq_pos + 1);

      if (arg_value.empty()) {
        // test arg=\n
        FLP_THROW(InvalidArgumentError, token + " incomplete pair");
      }

      char* p;
      auto int_val = strtol(arg_value.c_str(), &p, 10);
      // if p points to the end of the string, conversion is successful
      bool val_is_int = *p == 0;

      auto float_val = strtof(arg_value.c_str(), &p);
      // if p points to the end of the string, conversion is successful
      bool val_is_float = *p == 0;

      if (!val_is_float) {
        // float val is used throughout the next steps. If it is not valid, use the one from int.
        float_val = (float)int_val;
      }

      if (!(val_is_int || val_is_float)) {
        FLP_THROW(InvalidArgumentError, token + " value is not numeric");
      }

      // check if the arg_name exists
      auto found_arg = arg_map.find(arg_name);
      if (found_arg == arg_map.end()) {
        // non existing arg
        undefined_arg_map[arg_name] = float_val;
      } else {
        // existing in the spec

        // should be int but a float is given, error.
        if (!found_arg->second.is_float && !val_is_int) {
          FLP_THROW(InvalidArgumentError, token + "  should be int");
        }

        auto validator = found_arg->second.validator;

        if (!validator) {
          // if validator is not set, just set the value
          predefined_arg_map[arg_name] = float_val;
        } else {
          // validator was set, it should return true if valid.
          if (!validator(float_val)) {
            FLP_THROW(ValidatorError, token + " validation failed");
          } else {
            predefined_arg_map[arg_name] = float_val;
          }
        }
      }
    }

    // check if all required arguments are supplied
    for (auto& item : arg_map) {
      if (!item.second.optional) {
        if (predefined_arg_map.find(item.first) == predefined_arg_map.end()) {
          FLP_THROW(InvalidArgumentError, item.first + " is required");
        }
      }
    }

    // All check has passed, now apply the arguments.
    // TODO: cache the previous arguments for transaction rollback
    for (auto& item : predefined_arg_map) {
      arg_map.at(item.first).setter(item.second);
    }

    // Invoke the callback, which can be nullptr.
    if (command.callback) {
      command.callback(predefined_arg_map, undefined_arg_map);
    }
    return true;
  }

 public:
  /// Process will only process one valid command once.
  /// However, if there are consecutive CR, they will be purged.
  /// \return
  bool Process() {
    while (true) {
      // check if there is a delim in the buffer
      auto found = buf_.find(delim);
      if (found == std::string::npos) {
        // no delim
        return false;
      }

      auto cmd_str = buf_.substr(0, found);
      buf_.erase(0, found + 1);

      if (cmd_str.empty() || cmd_str.find_first_not_of(' ') == std::string::npos) {
        // ignore multiple \n\n\n or \n[space]\n
        continue;
      }
      return ValidateApply(cmd_str);
    }
  }

 public:
  bool RegisterCommand(const std::string& full_qualifier, const ArgumentMap& arg_map, const CommandCallback& callback) {
    if (command_map_.find(full_qualifier) == command_map_.end()) {
      command_map_.try_emplace(full_qualifier, arg_map, callback);
      return true;
    } else {
      FLP_THROW(InvalidArgumentError, full_qualifier + " is already registered");
    }
  }

  template <typename T>
  bool RegisterExchangeState(ExchangeState<T>& es);

  void UnregisterExchangeState(const std::string& name) {
    exchange_state_map_.erase(name);
  }

  void RegisterInternalCommands() {
    RegisterCommand("@flp.version",
                    {},
                    [&](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
                      Respond("@flp.version", FLP_VERSION, '_');
                    });
    RegisterCommand("@flp.buffer.size",
                    {},
                    [&](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
                      Respond("@flp.buffer.size", std::to_string(buf_.size()), '_');
                    });
    RegisterCommand("@flp.cmd_reg",
                    {},
                    [&](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
                      std::string reg = "{";
                      for (auto it = command_map_.begin(); it != command_map_.end(); ++it) {
                        reg += "\"" + it->first + "\": {";
                        auto& arg_map = it->second.arg_map;
                        for (auto arg_it = arg_map.begin(); arg_it != arg_map.end(); ++arg_it) {
                          reg += "\"" + arg_it->first + "\":\""
                              + (arg_it->second.optional ? "optional," : "required,")
                              + (arg_it->second.is_float ? "float" : "int") + "\"";
                          if (std::next(arg_it) != arg_map.end()) {
                            reg += ",";
                          }
                        }
                        reg += (std::next(it) == command_map_.end() ? "}" : "},");
                      }
                      reg += "}";
                      Respond("@flp.cmd_reg", reg, '_');
                    });

    RegisterCommand("@flp.state",
                    {},
                    [&](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
                      std::string reg = "{";
                      for (auto it = exchange_state_map_.begin(); it != exchange_state_map_.end(); ++it) {
                        float getter = it->second.getter();
                        reg += "\"" + it->first + "\":"
                            + (it->second.is_float ? std::to_string(getter) : std::to_string((int)getter));
                        if (std::next(it) != exchange_state_map_.end()) {
                          reg += ",";
                        }
                      }
                      reg += "}";
                      Respond("@flp.state", reg, '_');
                    });
  }

 public:
  void Respond(const std::string& channel, const std::string& message, char label = 'R') {
    ostream_.get() << label << "(" << FLP_TIMESTAMP << ") "
                   << channel << ": " << message << "\n";
  }
};

// Method implementations
template <typename T>
ExchangeState<T>::ExchangeState(LineProtocol& flp, const std::string& name)
    : flp_(flp), name_(name) {
  flp.RegisterExchangeState(*this);
}

template <typename T>
void ExchangeState<T>::ReportState() {
  std::stringstream ss;
  if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>) {
    ss << (int)state_;
  } else if constexpr (std::is_floating_point_v<T>) {
    if (n_decimal >= 0) {
      ss << std::fixed << std::setprecision(n_decimal);
    }
    ss << state_;
  } else {
    ss << state_;
  }
  flp_.Respond(name_, ss.str(), 'R');
}
template <typename T>
ExchangeState<T>::~ExchangeState() {
  flp_.UnregisterExchangeState(name_);
}

template <typename T>
bool LineProtocol::RegisterExchangeState(ExchangeState<T>& es) {
  auto& name = es.GetName();
  if (exchange_state_map_.find(name) == exchange_state_map_.end()) {
    exchange_state_map_.try_emplace(name, es.Getter(), es.Setter(), std::is_floating_point_v<T>);
    return true;
  } else {
    FLP_THROW(InvalidArgumentError, name + " is already registered");
  }
}

template <typename T>
std::function<bool(float)>& get_default_validator() {
  if constexpr (std::is_same_v<T, bool>) {
    static std::function<bool(float)> validator = [](float v) {
      return (v == 0 || v == 1);
    };
    return validator;
  } else if constexpr (std::is_integral_v<T>) {
    static std::function<bool(float)> validator = [](float v) {
      return (v >= std::numeric_limits<T>::min() && v <= std::numeric_limits<T>::max());
    };
    return validator;
  } else {
    static std::function<bool(float)> validator = [](float v) {
      return true;
    };
    return validator;
  }
}

template <typename T>
ArgumentSpec::ArgumentSpec(ExchangeState<T>& assign_to, bool optional, const std::function<bool(float)>& validator)
    : optional(optional),
      is_float(std::is_floating_point_v<T>),
      setter(assign_to.Setter()),
      validator(validator ? validator : get_default_validator<T>()) {}

}  // namespace finix