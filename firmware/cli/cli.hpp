// Line-oriented USB CDC command interface (see docs/USB_CLI.md).
//
// Protocol: one command per line (CR, LF or CRLF). Every command ends with a
// final line that is exactly "OK" or starts with "ERR ", so scripts can wait
// for completion. Values after `set <key>` run to the end of the line; wrap
// in double quotes to keep leading/trailing spaces. Escapes: \n \\ \" \t.
#pragma once

#include <cstddef>
#include <cstdint>

#include "app.hpp"
#include "settings.hpp"

namespace badge {

class CliHost {
 public:
  virtual ~CliHost() = default;
  virtual void write(const char *s) = 0;
  virtual Settings &staged() = 0;               // edited by set/revert/defaults
  virtual const Settings &committed() const = 0;
  virtual void staged_changed() = 0;            // re-evaluate + redraw with staged content
  virtual bool commit(char *err, size_t errlen) = 0;
  virtual void revert() = 0;
  virtual bool select_screen(Screen s, int project) = 0;
  virtual bool project_step(int delta) = 0;
  virtual void refresh(bool clean) = 0;
  virtual void print_version() = 0;
  virtual void print_status() = 0;
  virtual bool print_diag(const char *topic) = 0;  // false = unknown topic
  virtual bool self_test() = 0;
  virtual void request_sleep() = 0;
  virtual void request_reboot(bool bootsel) = 0;
};

class Cli {
 public:
  static constexpr size_t kMaxLine = 480;
  explicit Cli(CliHost &host) : host_(host) {}
  // Feed received bytes; complete lines are executed immediately.
  void feed(const char *data, size_t len);
  void execute(char *line);  // exposed for tests; modifies `line`
  void set_echo(bool on) { echo_ = on; }
  uint32_t commands() const { return commands_; }
  uint32_t errors() const { return errors_; }

 private:
  void ok();
  void err(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  void cmd_help();
  void cmd_fields();
  void cmd_get(char *args);
  void cmd_set(char *args);
  void cmd_export();
  CliHost &host_;
  char line_[kMaxLine + 1];
  size_t len_ = 0;
  bool overflow_ = false;
  bool echo_ = true;
  bool last_cr_ = false;
  uint32_t commands_ = 0, errors_ = 0;
};

// Decode the value syntax (quotes/escapes) in place. Returns false on a bad escape.
bool cli_unescape(char *s);
// Escape `in` for `export` output (reversible by cli_unescape).
void cli_escape(const char *in, char *out, size_t outlen);

}  // namespace badge
