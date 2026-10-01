// Host preview: renders badge screens with the firmware renderer and writes
// them as binary PBM (P4) at native 296x128 resolution.
//
//   badger_preview --out DIR [--pack FILE|--no-portrait] [--layout N]
//                  [--set key=value ...] [--screens badge,card,...]
//                  [--battery none|usb|invalid|low|0..4] [--gesture off|on|fault]
//                  [--faults]   diagnostics show fault states (host diagnostic sample)
//   badger_preview --dump-fields     (key type size min max, for tests)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "assetpack.hpp"
#include "renderer.hpp"
#include "settings.hpp"
#include "text.hpp"

namespace badge {
extern const uint8_t kBuiltinAssetPack[];
extern const size_t kBuiltinAssetPack_size;
}

using namespace badge;

static bool write_pbm(const Framebuffer &fb, const std::string &path) {
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P4\n%d %d\n", Framebuffer::kWidth, Framebuffer::kHeight);
  const int stride = (Framebuffer::kWidth + 7) / 8;
  std::vector<uint8_t> row(stride);
  for (int y = 0; y < Framebuffer::kHeight; ++y) {
    std::fill(row.begin(), row.end(), 0);
    for (int x = 0; x < Framebuffer::kWidth; ++x)
      if (fb.get(x, y) == Ink::Black) row[x >> 3] |= uint8_t(0x80 >> (x & 7));
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
  return true;
}

static std::vector<uint8_t> read_file(const char *path) {
  std::vector<uint8_t> d;
  FILE *f = std::fopen(path, "rb");
  if (!f) return d;
  uint8_t buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
  std::fclose(f);
  return d;
}

int main(int argc, char **argv) {
  static Settings s;
  settings_defaults(&s);
  std::string out = ".";
  std::vector<uint8_t> pack(kBuiltinAssetPack, kBuiltinAssetPack + kBuiltinAssetPack_size);
  bool no_portrait = false, faults = false;
  int layout = -1;
  std::string screens = "badge,card,projects,project-qr,qr,info";
  std::string suffix;
  StatusInfo status;
  status.battery.display = PowerDisplay::Battery;
  status.battery.bars = 3;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> const char * {
      if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); }
      return argv[++i];
    };
    if (a == "--out") out = next();
    else if (a == "--pack") { pack = read_file(next()); }
    else if (a == "--no-portrait") no_portrait = true;
    else if (a == "--layout") layout = std::atoi(next());
    else if (a == "--screens") screens = next();
    else if (a == "--set") {
      std::string kv = next();
      auto eq = kv.find('=');
      const FieldDesc *f = eq == std::string::npos ? nullptr : find_field(kv.substr(0, eq).c_str());
      if (!f) { std::fprintf(stderr, "bad --set %s\n", kv.c_str()); return 2; }
      std::string v = kv.substr(eq + 1);
      for (size_t p; (p = v.find("\\n")) != std::string::npos;) v.replace(p, 2, "\n");
      SetResult r = settings_set(&s, *f, v.c_str());
      if (r != SetResult::Ok) { std::fprintf(stderr, "--set %s: %s\n", f->key, set_result_str(r)); return 2; }
    } else if (a == "--battery") {
      // none | usb | invalid | low | 0..4 (bars)
      std::string b = next();
      status.battery.display = PowerDisplay::Battery;
      if (b == "none") status.battery.display = PowerDisplay::Unknown;
      else if (b == "usb") status.battery.display = PowerDisplay::Usb;
      else if (b == "invalid") status.battery.display = PowerDisplay::Invalid;
      else if (b == "low") { status.battery.bars = 0; status.battery.low = true; }
      else status.battery.bars = uint8_t(std::atoi(b.c_str()) & 7);
    } else if (a == "--gesture") {
      std::string g = next();
      status.gesture = g == "on" ? GestureIndicator::On : g == "fault" ? GestureIndicator::Fault : GestureIndicator::Off;
    } else if (a == "--faults") {
      faults = true;
    } else if (a == "--suffix") {
      suffix = next();
    } else if (a == "--dump-fields") {
      size_t n;
      const FieldDesc *f = settings_fields(&n);
      for (size_t k = 0; k < n; ++k) {
        const char *t = f[k].type == FieldType::Str ? "str" : f[k].type == FieldType::Bool ? "bool"
                        : f[k].type == FieldType::U8 ? "u8" : "u16";
        std::printf("%s %s %u %u %u\n", f[k].key, t, f[k].size, f[k].min, f[k].max);
      }
      return 0;
    } else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); return 2; }
  }

  RenderContext ctx;
  ctx.settings = &s;
  ctx.status = status;
  AssetPackInfo pi = asset_pack_validate(pack.data(), pack.size());
  if (pi.status == AssetStatus::Ok && !no_portrait) asset_pack_bitmap(pack.data(), kAssetIdPortrait, &ctx.portrait);
  else if (!no_portrait) std::fprintf(stderr, "asset pack: %s (rendering without portrait)\n", asset_status_str(pi.status));

  static InfoLines info;
  if (!faults) {
    info.add("Firmware  BHIHKH! (host preview)");
    info.add("Battery   3.86 V (filtered 3.87) | 3/4 bars | approx.");
    info.add("Gesture   off | sensor standby (off) | swipes 0");
    info.add("Reset     power-on  |  boots 1  |  faults 0");
    info.add("Settings  slot A seq 3  |  staged: clean");
    info.add("Assets    flash pack ok (1 entry, 1720 B)");
    info.add("Display   dual-core  |  full 3 partial 1 skipped 2");
    info.add("Memory    heap free 180 KiB  |  stack margin 3.1 KiB");
    ctx.recovery_reason = "3 watchdog resets";
  } else {
    // Same line formats as build_info_lines() in main.cpp, fault values.
    info.add("Firmware  HOST DIAGNOSTIC SAMPLE - fault states");
    info.add("Battery   INVALID reading 0.41 V");
    info.add("Gesture   on | sensor fault | swipes 0");
    info.add("Reset     watchdog timeout | boot 4 | streak 3");
    info.add("Last      core1 display heartbeat stalled");
    info.add("Settings  slot B seq 4294967295 | recovered | UNSAVED");
    info.add("Assets    built-in (flash pack invalid): ok");
    info.add("Display   dual-core | full 1 part 0 skip 0 | TIMEOUTS");
    info.add("Memory    flash 176 K | stack free 212/96 B");
    ctx.recovery_reason = "repeated crashes";
  }
  ctx.info = &info;

  static Framebuffer fb;
  const int layouts[2] = {0, 1};
  size_t start = 0;
  while (start <= screens.size()) {
    size_t comma = screens.find(',', start);
    std::string name = screens.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    start = comma == std::string::npos ? screens.size() + 1 : comma + 1;
    if (name.empty()) continue;
    Screen sc;
    if (!screen_from_name(name.c_str(), &sc)) { std::fprintf(stderr, "unknown screen %s\n", name.c_str()); return 2; }
    View v;
    v.screen = sc;
    const bool per_project = sc == Screen::Projects || sc == Screen::ProjectQr;
    const int nproj = per_project ? configured_project_count(s.profile) : 1;
    for (int li = 0; li < 2; ++li) {
      const int lay = layouts[li];
      if (layout >= 0 && lay != layout) continue;
      if (sc != Screen::Badge && li > 0) continue;  // only the badge differs by layout
      v.layout = uint8_t(lay);
      for (int p = 0; p < (nproj ? nproj : 1); ++p) {
        v.project = uint8_t(p);
        if (sc == Screen::ProjectQr && !project_url(s.profile, p)) continue;  // no URL: no QR screen
        render(fb, v, ctx);
        std::string file = out + "/" + name;
        if (sc == Screen::Badge) file += lay ? "_layoutB" : "_layoutA";
        if (per_project && nproj > 1) file += "_" + std::to_string(p + 1);
        file += suffix + ".pbm";
        if (!write_pbm(fb, file)) { std::fprintf(stderr, "cannot write %s\n", file.c_str()); return 1; }
        std::printf("%s\n", file.c_str());
      }
    }
  }
  return 0;
}
