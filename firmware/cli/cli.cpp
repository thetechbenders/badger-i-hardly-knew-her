#include "cli.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace badge {

namespace {
char *skip_ws(char *s) {
  while (*s == ' ' || *s == '\t') ++s;
  return s;
}
// Split the first token off `s`; returns it NUL terminated, advances `s`.
char *next_token(char **s) {
  char *p = skip_ws(*s);
  if (!*p) { *s = p; return nullptr; }
  char *start = p;
  while (*p && *p != ' ' && *p != '\t') ++p;
  if (*p) *p++ = 0;
  *s = p;
  return start;
}
bool parse_int(const char *s, int *out) {
  if (!s || !*s) return false;
  char *end;
  long v = std::strtol(s, &end, 10);
  if (*end || v < -1000 || v > 1000) return false;
  *out = int(v);
  return true;
}
}  // namespace

bool crash_test_from_name(const char *name, CrashTest *out) {
  static const struct {
    const char *name;
    CrashTest kind;
  } kNames[] = {{"hang0", CrashTest::HangCore0}, {"hang1", CrashTest::HangCore1}, {"panic", CrashTest::Panic},
                {"fault0", CrashTest::FaultCore0}, {"fault1", CrashTest::FaultCore1}};
  for (const auto &n : kNames)
    if (!std::strcmp(name, n.name)) { *out = n.kind; return true; }
  return false;
}

bool cli_unescape(char *s) {
  size_t n = std::strlen(s);
  // Trim trailing whitespace first (terminals often add it).
  while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
  char *src = s, *dst = s;
  bool quoted = false;
  if (*src == '"') {
    if (n < 2 || s[n - 1] != '"') return false;
    quoted = true;
    s[n - 1] = 0;
    ++src;
  }
  (void)quoted;
  while (*src) {
    if (*src == '\\') {
      ++src;
      switch (*src) {
        case 'n': *dst++ = '\n'; break;
        case 't': *dst++ = ' '; break;  // tabs are not printable on the badge
        case '\\': *dst++ = '\\'; break;
        case '"': *dst++ = '"'; break;
        default: return false;
      }
      ++src;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = 0;
  return true;
}

void cli_escape(const char *in, char *out, size_t outlen) {
  size_t o = 0;
  if (outlen < 3) { if (outlen) out[0] = 0; return; }
  out[o++] = '"';
  for (const char *p = in; *p && o + 3 < outlen; ++p) {
    if (*p == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
    else if (*p == '\\' || *p == '"') { out[o++] = '\\'; out[o++] = *p; }
    else out[o++] = *p;
  }
  out[o++] = '"';
  out[o] = 0;
}

void Cli::printf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  host_.write(buf);
}

void Cli::ok() { host_.write("OK\r\n"); }

void Cli::err(const char *fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  ++errors_;
  host_.write("ERR ");
  host_.write(buf);
  host_.write("\r\n");
}

void Cli::feed(const char *data, size_t len, uint32_t now_ms) {
  if (have_rx_time_ && (len_ || overflow_ || bad_char_ >= 0) && now_ms - last_rx_ms_ >= kIdleDiscardMs) {
    len_ = 0;
    overflow_ = false;
    bad_char_ = -1;
    last_cr_ = false;
    ++stale_discards_;
  }
  have_rx_time_ = true;
  last_rx_ms_ = now_ms;
  feed(data, len);
}

void Cli::feed(const char *data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = data[i];
    if (c == '\n' && last_cr_) { last_cr_ = false; continue; }  // CRLF
    last_cr_ = c == '\r';
    if (c == '\r' || c == '\n') {
      if (echo_) host_.write("\r\n");
      if (overflow_) {
        err("line too long (max %u bytes)", unsigned(kMaxLine));
      } else if (bad_char_ >= 0) {
        err("control character 0x%02x in line; line ignored", unsigned(bad_char_));
      } else {
        line_[len_] = 0;
        execute(line_);
      }
      len_ = 0;
      overflow_ = false;
      bad_char_ = -1;
      continue;
    }
    if (c == 0x08 || c == 0x7F) {  // backspace
      if (len_ && !overflow_) {
        // Remove a whole UTF-8 sequence.
        do { --len_; } while (len_ && (uint8_t(line_[len_]) & 0xC0) == 0x80);
        if (echo_) host_.write("\b \b");
      }
      continue;
    }
    if (c == 0x03) {  // Ctrl-C: discard the line
      len_ = 0;
      overflow_ = false;
      bad_char_ = -1;
      if (echo_) host_.write("^C\r\n");
      continue;
    }
    if (uint8_t(c) < 0x20 && c != '\t') {
      // NUL would silently truncate the command; ESC sequences and other
      // control bytes are never valid input. Reject the whole line at EOL.
      if (bad_char_ < 0) bad_char_ = uint8_t(c);
      continue;
    }
    if (len_ >= kMaxLine) { overflow_ = true; continue; }
    line_[len_++] = c;
    if (echo_) { const char s[2] = {c, 0}; host_.write(s); }
  }
}

void Cli::cmd_help() {
  host_.write(
      "Commands:\r\n"
      "  help | version | status | selftest\r\n"
      "  screen badge|card|projects [n]|project-qr [n]|qr|info  show a screen (n = 1-based project)\r\n"
      "  project next|prev|<n>\r\n"
      "  fields                list configurable keys with limits\r\n"
      "  get [key]             show staged value(s)\r\n"
      "  set <key> <value>     stage a validated change (shown immediately)\r\n"
      "  clear <key>           stage an empty value\r\n"
      "  commit | revert       persist to flash / drop staged changes\r\n"
      "  defaults              stage factory defaults (then commit)\r\n"
      "  export                print replayable set commands\r\n"
      "  refresh [clean]       redraw the current screen\r\n"
      "  diag [all|mem|flash|display|refresh|assets|reset|settings|qr|battery|gesture]\r\n"
      "  gesture on|off        gesture mode (APDS-9960 on Qwiic)\r\n"
      "  echo on|off           terminal echo\r\n"
      "  sleep                 power off (battery) / emulated sleep (USB)\r\n"
      "  reboot [bootsel]\r\n"
      "  crashtest hang0|hang1|panic|fault0|fault1 confirm   crash on purpose (recovery test)\r\n");
}

void Cli::cmd_fields() {
  size_t n;
  const FieldDesc *f = settings_fields(&n);
  for (size_t i = 0; i < n; ++i) {
    if (f[i].type == FieldType::Str) printf("%-22s text  max %u bytes  %s\r\n", f[i].key, unsigned(f[i].size - 1), f[i].help);
    else printf("%-22s %-5s %u..%u  %s\r\n", f[i].key, f[i].type == FieldType::Bool ? "bool" : "int",
                unsigned(f[i].min), unsigned(f[i].max), f[i].help);
  }
}

void Cli::cmd_get(char *args) {
  char *key = next_token(&args);
  char val[400], esc[820];
  size_t n;
  const FieldDesc *f = settings_fields(&n);
  if (!key) {
    for (size_t i = 0; i < n; ++i) {
      settings_get(host_.staged(), f[i], val, sizeof val);
      cli_escape(val, esc, sizeof esc);
      printf("%s = ", f[i].key);
      host_.write(esc);
      host_.write("\r\n");
    }
    ok();
    return;
  }
  const FieldDesc *fd = find_field(key);
  if (!fd) { err("unknown key '%s' (try: fields)", key); return; }
  settings_get(host_.staged(), *fd, val, sizeof val);
  cli_escape(val, esc, sizeof esc);
  host_.write(esc);
  host_.write("\r\n");
  ok();
}

void Cli::cmd_set(char *args) {
  char *key = next_token(&args);
  if (!key) { err("usage: set <key> <value>"); return; }
  const FieldDesc *fd = find_field(key);
  if (!fd) { err("unknown key '%s' (try: fields)", key); return; }
  char *value = skip_ws(args);
  if (!cli_unescape(value)) { err("bad quoting or escape (use \\n \\\\ \\\")"); return; }
  if (fd->type != FieldType::Str && !*value) { err("%s needs a value", key); return; }
  if (fd->id == 0x010 && *value && std::strncmp(value, "https://", 8) != 0 &&
      std::strncmp(value, "BEGIN:VCARD", 11) != 0) {
    err("qr.payload must start with https:// or BEGIN:VCARD");
    return;
  }
  const SetResult r = settings_set(&host_.staged(), *fd, value);
  if (r != SetResult::Ok) {
    if (r == SetResult::TooLong) err("%s: value too long (max %u bytes)", key, unsigned(fd->size - 1));
    else if (r == SetResult::OutOfRange) err("%s: out of range %u..%u", key, unsigned(fd->min), unsigned(fd->max));
    else err("%s: %s", key, set_result_str(r));
    return;
  }
  host_.staged_changed();
  host_.write("staged (not saved; run 'commit')\r\n");
  ok();
}

void Cli::cmd_export() {
  size_t n;
  const FieldDesc *f = settings_fields(&n);
  char val[400], esc[820];
  for (size_t i = 0; i < n; ++i) {
    settings_get(host_.staged(), f[i], val, sizeof val);
    cli_escape(val, esc, sizeof esc);
    printf("set %s ", f[i].key);
    host_.write(esc);
    host_.write("\r\n");
  }
  ok();
}

void Cli::execute(char *line) {
  char *args = line;
  char *cmd = next_token(&args);
  if (!cmd) return;  // empty line: no response
  ++commands_;
  if (!std::strcmp(cmd, "help") || !std::strcmp(cmd, "?")) { cmd_help(); ok(); return; }
  if (!std::strcmp(cmd, "version")) { host_.print_version(); ok(); return; }
  if (!std::strcmp(cmd, "status")) { host_.print_status(); ok(); return; }
  if (!std::strcmp(cmd, "selftest")) { host_.self_test() ? ok() : err("self-test failed"); return; }
  if (!std::strcmp(cmd, "fields")) { cmd_fields(); ok(); return; }
  if (!std::strcmp(cmd, "get") || !std::strcmp(cmd, "show")) { cmd_get(args); return; }
  if (!std::strcmp(cmd, "set")) { cmd_set(args); return; }
  if (!std::strcmp(cmd, "clear")) {
    char *key = next_token(&args);
    const FieldDesc *fd = key ? find_field(key) : nullptr;
    if (!fd) { err("usage: clear <text key>"); return; }
    if (fd->type != FieldType::Str) { err("%s is numeric; use set", key); return; }
    settings_set(&host_.staged(), *fd, "");
    host_.staged_changed();
    host_.write("staged (not saved; run 'commit')\r\n");
    ok();
    return;
  }
  if (!std::strcmp(cmd, "commit")) {
    char e[80] = "";
    if (host_.commit(e, sizeof e)) ok(); else err("commit failed: %s", e);
    return;
  }
  if (!std::strcmp(cmd, "revert")) { host_.revert(); ok(); return; }
  if (!std::strcmp(cmd, "defaults")) {
    settings_defaults(&host_.staged());
    host_.staged_changed();
    host_.write("factory defaults staged (not saved; run 'commit')\r\n");
    ok();
    return;
  }
  if (!std::strcmp(cmd, "export")) { cmd_export(); return; }
  if (!std::strcmp(cmd, "screen")) {
    char *name = next_token(&args);
    Screen s;
    if (!name || !screen_from_name(name, &s) || s == Screen::Recovery) {
      err("usage: screen badge|card|projects [n]|project-qr [n]|qr|info");
      return;
    }
    int project = -1;
    if (char *n = next_token(&args)) {
      if (!parse_int(n, &project) || project < 1) { err("project number must be >= 1"); return; }
      --project;
    }
    host_.select_screen(s, project) ? ok() : err("screen not available (not configured?)");
    return;
  }
  if (!std::strcmp(cmd, "project")) {
    char *a = next_token(&args);
    int n;
    if (a && !std::strcmp(a, "next")) { host_.project_step(1) ? ok() : err("no other project"); return; }
    if (a && !std::strcmp(a, "prev")) { host_.project_step(-1) ? ok() : err("no other project"); return; }
    if (a && parse_int(a, &n) && n >= 1) {
      host_.select_screen(Screen::Projects, n - 1) ? ok() : err("no such project");
      return;
    }
    err("usage: project next|prev|<n>");
    return;
  }
  if (!std::strcmp(cmd, "refresh")) {
    char *a = next_token(&args);
    if (a && std::strcmp(a, "clean")) { err("usage: refresh [clean]"); return; }
    host_.refresh(a != nullptr);
    ok();
    return;
  }
  if (!std::strcmp(cmd, "diag")) {
    char *t = next_token(&args);
    host_.print_diag(t ? t : "all") ? ok() : err("unknown diag topic");
    return;
  }
  if (!std::strcmp(cmd, "echo")) {
    char *a = next_token(&args);
    if (a && !std::strcmp(a, "on")) { echo_ = true; ok(); return; }
    if (a && !std::strcmp(a, "off")) { echo_ = false; ok(); return; }
    err("usage: echo on|off");
    return;
  }
  if (!std::strcmp(cmd, "gesture")) {
    char *a = next_token(&args);
    if (!a || (std::strcmp(a, "on") && std::strcmp(a, "off"))) { err("usage: gesture on|off"); return; }
    const bool on = !std::strcmp(a, "on");
    if (!host_.set_gesture(on) && on) host_.write("gesture mode on, but the sensor is not available (see diag gesture)\r\n");
    ok();
    return;
  }
  if (!std::strcmp(cmd, "sleep")) { ok(); host_.request_sleep(); return; }
  if (!std::strcmp(cmd, "reboot")) {
    char *a = next_token(&args);
    if (a && std::strcmp(a, "bootsel")) { err("usage: reboot [bootsel]"); return; }
    ok();
    host_.request_reboot(a != nullptr);
    return;
  }
  if (!std::strcmp(cmd, "crashtest")) {
    char *a = next_token(&args);
    char *c = next_token(&args);
    CrashTest k;
    if (!a || !crash_test_from_name(a, &k) || !c || std::strcmp(c, "confirm") || next_token(&args)) {
      err("usage: crashtest hang0|hang1|panic|fault0|fault1 confirm");
      return;
    }
    if (!host_.crash_test(k)) err("crashtest %s not available in this mode (single-core?)", a);
    return;  // on the device a successful crash test never returns
  }
  err("unknown command '%s' (try: help)", cmd);
}

}  // namespace badge
