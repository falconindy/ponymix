#include "pulse.h"

#include <err.h>
#include <getopt.h>

#include <algorithm>
#include <map>
#include <stdexcept>

#define _unused_ __attribute__((unused))

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

static enum DeviceType opt_devtype;
static enum Action opt_action;
static const char* opt_device;
static const char* opt_card;

static const int kMinVolume = 0;
static const int kMaxVolume = 150;
static const int kMinBalance = -100;
static const int kMaxBalance = 100;

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
  return NULL;
}

static enum Action string_to_action(const char* str) {
  static std::map<string, enum Action> actionmap = {
    { "defaults",       ACTION_DEFAULTS     },
    { "list",           ACTION_LIST         },
    { "list-cards",     ACTION_LISTCARDS    },
    { "list-profiles",  ACTION_LISTPROFILES },
    { "get-volume",     ACTION_GETVOL       },
    { "set-volume",     ACTION_SETVOL       },
    { "get-balance",    ACTION_GETBAL       },
    { "set-balance",    ACTION_SETBAL       },
    { "adj-balance",    ACTION_ADJBAL       },
    { "increase",       ACTION_INCREASE     },
    { "decrease",       ACTION_DECREASE     },
    { "mute",           ACTION_MUTE         },
    { "unmute",         ACTION_UNMUTE       },
    { "toggle",         ACTION_TOGGLE       },
    { "is-muted",       ACTION_ISMUTED      },
    { "set-default",    ACTION_SETDEFAULT   },
    { "get-profile",    ACTION_GETPROFILE   },
    { "set-profile",    ACTION_SETPROFILE   },
    { "move",           ACTION_MOVE         },
    { "kill",           ACTION_KILL         }
  };

  try {
    return actionmap.at(str);
  } catch(std::out_of_range) {
    errx(1, "error: Invalid action specified: %s", str);
  }
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

static Device* string_to_device(Pulse& ponymix, string arg, enum DeviceType type) {
  switch (type) {
  case DEVTYPE_SINK:
    return ponymix.GetSink(arg);
  case DEVTYPE_SOURCE:
    return ponymix.GetSource(arg);
  case DEVTYPE_SOURCE_OUTPUT:
    return ponymix.GetSourceOutput(arg);
  case DEVTYPE_SINK_INPUT:
    return ponymix.GetSinkInput(arg);
  default:
  return nullptr;
  }
}

static Device* string_to_device_or_die(Pulse& ponymix,
                                       string arg,
                                       enum DeviceType type) {
  Device* device = string_to_device(ponymix, arg, type);
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

static int ShowDefaults(Pulse& ponymix,
                        int argc _unused_,
                        char* argv[] _unused_) {
  const auto& info = ponymix.GetDefaults();
  Print(*ponymix.GetSink(info.sink));
  Print(*ponymix.GetSource(info.source));
  return 0;
}

static int List(Pulse& ponymix, int argc _unused_, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: list requires 0 arguments");

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

static int ListCards(Pulse& ponymix, int argc _unused_, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: list-cards requires 0 arguments");

  const auto& cards = ponymix.GetCards();
  for (const auto& c : cards) Print(c);

  return 0;
}

static int ListProfiles(Pulse& ponymix, int argc _unused_, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: list-profiles requires 0 arguments");

  // TODO: figure out how to get a list of cards?
  auto card = ponymix.GetCard(opt_card);
  if (card == nullptr) errx(1, "error: no match found for card: %s", opt_card);

  const auto& profiles = card->Profiles();
  for (const auto& p : profiles) Print(p, p.name == card->ActiveProfile().name);

  return 0;
}

static int GetVolume(Pulse& ponymix, int argc, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: get-volume requires 0 arguments");

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  printf("%d\n", device->Volume());
  return 0;
}

static int SetVolume(Pulse& ponymix, int argc, char* argv[]) {
  if (argc != 1) errx(1, "error: set-volume requires exactly 1 argument");

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long volume;
  try {
    volume = std::stol(argv[0]);
  } catch (std::invalid_argument _) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  if (!ponymix.SetVolume(*device, volume)) return 1;

  printf("%d\n", device->Volume());

  return 0;
}

static int GetBalance(Pulse& ponymix, int argc, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: get-balance requires 0 arguments");

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  printf("%d\n", device->Balance());
  return 0;
}

static int SetBalance(Pulse& ponymix, int argc, char* argv[]) {
  if (argc != 1) errx(1, "error: set-balance requires exactly 1 argument");

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long balance;
  try {
    balance = std::stol(argv[0]);
  } catch (std::invalid_argument _) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  if (!ponymix.SetBalance(*device, balance)) return 1;

  printf("%d\n", device->Balance());

  return 0;
}

static int AdjBalance(Pulse& ponymix, int argc, char* argv[]) {
  if (argc != 1) errx(1, "error: adj-balance requires exactly 1 argument");

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long balance;
  try {
    balance = std::stol(argv[0]);
  } catch (std::invalid_argument _) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  if (!ponymix.SetBalance(*device, device->Balance() + balance)) return 1;

  printf("%d\n", device->Balance());

  return 0;
}

static int IncreaseVolume(Pulse& ponymix, int argc, char* argv[]) {
  if (argc != 1) errx(1, "error: increase requires exactly 1 argument");

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long delta;
  try {
    delta = std::stol(argv[0]);
  } catch (std::invalid_argument _) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  if (!ponymix.SetVolume(*device, device->Volume() + delta)) return 1;

  printf("%d\n", device->Volume());

  return 0;
}

static int DecreaseVolume(Pulse& ponymix, int argc, char* argv[]) {
  if (argc != 1) errx(1, "error: decrease requires exactly 1 argument");

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  long delta;
  try {
    delta = std::stol(argv[0]);
  } catch (std::invalid_argument _) {
    errx(1, "error: failed to convert string to integer: %s", argv[0]);
  }

  if (!ponymix.SetVolume(*device, device->Volume() - delta)) return 1;

  printf("%d\n", device->Volume());

  return 0;
}

static int Mute(Pulse& ponymix, int argc, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: mute requires 0 arguments");

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  if (!ponymix.SetMute(*device, true)) return 1;

  printf("%d\n", device->Volume());

  return 0;
}

static int Unmute(Pulse& ponymix, int argc, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: unmute requires 0 arguments");

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  if (!ponymix.SetMute(*device, false)) return 1;

  printf("%d\n", device->Volume());

  return 0;
}

static int ToggleMute(Pulse& ponymix, int argc, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: toggle requires 0 arguments");

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  if (!ponymix.SetMute(*device, !ponymix.IsMuted(*device))) return 1;

  printf("%d\n", device->Volume());

  return 0;
}

static int IsMuted(Pulse& ponymix, int argc, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: is-muted requires 0 arguments"); 

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);
  return !ponymix.IsMuted(*device);
}

static int SetDefault(Pulse& ponymix, int argc, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: set-default requires 0 arguments"); 

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  return !ponymix.SetDefault(*device);
}

static int GetProfile(Pulse& ponymix, int argc, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: get-profile requires 0 arguments");

  auto card = ponymix.GetCard(opt_card);
  if (card == nullptr) errx(1, "error: no match found for card: %s", opt_card);

  printf("%s\n", card->ActiveProfile().name.c_str());

  return true;
}

static int SetProfile(Pulse& ponymix, int argc, char* argv[] _unused_) {
  if (argc != 1) errx(1, "error: set-profile requires 1 argument");

  auto card = ponymix.GetCard(opt_card);
  if (card == nullptr) errx(1, "error: no match found for card: %s", opt_card);

  return !ponymix.SetProfile(*card, argv[0]);
}

static int Move(Pulse& ponymix, int argc, char* argv[]) {
  if (argc != 1) errx(1, "error: move requires 1 argument");

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

static int Kill(Pulse& ponymix, int argc, char* argv[] _unused_) {
  if (argc != 0) errx(1, "error: set-default requires 0 arguments"); 

  auto device = string_to_device_or_die(ponymix, opt_device, opt_devtype);

  return !ponymix.Kill(*device);
}

static int (*fn[])(Pulse& ponymix, int argc, char* argv[]) = {
  [ACTION_DEFAULTS] = ShowDefaults,
  [ACTION_LIST] = List,
  [ACTION_LISTCARDS] = ListCards,
  [ACTION_LISTPROFILES] = ListProfiles,
  [ACTION_GETVOL] = GetVolume,
  [ACTION_SETVOL] = SetVolume,
  [ACTION_GETBAL] = GetBalance,
  [ACTION_SETBAL] = SetBalance,
  [ACTION_ADJBAL] = AdjBalance,
  [ACTION_INCREASE] = IncreaseVolume,
  [ACTION_DECREASE] = DecreaseVolume,
  [ACTION_MUTE] = Mute,
  [ACTION_UNMUTE] = Unmute,
  [ACTION_TOGGLE] = ToggleMute,
  [ACTION_ISMUTED] = IsMuted,
  [ACTION_SETDEFAULT] = SetDefault,
  [ACTION_GETPROFILE] = GetProfile,
  [ACTION_SETPROFILE] = SetProfile,
  [ACTION_MOVE] = Move,
  [ACTION_KILL] = Kill,
};

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
  Pulse ponymix("ponymix");
  ponymix.Populate();

  // defaults
  opt_action = ACTION_DEFAULTS;
  opt_devtype = DEVTYPE_SINK;
  opt_device = ponymix.GetDefaults().sink.c_str();
  opt_card = ponymix.GetCards()[0].Name().c_str();

  if (!parse_options(argc, argv)) return 1;
  argc -= optind;
  argv += optind;

  if (argc > 0) {
    opt_action = string_to_action(argv[0]);
    argc--;
    argv++;
  }

  return fn[opt_action](ponymix, argc, argv);
}

// vim: set et ts=2 sw=2:
