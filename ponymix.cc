#include "pulse.h"

#include <err.h>
#include <getopt.h>

#include <algorithm>
#include <map>
#include <stdexcept>

enum Action {
  ACTION_DEFAULTS,
  ACTION_LIST,
  ACTION_LISTCARDS,
  ACTION_LISTPROFILES,
  ACTION_GETVOL,
  ACTION_SETVOL,
  ACTION_GETBAL,
  ACTION_SETBAL,
  ACTION_ADJBAL,
  ACTION_INCREASE,
  ACTION_DECREASE,
  ACTION_MUTE,
  ACTION_UNMUTE,
  ACTION_TOGGLE,
  ACTION_ISMUTED,
  ACTION_SETDEFAULT,
  ACTION_GETPROFILE,
  ACTION_SETPROFILE,
  ACTION_MOVE,
  ACTION_KILL,
  ACTION_INVALID,
};

struct Command {
  int (*fn)(PulseClient&, int, char*[]);
  Range<int> args;
};

static enum DeviceType opt_devtype;
static const char* opt_action;
static const char* opt_device;
static const char* opt_card;

static const char* type_to_string(enum DeviceType t) {
  switch (t) {
  case DEVTYPE_SINK:
    return "sink";
  case DEVTYPE_SOURCE:
    return "source";
  case DEVTYPE_SINK_INPUT:
    return "sink input";
  case DEVTYPE_SOURCE_OUTPUT:
    return "source output";
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

static void Print(const Device& device) {
  printf("%s %d: %s\n"
         "  %s\n"
         "  Avg. Volume: %d%%%s\n",
         type_to_string(device.Type()),
         device.Index(),
         device.Name().c_str(),
         device.Desc().c_str(),
         device.Volume(),
         device.Muted() ? " [muted]" : "");
}

static void Print(const Card& card) {
  printf("%s\n", card.Name().c_str());
}

static void Print(const Profile& profile, bool active) {
  printf("%s: %s%s\n",
         profile.name.c_str(), profile.desc.c_str(), active ? " [active]" : "");
}

static int ShowDefaults(PulseClient& ponymix, int, char*[]) {
  const auto& info = ponymix.GetDefaults();
  Print(*ponymix.GetSink(info.sink));
  Print(*ponymix.GetSource(info.source));
  return 0;
}

static int List(PulseClient& ponymix, int, char*[]) {
  const auto& sinks = ponymix.GetSinks();
  for (const auto& s : sinks) Print(s);

  const auto& sources = ponymix.GetSources();
  for (const auto& s : sources) Print(s);

  const auto& sinkinputs = ponymix.GetSinkInputs();
  for (const auto& s : sinkinputs) Print(s);

  const auto& sourceoutputs = ponymix.GetSourceOutputs();
  for (const auto& s : sourceoutputs) Print(s);

  return 0;
}

static int ListCards(PulseClient& ponymix, int, char*[]) {
  const auto& cards = ponymix.GetCards();
  for (const auto& c : cards) Print(c);

  return 0;
}

static int ListProfiles(PulseClient& ponymix, int, char*[]) {
  if (opt_card == nullptr) errx(1, "error: no card selected");

  auto card = ponymix.GetCard(opt_card);
  if (card == nullptr) errx(1, "error: no match found for card: %s", opt_card);

  const auto& profiles = card->Profiles();
  for (const auto& p : profiles) Print(p, p.name == card->ActiveProfile().name);

  return 0;
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

static int IncreaseVolume(PulseClient& ponymix, int, char* argv[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long delta;
  try {
    delta = std::stol(argv[0]);
  } catch (std::invalid_argument) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  if (!ponymix.SetVolume(*device, device->Volume() + delta)) return 1;

  printf("%d\n", device->Volume());

  return 0;
}

static int DecreaseVolume(PulseClient& ponymix, int, char* argv[]) {
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long delta;
  try {
    delta = std::stol(argv[0]);
  } catch (std::invalid_argument) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  if (!ponymix.SetVolume(*device, device->Volume() - delta)) return 1;

  printf("%d\n", device->Volume());

  return 0;
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
  if (opt_card == nullptr) errx(1, "error: no card selected");

  auto card = ponymix.GetCard(opt_card);
  if (card == nullptr) errx(1, "error: no match found for card: %s", opt_card);

  printf("%s\n", card->ActiveProfile().name.c_str());

  return true;
}

static int SetProfile(PulseClient& ponymix, int, char* argv[]) {
  if (opt_card == nullptr) errx(1, "error: no card selected");

  auto card = ponymix.GetCard(opt_card);
  if (card == nullptr) errx(1, "error: no match found for card: %s", opt_card);

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
  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  return !ponymix.Kill(*device);
}

static const Command& string_to_command(const char* str) {
  static std::map<string, const Command> actionmap = {
    // command name       function    arg min  arg max
    { "defaults",       { ShowDefaults,   { 0, 0 } } },
    { "list",           { List,           { 0, 0 } } },
    { "list-cards",     { ListCards,      { 0, 0 } } },
    { "list-profiles",  { ListProfiles,   { 0, 0 } } },
    { "get-volume",     { GetVolume,      { 0, 0 } } },
    { "set-volume",     { SetVolume,      { 1, 1 } } },
    { "get-balance",    { GetBalance,     { 0, 0 } } },
    { "set-balance",    { SetBalance,     { 1, 1 } } },
    { "adj-balance",    { AdjBalance,     { 1, 1 } } },
    { "increase",       { IncreaseVolume, { 1, 1 } } },
    { "decrease",       { DecreaseVolume, { 1, 1 } } },
    { "mute",           { Mute,           { 0, 0 } } },
    { "unmute",         { Unmute,         { 0, 0 } } },
    { "toggle",         { ToggleMute,     { 0, 0 } } },
    { "is-muted",       { IsMuted,        { 0, 0 } } },
    { "set-default",    { SetDefault,     { 0, 0 } } },
    { "get-profile",    { GetProfile,     { 0, 0 } } },
    { "set-profile",    { SetProfile,     { 1, 1 } } },
    { "move",           { Move,           { 1, 1 } } },
    { "kill",           { Kill,           { 0, 0 } } }
  };

  try {
    return actionmap.at(str);
  } catch(std::out_of_range) {
    errx(1, "error: Invalid action specified: %s", str);
  }
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

  const Command& cmd = string_to_command(opt_action);
  if (cmd.args.InRange(argc) != 0) error_wrong_args(cmd, opt_action);

  return cmd.fn(ponymix, argc, argv);
}

void usage() {
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

  fputs("\nCommon Commands:\n"
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
        "  is-muted               check if muted\n", stdout);

  fputs("\nCard Commands:\n"
        "  list-profiles          list available profiles for a card\n"
        "  get-profile            get active profile for card\n"
        "  set-profile PROFILE    set profile for a card\n"

        "\nDevice Commands:\n"
        "  defaults               list default devices (default command)\n"
        "  set-default DEVICE     set default device by ID\n"

        "\nApplication Commands:\n"
        "  move DEVICE            move target device to DEVICE\n"
        "  kill DEVICE            kill target DEVICE\n", stdout);

  exit(EXIT_SUCCESS);
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
      break;
    case 0x100:
    case 0x101:
      opt_devtype = DEVTYPE_SINK;
      break;
    case 0x102:
    case 0x103:
      opt_devtype = DEVTYPE_SOURCE;
      break;
    case 0x104:
      opt_devtype = DEVTYPE_SINK_INPUT;
      break;
    case 0x105:
      opt_devtype = DEVTYPE_SOURCE_OUTPUT;
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

  // defaults
  ServerInfo defaults = ponymix.GetDefaults();
  opt_action = "defaults";
  opt_devtype = DEVTYPE_SINK;
  opt_device = defaults.sink.c_str();

  if (!parse_options(argc, argv)) return 1;
  argc -= optind;
  argv += optind;

  // cards are tricky... find the one that belongs to the chosen sink.
  if (opt_card == nullptr) {
    const Device* device = ponymix.GetDevice(opt_device, opt_devtype);
    if (device) {
      const Card* card = ponymix.GetCard(*device);
      if (card) opt_card = card->Name().c_str();
    }
  }

  return CommandDispatch(ponymix, argc, argv);
}

// vim: set et ts=2 sw=2:
