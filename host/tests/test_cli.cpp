#include <initializer_list>
#include <string>

#include "check.hpp"
#include "cli.hpp"

using namespace badge;

namespace {
class FakeHost : public CliHost {
 public:
  FakeHost() { settings_defaults(&staged_); settings_defaults(&committed_); }
  void write(const char *s) override { out += s; }
  Settings &staged() override { return staged_; }
  const Settings &committed() const override { return committed_; }
  void staged_changed() override { ++changes; }
  bool commit(char *, size_t) override { committed_ = staged_; ++commits; return true; }
  bool safe_mode() const override { return safe; }
  bool safe = false;
  void revert() override { staged_ = committed_; }
  bool select_screen(Screen s, int p) override { screen = s; project = p; return s != Screen::QrFull; }
  bool project_step(int d) override { step += d; return true; }
  void refresh(bool clean) override { refreshed = clean ? 2 : 1; }
  void print_version() override { write("v\r\n"); }
  void print_status() override { write("s\r\n"); }
  bool print_diag(const char *t) override { return std::strcmp(t, "bogus") != 0; }
  bool self_test() override { return true; }
  bool set_gesture(bool on) override { gesture = on ? 1 : 0; return false; }
  void request_sleep() override { slept = true; }
  void request_reboot(bool b) override { rebooted = b ? 2 : 1; }
  bool crash_test(CrashTest k) override { crashed = int(k); return k != CrashTest::HangCore1; }
  int crashed = -1;

  std::string out;
  Settings staged_, committed_;
  int gesture = -1;
  int changes = 0, commits = 0, step = 0, refreshed = 0, rebooted = 0;
  bool slept = false;
  Screen screen = Screen::Badge;
  int project = -2;
};

std::string run(Cli &cli, FakeHost &h, const std::string &line) {
  h.out.clear();
  cli.feed(line.data(), line.size());
  return h.out;
}
bool ends_ok(const std::string &s) { return s.size() >= 4 && s.compare(s.size() - 4, 4, "OK\r\n") == 0; }
bool has_err(const std::string &s) { return s.find("ERR ") != std::string::npos; }
}  // namespace

TEST(cli_set_get_commit) {
  FakeHost h;
  Cli cli(h);
  cli.set_echo(false);
  CHECK(ends_ok(run(cli, h, "set name Ada Lovelace\r\n")));
  CHECK_STR(h.staged_.profile.name, "Ada Lovelace");
  CHECK_EQ(h.changes, 1);
  CHECK(run(cli, h, "get name\n").find("\"Ada Lovelace\"") != std::string::npos);
  CHECK(ends_ok(run(cli, h, "commit\n")));
  CHECK_STR(h.committed_.profile.name, "Ada Lovelace");
  run(cli, h, "set name Temp\n");
  run(cli, h, "revert\n");
  CHECK_STR(h.staged_.profile.name, "Ada Lovelace");
}

TEST(cli_rejects_invalid_values) {
  FakeHost h;
  Cli cli(h);
  cli.set_echo(false);
  CHECK(has_err(run(cli, h, "set nosuch x\n")));
  CHECK(has_err(run(cli, h, "set refresh.speed 7\n")));
  CHECK(has_err(run(cli, h, "set refresh.speed\n")));
  CHECK(has_err(run(cli, h, "set name " + std::string(60, 'x') + "\n")));
  CHECK(has_err(run(cli, h, "set qr.payload http://insecure.example\n")));
  CHECK(has_err(run(cli, h, "set name \"unterminated\n")));
  CHECK(has_err(run(cli, h, "set name bad\\q\n")));
  CHECK(has_err(run(cli, h, std::string("set name ctl\x01x\n"))));
  CHECK(has_err(run(cli, h, "frobnicate\n")));
  CHECK_EQ(h.changes, 0);  // nothing staged by failed commands
  CHECK(ends_ok(run(cli, h, "set qr.payload https://example.com/dan\n")));
}

TEST(cli_quotes_and_escapes) {
  FakeHost h;
  Cli cli(h);
  cli.set_echo(false);
  run(cli, h, "set project1.body \"  padded line one\\nline \\\"two\\\"\"\n");
  CHECK_STR(h.staged_.profile.projects[0].body, "  padded line one\nline \"two\"");
  // export -> replay reproduces identical settings
  run(cli, h, "set contact1.value M\xC3\xBCller@example.com\n");
  const std::string exported = run(cli, h, "export\n");
  FakeHost h2;
  Cli cli2(h2);
  cli2.set_echo(false);
  h2.out.clear();
  cli2.feed(exported.data(), exported.size() - 4);  // drop the trailing OK line
  CHECK(std::memcmp(&h.staged_, &h2.staged_, sizeof(Settings)) == 0);
}

TEST(cli_line_handling) {
  FakeHost h;
  Cli cli(h);
  std::string s = run(cli, h, "helx\x7Fp\r\n");  // backspace, echo on
  CHECK(s.find("Commands:") != std::string::npos);
  cli.set_echo(false);
  CHECK(has_err(run(cli, h, std::string(Cli::kMaxLine + 20, 'a') + "\n")));
  CHECK(ends_ok(run(cli, h, "status\n")));  // recovers after overflow
  CHECK(run(cli, h, "\r\n\n").empty());      // blank lines produce no output
  CHECK(run(cli, h, "junk\x03").find("^C") == std::string::npos);  // no echo when off
  CHECK(ends_ok(run(cli, h, "version\n")));
}

TEST(cli_screen_and_control_commands) {
  FakeHost h;
  Cli cli(h);
  cli.set_echo(false);
  CHECK(ends_ok(run(cli, h, "screen card\n")));
  CHECK(h.screen == Screen::Card);
  CHECK(ends_ok(run(cli, h, "screen projects 2\n")));
  CHECK_EQ(h.project, 1);
  CHECK(has_err(run(cli, h, "screen projects 0\n")));
  CHECK(has_err(run(cli, h, "screen recovery\n")));
  CHECK(has_err(run(cli, h, "screen qr\n")));  // host refused
  CHECK(ends_ok(run(cli, h, "project next\n")));
  CHECK_EQ(h.step, 1);
  CHECK(ends_ok(run(cli, h, "refresh clean\n")));
  CHECK_EQ(h.refreshed, 2);
  CHECK(has_err(run(cli, h, "diag bogus\n")));
  CHECK(ends_ok(run(cli, h, "reboot bootsel\n")));
  CHECK_EQ(h.rebooted, 2);
  CHECK(run(cli, h, "gesture on\n").find("not available") != std::string::npos);
  CHECK_EQ(h.gesture, 1);
  CHECK(has_err(run(cli, h, "gesture maybe\n")));
  CHECK(ends_ok(run(cli, h, "gesture off\n")));
  CHECK_EQ(h.gesture, 0);
  CHECK(ends_ok(run(cli, h, "sleep\n")));
  CHECK(h.slept);
}

// Raw control bytes must never reach the parser: a NUL used to cut the line
// short, so "set name Ada<NUL>X" silently stored "Ada".
TEST(cli_rejects_control_bytes_without_side_effects) {
  FakeHost h;
  Cli cli(h);
  cli.set_echo(false);
  const std::string nul = std::string("set name Ada") + '\0' + "Lovelace\n";
  std::string out = run(cli, h, nul);
  CHECK(has_err(out));
  CHECK(out.find("0x00") != std::string::npos);
  CHECK_EQ(h.changes, 0);
  CHECK(has_err(run(cli, h, "set name \x1b[AAda\n")));  // arrow-key escape sequence
  CHECK_EQ(h.changes, 0);
  CHECK(ends_ok(run(cli, h, "set name\tAda\n")));  // tab still separates tokens
  CHECK_STR(h.staged_.profile.name, "Ada");
  CHECK(ends_ok(run(cli, h, "status\n")));  // next line is unaffected
}

TEST(cli_stale_partial_line_is_dropped_after_idle) {
  FakeHost h;
  Cli cli(h);
  cli.set_echo(false);
  h.out.clear();
  cli.feed("set name Garb", 13, 1000);  // an earlier session died mid-line
  cli.feed("status\n", 7, 1000 + Cli::kIdleDiscardMs + 1);
  CHECK(ends_ok(h.out));
  CHECK(!has_err(h.out));
  CHECK_EQ(cli.stale_discards(), 1u);
  CHECK_EQ(h.changes, 0);
  // A slow typist within the window keeps the line.
  h.out.clear();
  cli.feed("set name Ad", 11, 50000);
  cli.feed("a\n", 2, 50000 + Cli::kIdleDiscardMs - 1);
  CHECK(ends_ok(h.out));
  CHECK_STR(h.staged_.profile.name, "Ada");
  // Timer wraparound does not discard a fresh line.
  h.out.clear();
  cli.feed("stat", 4, 0xFFFFFF00u);
  cli.feed("us\n", 3, 0x00000010u);
  CHECK(ends_ok(h.out));
  CHECK_EQ(cli.stale_discards(), 1u);
}

// Deliberate crashes need the exact kind plus "confirm"; anything else
// (typos, missing or extra words) must not crash the badge.
TEST(cli_crashtest_requires_exact_confirmation) {
  FakeHost h;
  Cli cli(h);
  cli.set_echo(false);
  for (const char *bad : {"crashtest\n", "crashtest panic\n", "crashtest panic yes\n", "crashtest boom confirm\n",
                          "crashtest panic confirm now\n", "crashtest Panic confirm\n"}) {
    CHECK(has_err(run(cli, h, bad)));
    CHECK_EQ(h.crashed, -1);
  }
  run(cli, h, "crashtest fault1 confirm\n");
  CHECK_EQ(h.crashed, int(CrashTest::FaultCore1));
  CHECK(has_err(run(cli, h, "crashtest hang1 confirm\n")));  // host says: not available
}

TEST(cli_portfolio_keys_and_project_qr) {
  FakeHost h;
  Cli cli(h);
  cli.set_echo(false);
  CHECK(ends_ok(run(cli, h, "screen project-qr 3\n")));
  CHECK(h.screen == Screen::ProjectQr);
  CHECK_EQ(h.project, 2);
  CHECK(has_err(run(cli, h, "screen project-qr 0\n")));
  CHECK(ends_ok(run(cli, h, "diag refresh\n")));
  // Contact types are explicit names; project links must be https URLs.
  CHECK(has_err(run(cli, h, "set contact1.type pigeon\n")));
  CHECK(has_err(run(cli, h, "set contact1.type GitHub\n")));
  CHECK(has_err(run(cli, h, "set project1.link http://example.com\n")));
  CHECK(has_err(run(cli, h, "set project1.link \"https://a b\"\n")));
  CHECK(has_err(run(cli, h, "set project13.title x\n")));  // bounded list
  CHECK_EQ(h.changes, 0);
  CHECK(ends_ok(run(cli, h, "set contact1.type github\n")));
  CHECK(ends_ok(run(cli, h, "set contact1.type \"\"\n")));  // untyped is valid
  CHECK(ends_ok(run(cli, h, "set project12.link https://example.com/p12\n")));
  CHECK(ends_ok(run(cli, h, "set project12.banner \"TOP SECRET - COMING SOON\"\n")));
  // export carries every new key so backup/restore round-trips.
  const std::string ex = run(cli, h, "export\n");
  for (const char *k : {"set contact6.type", "set project1.status", "set project12.banner", "set project12.link"})
    CHECK(ex.find(k) != std::string::npos);
}

// Safe mode stages defaults instead of the stored record: a bare commit there
// would silently replace the stored profile, so only an explicit `defaults`
// lets it through.
TEST(cli_safe_mode_commit_requires_explicit_defaults) {
  FakeHost h;
  Cli cli(h);
  cli.set_echo(false);
  h.safe = true;
  CHECK(ends_ok(run(cli, h, "set diag.single_core true\n")));
  CHECK(has_err(run(cli, h, "commit\n")));
  CHECK_EQ(h.commits, 0);
  CHECK(ends_ok(run(cli, h, "defaults\n")));
  CHECK(ends_ok(run(cli, h, "revert\n")));  // revert withdraws the explicit choice
  CHECK(has_err(run(cli, h, "commit\n")));
  CHECK_EQ(h.commits, 0);
  CHECK(ends_ok(run(cli, h, "defaults\n")));
  CHECK(ends_ok(run(cli, h, "set diag.single_core true\n")));
  CHECK(ends_ok(run(cli, h, "commit\n")));
  CHECK_EQ(h.commits, 1);
  CHECK(has_err(run(cli, h, "commit\n")));  // one explicit defaults, one commit
  CHECK_EQ(h.commits, 1);
  h.safe = false;  // normal mode: commits are unrestricted
  CHECK(ends_ok(run(cli, h, "commit\n")));
  CHECK_EQ(h.commits, 2);
}
