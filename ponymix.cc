#include "pulse.h"

#include <err.h>
#include <getopt.h>
#include <unistd.h>

#include <algorithm>
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

static enum DeviceType opt_devtype;
static bool opt_listrestrict;
static const char* opt_action;
static const char* opt_device;
static const char* opt_card;
static Color color;

static const char* type_to_string(enum DeviceType t) {
  switch (t) {
  case DEVTYPE_SINK:
    return "sink";
  case DEVTYPE_SOURCE:
    return "source";
  case DEVTYPE_SINK_INPUT:
    return "sink-input";
  case DEVTYPE_SOURCE_OUTPUT:
    return "source-output";
  }

  /* impossibiru! */
  throw std::out_of_range("device type out of range");
}

static enum DeviceType string_to_devtype_or_die(const char* str) {
  static std::map<string, enum DeviceType> typemap = {
    { "sink",           DEVTYPE_SINK          },
    { "source",         DEVTYPE_SOURCE        },
    { "sink-input",     DEVTYPE_SINK_INPUT    },
    { "source-output",  DEVTYPE_SOURCE_OUTPUT },
  };
  try {
    return typemap.at(str);
  } catch(std::out_of_range) {
    errx(1, "error: Invalid device type specified: %s", str);
  }
}

static Device* string_to_device_or_die(PulseClient& ponymix,
                                       string arg,
                                       enum DeviceType type) {
  Device* device = ponymix.GetDevice(arg, type);
  if (device == nullptr) errx(1, "no match found for device: %s", arg.c_str());
  return device;
}

static void Print(const Device& device, bool shirt) {
  if (shirt) {
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

static void Print(const Card& card, bool shirt) {
  if (shirt) {
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

static void Print(const Profile& profile, bool active, bool shirt) {
  if (shirt) {
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
  Print(*ponymix.GetSink(info.sink), false);
  Print(*ponymix.GetSource(info.source), false);
  return 0;
}

static int list_devices(PulseClient& ponymix, bool shirt) {
  if (opt_listrestrict) {
    const auto& devices = ponymix.GetDevices(opt_devtype);
    for (const auto& d : devices) Print(d, shirt);
    return 0;
  }

  const auto& sinks = ponymix.GetSinks();
  for (const auto& s : sinks) Print(s, shirt);

  const auto& sources = ponymix.GetSources();
  for (const auto& s : sources) Print(s, shirt);

  const auto& sinkinputs = ponymix.GetSinkInputs();
  for (const auto& s : sinkinputs) Print(s, shirt);

  const auto& sourceoutputs = ponymix.GetSourceOutputs();
  for (const auto& s : sourceoutputs) Print(s, shirt);

  return 0;
}

static int List(PulseClient& ponymix, int, char*[]) {
  return list_devices(ponymix, false);
}

static int ListShort(PulseClient& ponymix, int, char*[]) {
  return list_devices(ponymix, true);
}

static int list_cards(PulseClient& ponymix, bool shirt) {
  const auto& cards = ponymix.GetCards();
  for (const auto& c : cards) Print(c, shirt);

  return 0;
}

static int ListCards(PulseClient& ponymix, int, char*[]) {
  return list_cards(ponymix, false);
}

static int ListCardsShort(PulseClient& ponymix, int, char*[]) {
  return list_cards(ponymix, true);
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

static int list_profiles(PulseClient& ponymix, bool shirt) {
  auto card = resolve_active_card_or_die(ponymix);

  const auto& profiles = card->Profiles();
  for (const auto& p : profiles) Print(p,
                                       p.name == card->ActiveProfile().name,
                                       shirt);

  return 0;
}

static int ListProfiles(PulseClient& ponymix, int, char*[]) {
  return list_profiles(ponymix, false);
}

static int ListProfilesShort(PulseClient& ponymix, int, char*[]) {
  return list_profiles(ponymix, true);
}

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

  if (!ponymix.SetVolume(*device, volume)) return 1;

  printf("%d\n", device->Volume());

  return 0;
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

  if (!ponymix.SetBalance(*device, balance)) return 1;

  printf("%d\n", device->Balance());

  return 0;
}

static int AdjBalance(PulseClient& ponymix, int, char* argv[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long balance;
  try {
    balance = std::stol(argv[0]);
  } catch (std::invalid_argument) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  if (!ponymix.SetBalance(*device, device->Balance() + balance)) return 1;

  printf("%d\n", device->Balance());

  return 0;
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

  ponymix.SetVolumeRange(0, 100);
  if (!(ponymix.*adjust)(*device, delta)) return 1;

  printf("%d\n", device->Volume());

  return 0;
}

static int IncreaseVolume(PulseClient& ponymix, int, char* argv[]) {
  return adj_volume(ponymix, &PulseClient::IncreaseVolume, argv);
}

static int DecreaseVolume(PulseClient& ponymix, int, char* argv[]) {
  return adj_volume(ponymix, &PulseClient::DecreaseVolume, argv);
}

static int Mute(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  if (!ponymix.SetMute(*device, true)) return 1;

  printf("%d\n", device->Volume());

  return 0;
}

static int Unmute(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  if (!ponymix.SetMute(*device, false)) return 1;

  printf("%d\n", device->Volume());

  return 0;
}

static int ToggleMute(PulseClient& ponymix, int, char*[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  if (!ponymix.SetMute(*device, !ponymix.IsMuted(*device))) return 1;

  printf("%d\n", device->Volume());

  return 0;
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
  enum DeviceType target_devtype = opt_devtype;
  switch (opt_devtype) {
  case DEVTYPE_SOURCE:
    opt_devtype = DEVTYPE_SOURCE_OUTPUT;
  case DEVTYPE_SOURCE_OUTPUT:
    target_devtype = DEVTYPE_SOURCE;
    break;
  case DEVTYPE_SINK:
    opt_devtype = DEVTYPE_SINK_INPUT;
  case DEVTYPE_SINK_INPUT:
    target_devtype = DEVTYPE_SINK;
    break;
  }

  // Does this even work?
  auto source = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  auto target = string_to_device_or_die(ponymix, argv[0], target_devtype);

  return !ponymix.Move(*source, *target);
}

static int Kill(PulseClient& ponymix, int, char*[]) {
  switch (opt_devtype) {
  case DEVTYPE_SOURCE:
    opt_devtype = DEVTYPE_SOURCE_OUTPUT;
    break;
  case DEVTYPE_SINK:
    opt_devtype = DEVTYPE_SINK_INPUT;
    break;
  default:
    break;
  }

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  return !ponymix.Kill(*device);
}

static const Command& string_to_command(const char* str) {
  static std::map<string, const Command> actionmap = {
    // command name       function    arg min  arg max
    { "defaults",            { ShowDefaults,        { 0, 0 } } },
    { "list",                { List,                { 0, 0 } } },
    { "list-short",          { ListShort,           { 0, 0 } } },
    { "list-cards",          { ListCards,           { 0, 0 } } },
    { "list-cards-short",    { ListCardsShort,      { 0, 0 } } },
    { "list-profiles",       { ListProfiles,        { 0, 0 } } },
    { "list-profiles-short", { ListProfilesShort,   { 0, 0 } } },
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
    { "kill",                { Kill,                { 0, 0 } } }
  };

  try {
    return actionmap.at(str);
  } catch(std::out_of_range) {
    errx(1, "error: Invalid action specified: %s", str);
  }
}

static void usage() {
  printf("usage: %s [options] <command>...\n", program_invocation_short_name);
  fputs("\nOptions:\n"
        " -h, --help              display this help and exit\n\n"

        " -c, --card CARD         target card (index or name)\n"
        " -d, --device DEVICE     target device (index or name)\n"
        " -t, --devtype TYPE      device type\n"
        "     --source            alias to -t source\n"
        "     --input             alais to -t source\n"
        "     --sink              alias to -t sink\n"
        "     --output            alias to -t sink\n"
        "     --sink-input        alias to -t sink-input\n"
        "     --source-output     alias to -t source-output\n", stdout);

  fputs("\nDevice Commands:\n"
        "  help                   display this message\n"
        "  defaults               list default devices (default command)\n"
        "  set-default            set default device by ID\n"
        "  list                   list available devices\n"
        "  list-short             list available devices (short form)\n"
        "  list-cards             list available cards\n"
        "  list-cards-short       list available cards (short form)\n"
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
        "  is-muted               check if muted\n", stdout);
  fputs("\nApplication Commands:\n"
        "  move DEVICE            move target device to DEVICE\n"
        "  kill DEVICE            kill target DEVICE\n", stdout);

  fputs("\nCard Commands:\n"
        "  list-profiles          list available profiles for a card\n"
        "  list-profiles-short    list available profiles for a card"
                                  "(short form)\n"
        "  get-profile            get active profile for card\n"
        "  set-profile PROFILE    set profile for a card\n", stdout);

  exit(EXIT_SUCCESS);
}

void error_wrong_args(const Command& cmd, const char* cmdname) {
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

  const Command& cmd = string_to_command(opt_action);
  if (cmd.args.InRange(argc) != 0) error_wrong_args(cmd, opt_action);

  return cmd.fn(ponymix, argc, argv);
}

bool parse_options(int argc, char** argv) {
  static const struct option opts[] = {
    { "card",           required_argument, 0, 'c' },
    { "device",         required_argument, 0, 'd' },
    { "help",           no_argument,       0, 'h' },
    { "type",           required_argument, 0, 't' },
    { "sink",           no_argument,       0, 0x100 },
    { "output",         no_argument,       0, 0x101 },
    { "source",         no_argument,       0, 0x102 },
    { "input",          no_argument,       0, 0x103 },
    { "sink-input",     no_argument,       0, 0x104 },
    { "source-output",  no_argument,       0, 0x105 },
    { 0, 0, 0, 0 },
  };

  for (;;) {
    int opt = getopt_long(argc, argv, "c:d:ht:", opts, NULL);
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
    case 't':
      opt_devtype = string_to_devtype_or_die(optarg);
      opt_listrestrict = true;
      break;
    case 0x100:
    case 0x101:
      opt_devtype = DEVTYPE_SINK;
      opt_listrestrict = true;
      break;
    case 0x102:
    case 0x103:
      opt_devtype = DEVTYPE_SOURCE;
      opt_listrestrict = true;
      break;
    case 0x104:
      opt_devtype = DEVTYPE_SINK_INPUT;
      opt_listrestrict = true;
      break;
    case 0x105:
      opt_devtype = DEVTYPE_SOURCE_OUTPUT;
      opt_listrestrict = true;
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
  opt_devtype = DEVTYPE_SINK;
  opt_device = defaults.sink.c_str();

  if (!parse_options(argc, argv)) return 1;
  argc -= optind;
  argv += optind;

  return CommandDispatch(ponymix, argc, argv);
}

// vim: set et ts=2 sw=2:
