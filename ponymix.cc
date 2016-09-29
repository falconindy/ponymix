#include "pulse.h"

#include <err.h>
#include <getopt.h>
#include <unistd.h>

#include <map>
#include <stdexcept>

struct Command {
  int (*fn)(PulseClient&, int, char*[]);
  Range<int> args;
};

struct Color {
  Color() {
    if (isatty(fileno(stdout))) {
      name = "\033[1m";
      reset = "\033[0m";
      over9000 = "\033[7;31m";
      veryhigh = "\033[31m";
      high = "\033[35m";
      mid = "\033[33m";
      low = "\033[32m";
      verylow = "\033[34m";
      mute = "\033[1;31m";
    } else {
      name = "";
      reset = "";
      over9000 = "";
      veryhigh = "";
      high = "";
      mid = "";
      low = "";
      verylow = "";
      mute = "";
    }
  }

  const char* name;
  const char* reset;

  // Volume levels
  const char* over9000;
  const char* veryhigh;
  const char* high;
  const char* mid;
  const char* low;
  const char* verylow;
  const char* mute;
};

static DeviceType opt_devtype;
static bool opt_listrestrict;
static bool opt_short;
static const char* opt_action;
static const char* opt_device;
static const char* opt_card;
static bool opt_notify;
static long opt_maxvolume;
static Color color;

static int xstrtol(const char *str, long *out) {
  char *end = nullptr;

  if (str == nullptr || *str == '\0') return -1;
  errno = 0;

  *out = strtol(str, &end, 10);
  if (errno || str == end || (end && *end)) return -1;

  return 0;
}

static const char* type_to_string(DeviceType t) {
  switch (t) {
  case DeviceType::SINK:
    return "sink";
  case DeviceType::SOURCE:
    return "source";
  case DeviceType::SINK_INPUT:
    return "sink-input";
  case DeviceType::SOURCE_OUTPUT:
    return "source-output";
  }

  throw unreachable();
}

static DeviceType string_to_devtype_or_die(const char* str) {
  static std::map<std::string, DeviceType> typemap{
    { "sink",           DeviceType::SINK          },
    { "source",         DeviceType::SOURCE        },
    { "sink-input",     DeviceType::SINK_INPUT    },
    { "source-output",  DeviceType::SOURCE_OUTPUT },
  };
  try {
    return typemap.at(str);
  } catch(std::out_of_range) {
    errx(1, "error: Invalid device type specified: %s", str);
  }
}

static Device* string_to_device_or_die(PulseClient& ponymix,
                                       std::string arg,
                                       DeviceType type) {
  Device* device = ponymix.GetDevice(arg, type);
  if (device == nullptr) errx(1, "no match found for device: %s", arg.c_str());
  return device;
}

static void Print(const Device& device) {
  if (opt_short) {
    printf("%s\t%d\t%s\t%s\n",
           type_to_string(device.Type()),
           device.Index(),
           device.Name().c_str(),
           device.Desc().c_str());
    return;
  }

  const char *mute = device.Muted() ? " [Muted]" : "";
  const char *volume_color;

  if (device.Volume() < 20) {
    volume_color = color.verylow;
  } else if (device.Volume() < 40) {
    volume_color = color.low;
  } else if (device.Volume() < 60) {
    volume_color = color.mid;
  } else if (device.Volume() < 80) {
    volume_color = color.high;
  } else if (device.Volume() <= 100) {
    volume_color = color.veryhigh;
  } else {
    volume_color = color.over9000;
  }

  printf("%s%s %d:%s %s\n"
         "  %s\n"
         "  Avg. Volume: %s%d%%%s%s%s%s\n",
         color.name,
         type_to_string(device.Type()),
         device.Index(),
         color.reset,
         device.Name().c_str(),
         device.Desc().c_str(),
         volume_color,
         device.Volume(),
         color.reset,
         color.mute,
         mute,
         color.reset);
}

static void Print(const Card& card) {
  if (opt_short) {
    printf("%s\n", card.Name().c_str());
    return;
  }

  printf("%scard %d:%s %s\n"
         "  Driver: %s\n"
         "  Active Profile: %s\n",
         color.name,
         card.Index(),
         color.reset,
         card.Name().c_str(),
         card.Driver().c_str(),
         card.ActiveProfile().name.c_str());

}

static void Print(const Profile& profile, bool active) {
  if (opt_short) {
    printf("%s\n", profile.name.c_str());
    return;
  }

  const char* active_str = active ? " [active]" : "";
  printf("%s%s%s%s%s%s\n"
         "  %s\n",
         color.name,
         profile.name.c_str(),
         color.reset,
         color.low,
         active_str,
         color.reset,
         profile.desc.c_str());
}

static int ShowDefaults(PulseClient& ponymix, int, char*[]) {
  const auto& info = ponymix.GetDefaults();
  Print(*ponymix.GetSink(info.sink));
  Print(*ponymix.GetSource(info.source));
  return 0;
}

static int List(PulseClient& ponymix, int, char*[]) {
  if (opt_listrestrict) {
    for (const auto& d : ponymix.GetDevices(opt_devtype)) Print(d);
    return 0;
  }

  for (const auto& s : ponymix.GetSinks()) Print(s);
  for (const auto& s : ponymix.GetSources()) Print(s);
  for (const auto& s : ponymix.GetSinkInputs()) Print(s);
  for (const auto& s : ponymix.GetSourceOutputs()) Print(s);

  return 0;
}

static int ListCards(PulseClient& ponymix, int, char*[]) {
  const auto& cards = ponymix.GetCards();
  for (const auto& c : cards) Print(c);

  return 0;
}

static Card* resolve_active_card_or_die(PulseClient& ponymix) {
  Card* card;
  if (opt_card == nullptr) {
    auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
    card = ponymix.GetCard(*device);
    if (card == nullptr) errx(1, "error: no card found or selected.");
  } else {
    card = ponymix.GetCard(opt_card);
    if (card == nullptr) {
      errx(1, "error: no match found for card: %s", opt_card);
    }
  }

  return card;
}

static int ListProfiles(PulseClient& ponymix, int, char*[]) {
  auto card = resolve_active_card_or_die(ponymix);

  const auto& profiles = card->Profiles();
  for (const auto& p : profiles) Print(p, p.name == card->ActiveProfile().name);

  return 0;}

static int GetVolume(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  printf("%d\n", device->Volume());
  return 0;
}

static int SetVolume(PulseClient& ponymix, int, char* argv[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long volume;
  try {
    volume = std::stol(argv[0]);
  } catch (std::invalid_argument) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  return !ponymix.SetVolume(*device, volume);
}

static int GetBalance(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  printf("%d\n", device->Balance());
  return 0;
}

static int SetBalance(PulseClient& ponymix, int, char* argv[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long balance;
  try {
    balance = std::stol(argv[0]);
  } catch (std::invalid_argument) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  return !ponymix.SetBalance(*device, balance);
}

static int AdjBalance(PulseClient& ponymix, int, char* argv[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long balance;
  try {
    balance = std::stol(argv[0]);
  } catch (std::invalid_argument) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  return !ponymix.SetBalance(*device, device->Balance() + balance);
}

static int adj_volume(PulseClient& ponymix,
                      bool (PulseClient::*adjust)(Device&, long int),
                      char* argv[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long delta;
  try {
    delta = std::stol(argv[0]);
  } catch (std::invalid_argument) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  // Allow setting the volume over 100, but don't "clip" the level back down to
  // 100 on adjustment.
  ponymix.SetVolumeRange(0, std::max(device->Volume(), static_cast<int>(opt_maxvolume)));
  return !(ponymix.*adjust)(*device, delta);
}

static int IncreaseVolume(PulseClient& ponymix, int, char* argv[]) {
  return adj_volume(ponymix, &PulseClient::IncreaseVolume, argv);
}

static int DecreaseVolume(PulseClient& ponymix, int, char* argv[]) {
  return adj_volume(ponymix, &PulseClient::DecreaseVolume, argv);
}

static int Mute(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  return !ponymix.SetMute(*device, true);
}

static int Unmute(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  return !ponymix.SetMute(*device, false);
}

static int ToggleMute(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  return !ponymix.SetMute(*device, !ponymix.IsMuted(*device));
}

static int IsMuted(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  return !ponymix.IsMuted(*device);
}

static int SetDefault(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  return !ponymix.SetDefault(*device);
}

static int GetProfile(PulseClient& ponymix, int, char*[]) {
  auto card = resolve_active_card_or_die(ponymix);
  printf("%s\n", card->ActiveProfile().name.c_str());

  return true;
}

static int SetProfile(PulseClient& ponymix, int, char* argv[]) {
  auto card = resolve_active_card_or_die(ponymix);
  return !ponymix.SetProfile(*card, argv[0]);
}

static int Move(PulseClient& ponymix, int, char* argv[]) {
  // this assignment is a lie. stfu g++
  DeviceType target_devtype = opt_devtype;
  switch (opt_devtype) {
  case DeviceType::SOURCE:
    opt_devtype = DeviceType::SOURCE_OUTPUT;
  case DeviceType::SOURCE_OUTPUT:
    target_devtype = DeviceType::SOURCE;
    break;
  case DeviceType::SINK:
    opt_devtype = DeviceType::SINK_INPUT;
  case DeviceType::SINK_INPUT:
    target_devtype = DeviceType::SINK;
    break;
  }

  // Does this even work?
  auto source = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  auto target = string_to_device_or_die(ponymix, argv[0], target_devtype);

  return !ponymix.Move(*source, *target);
}

static int Kill(PulseClient& ponymix, int, char*[]) {
  switch (opt_devtype) {
  case DeviceType::SOURCE:
    opt_devtype = DeviceType::SOURCE_OUTPUT;
    break;
  case DeviceType::SINK:
    opt_devtype = DeviceType::SINK_INPUT;
    break;
  default:
    break;
  }

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  return !ponymix.Kill(*device);
}

static int IsAvailable(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  return ponymix.Availability(*device) == Device::Availability::YES;
}

static bool endswith(const std::string& subject, const std::string& predicate) {
  if (subject.size() < predicate.size()) {
    return false;
  }

  return subject.compare(subject.size() - predicate.size(),
      predicate.size(), predicate) == 0;
}

static const std::pair<const std::string, const Command>& string_to_command(
    const char* str) {
  static std::map<std::string, const Command> actionmap{
    // command name            function         arg min  arg max
    { "defaults",            { ShowDefaults,        { 0, 0 } } },
    { "list",                { List,                { 0, 0 } } },
    { "list-short",          { List,                { 0, 0 } } },
    { "list-cards",          { ListCards,           { 0, 0 } } },
    { "list-cards-short",    { ListCards,           { 0, 0 } } },
    { "list-profiles",       { ListProfiles,        { 0, 0 } } },
    { "list-profiles-short", { ListProfiles,        { 0, 0 } } },
    { "get-volume",          { GetVolume,           { 0, 0 } } },
    { "set-volume",          { SetVolume,           { 1, 1 } } },
    { "get-balance",         { GetBalance,          { 0, 0 } } },
    { "set-balance",         { SetBalance,          { 1, 1 } } },
    { "adj-balance",         { AdjBalance,          { 1, 1 } } },
    { "increase",            { IncreaseVolume,      { 1, 1 } } },
    { "decrease",            { DecreaseVolume,      { 1, 1 } } },
    { "mute",                { Mute,                { 0, 0 } } },
    { "unmute",              { Unmute,              { 0, 0 } } },
    { "toggle",              { ToggleMute,          { 0, 0 } } },
    { "is-muted",            { IsMuted,             { 0, 0 } } },
    { "set-default",         { SetDefault,          { 0, 0 } } },
    { "get-profile",         { GetProfile,          { 0, 0 } } },
    { "set-profile",         { SetProfile,          { 1, 1 } } },
    { "move",                { Move,                { 1, 1 } } },
    { "kill",                { Kill,                { 0, 0 } } },
    { "is-available",        { IsAvailable,         { 0, 0 } } },
  };

  const auto match = actionmap.lower_bound(str);
  if (match == actionmap.end()) {
    errx(1, "error: Invalid action specified: %s", str);
  }

  // Check for exact match
  if (match->first == str) {
    return *match;
  }

  // Match on prefix, ensure only a single match
  for (auto iter = match; iter != actionmap.end(); iter++) {
    if (iter->first.find(str) != 0) {
      if (iter == match) {
        errx(1, "error: Invalid action specified: %s", str);
      } else {
        break;
      }
    }
    if (iter != match) {
      auto i = match;
      std::string cand = i->first;
      i++;
      while (i->first.find(str) == 0) {
        cand += ", " + i->first;
        i++;
      }
      errx(1, "error: Ambiguous action specified: %s (%s)", str, cand.c_str());
    }
  }

  return *match;
}

static void version() {
  if (isatty(fileno(stdout)))
    execlp("ponysay", "ponysay", "-b", "", "ponymix " PONYMIX_VERSION, NULL);

  // Some people are pony haters.
  fputs("ponymix v" PONYMIX_VERSION "\n", stdout);
  exit(EXIT_SUCCESS);
}

static void usage() {
  printf("usage: %s [options] <command>...\n", program_invocation_short_name);
  fputs("\nOptions:\n"
        " -h, --help              display this help and exit\n"
        " -V, --version           display program version and exit\n\n"

        " -c, --card CARD         target card (index or name)\n"
        " -d, --device DEVICE     target device (index or name)\n"
        " -t, --devtype TYPE      device type\n"
        " -N, --notify            use libnotify to announce volume changes\n"
        "     --max-volume VALUE  use VALUE as max volume\n"
        "     --short             output brief (parseable) lists\n"
        "     --source            alias to -t source\n"
        "     --input             alias to -t source\n"
        "     --sink              alias to -t sink\n"
        "     --output            alias to -t sink\n"
        "     --sink-input        alias to -t sink-input\n"
        "     --source-output     alias to -t source-output\n", stdout);

  fputs("\nDevice Commands:\n"
        "  help                   display this message\n"
        "  defaults               list default devices (default command)\n"
        "  set-default            set default device by ID\n"
        "  list                   list available devices\n"
        "  list-cards             list available cards\n"
        "  get-volume             get volume for device\n"
        "  set-volume VALUE       set volume for device\n"
        "  get-balance            get balance for device\n"
        "  set-balance VALUE      set balance for device\n"
        "  adj-balance VALUE      increase or decrease balance for device\n"
        "  increase VALUE         increase volume\n", stdout);
  fputs("  decrease VALUE         decrease volume\n"
        "  mute                   mute device\n"
        "  unmute                 unmute device\n"
        "  toggle                 toggle mute\n"
        "  is-muted               check if muted\n"
        "  is-available           check if available\n", stdout);
  fputs("\nApplication Commands:\n"
        "  move DEVICE            move target device to DEVICE\n"
        "  kill DEVICE            kill target DEVICE\n", stdout);

  fputs("\nCard Commands:\n"
        "  list-profiles          list available profiles for a card\n"
        "  get-profile            get active profile for card\n"
        "  set-profile PROFILE    set profile for a card\n", stdout);

  exit(EXIT_SUCCESS);
}

static void error_wrong_args(const Command& cmd, const char* cmdname) {
  if (cmd.args.min == cmd.args.max) {
    errx(1, "error: %s takes exactly %d argument%c",
        cmdname, cmd.args.min, cmd.args.min == 1 ? '\0' : 's');
  } else {
    errx(1, "error: %s takes %d to %d arguments\n",
        cmdname, cmd.args.min, cmd.args.max);
  }
}

static int CommandDispatch(PulseClient& ponymix, int argc, char *argv[]) {
  if (argc > 0) {
    opt_action = argv[0];
    argv++;
    argc--;
  }

  if (strcmp(opt_action, "help") == 0) {
    usage();
    return 0;
  }

  const auto& cmd = string_to_command(opt_action);
  if (!cmd.second.args.InRange(argc)) {
    error_wrong_args(cmd.second, cmd.first.c_str());
  }

  if (endswith(cmd.first, std::string("-short"))) {
    opt_short = true;
  }

  return cmd.second.fn(ponymix, argc, argv);
}

bool parse_options(int argc, char** argv) {
  static const struct option opts[]{
    { "card",           required_argument, 0, 'c' },
    { "device",         required_argument, 0, 'd' },
    { "help",           no_argument,       0, 'h' },
    { "notify",         no_argument,       0, 'N' },
    { "devtype",        required_argument, 0, 't' },
    { "version",        no_argument,       0, 'V' },
    { "sink",           no_argument,       0, 0x100 },
    { "output",         no_argument,       0, 0x101 },
    { "source",         no_argument,       0, 0x102 },
    { "input",          no_argument,       0, 0x103 },
    { "sink-input",     no_argument,       0, 0x104 },
    { "source-output",  no_argument,       0, 0x105 },
    { "max-volume",     required_argument, 0, 0x106 },
    { "short",          no_argument,       0, 0x107 },
    { 0, 0, 0, 0 },
  };

  for (;;) {
    int opt = getopt_long(argc, argv, "c:d:hNt:V", opts, nullptr);
    if (opt == -1)
      break;

    switch (opt) {
    case 'c':
      opt_card = optarg;
      break;
    case 'd':
      opt_device = optarg;
      break;
    case 'h':
      usage();
      break;
    case 'N':
      opt_notify = true;
      break;
    case 't':
      opt_devtype = string_to_devtype_or_die(optarg);
      opt_listrestrict = true;
      break;
    case 'V':
      version();
      break;
    case 0x100:
    case 0x101:
      opt_devtype = DeviceType::SINK;
      opt_listrestrict = true;
      break;
    case 0x102:
    case 0x103:
      opt_devtype = DeviceType::SOURCE;
      opt_listrestrict = true;
      break;
    case 0x104:
      opt_devtype = DeviceType::SINK_INPUT;
      opt_listrestrict = true;
      break;
    case 0x105:
      opt_devtype = DeviceType::SOURCE_OUTPUT;
      opt_listrestrict = true;
      break;
    case 0x106:
      if (xstrtol(optarg, &opt_maxvolume) < 0) {
        fprintf(stderr, "error: invalid max volume: %s: must be a positive integer\n",
            optarg);
        return false;
      }
      break;
    case 0x107:
      opt_short = true;
      break;
    default:
      return false;
    }
  }

  return true;
}

int main(int argc, char* argv[]) {
  PulseClient ponymix("ponymix");
  ponymix.Populate();

  // defaults. intentionally, we don't set a card -- only get
  // that on demand if a function needs it.
  ServerInfo defaults = ponymix.GetDefaults();
  opt_action = "defaults";
  opt_devtype = DeviceType::SINK;
  opt_maxvolume = 100;

  if (!parse_options(argc, argv)) return 1;
  argc -= optind;
  argv += optind;

  // Do this after parsing such that we respect any changes to opt_devtype and
  // explicit opt_device
  if (opt_device == nullptr)
    opt_device = defaults.GetDefault(opt_devtype).c_str();

#ifdef HAVE_NOTIFY
  if (opt_notify) {
    ponymix.SetNotifier(std::make_unique<LibnotifyNotifier>());
  } else
#endif
  {
    ponymix.SetNotifier(std::make_unique<CommandLineNotifier>());
  }

  return CommandDispatch(ponymix, argc, argv);
}

// vim: set et ts=2 sw=2:
