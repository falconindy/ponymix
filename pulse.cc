// Self
#include "pulse.h"

// C
#include <err.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

// C++
#include <algorithm>
#include <stdexcept>

namespace {
void connect_state_cb(pa_context* context, void* raw) {
  auto state = static_cast<enum pa_context_state*>(raw);
  *state = pa_context_get_state(context);
}

void success_cb(pa_context* context, int success, void* raw) {
  auto r = static_cast<int*>(raw);
  *r = success;
  if (!success) {
    fprintf(stderr,
            "operation failed: %s\n",
            pa_strerror(pa_context_errno(context)));
  }
}

void card_info_cb(pa_context* context,
                         const pa_card_info* info,
                         int eol,
                         void* raw) {
  if (eol < 0) {
    fprintf(stderr, "%s error in %s: \n", __func__,
        pa_strerror(pa_context_errno(context)));
    return;
  }

  if (!eol) {
    auto cards = static_cast<std::vector<Card>*>(raw);
    cards->push_back(info);
  }
}

template<typename T>
void device_info_cb(pa_context* context, const T* info, int eol, void* raw) {
  if (eol < 0) {
    fprintf(stderr, "%s error in %s: \n", __func__,
        pa_strerror(pa_context_errno(context)));
    return;
  }

  if (!eol) {
    auto devices = static_cast<std::vector<Device>*>(raw);
    devices->push_back(info);
  }
}

void server_info_cb(pa_context* context __attribute__((unused)),
                    const pa_server_info* i, void* raw) {
  auto defaults = static_cast<ServerInfo*>(raw);
  defaults->sink = i->default_sink_name;
  defaults->source = i->default_source_name;
}

pa_cvolume* value_to_cvol(long value, pa_cvolume *cvol) {
  return pa_cvolume_scale(cvol, std::max(value * PA_VOLUME_NORM / 100.0, 0.0));
}

int volume_as_percent(const pa_cvolume* cvol) {
  return round(pa_cvolume_max(cvol) * 100.0 / PA_VOLUME_NORM);
}

int xstrtol(const char *str, long *out) {
  char *end = nullptr;

  if (str == nullptr || *str == '\0') return -1;
  errno = 0;

  *out = strtol(str, &end, 10);
  if (errno || str == end || (end && *end)) return -1;

  return 0;
}

}  // namespace

PulseClient::PulseClient(std::string client_name) :
    client_name_(client_name),
    volume_range_(0, 150),
    balance_range_(-100, 100),
    notifier_(new NullNotifier) {
  enum pa_context_state state = PA_CONTEXT_CONNECTING;

  pa_proplist* proplist = pa_proplist_new();
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, client_name.c_str());
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, "com.falconindy.ponymix");
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_VERSION, PONYMIX_VERSION);
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "audio-card");

  mainloop_ = pa_mainloop_new();
  context_ = pa_context_new_with_proplist(pa_mainloop_get_api(mainloop_),
                                          nullptr, proplist);

  pa_proplist_free(proplist);

  pa_context_set_state_callback(context_, connect_state_cb, &state);
  pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
  while (state != PA_CONTEXT_READY && state != PA_CONTEXT_FAILED) {
    pa_mainloop_iterate(mainloop_, 1, nullptr);
  }

  if (state != PA_CONTEXT_READY) {
    fprintf(stderr, "failed to connect to pulse daemon: %s\n",
        pa_strerror(pa_context_errno(context_)));
    exit(EXIT_FAILURE);
  }
}

//
// Pulse Client
//
PulseClient::~PulseClient() {
  pa_context_unref(context_);
  pa_mainloop_free(mainloop_);
}

void PulseClient::Populate() {
  populate_server_info();
  populate_sinks();
  populate_sources();
  populate_cards();
}

Card* PulseClient::GetCard(const uint32_t index) {
  for (Card& card : cards_) {
    if (card.index_ == index) return &card;
  }
  return nullptr;
}

Card* PulseClient::GetCard(const std::string& name) {
  long val;
  if (xstrtol(name.c_str(), &val) == 0) {
    return GetCard(val);
  } else {
    return find_fuzzy(cards_, name);
  }
}

Card* PulseClient::GetCard(const Device& device) {
  for (Card& card : cards_) {
    if (device.card_idx_ == card.index_) return &card;
  }
  return nullptr;
}

Device* PulseClient::get_device(std::vector<Device>& devices,
                                const uint32_t index) {
  for (Device& device : devices) {
    if (device.index_ == index) return &device;
  }
  return nullptr;
}

Device* PulseClient::get_device(std::vector<Device>& devices, const std::string& name) {
  long val;
  if (xstrtol(name.c_str(), &val) == 0) {
    return get_device(devices, val);
  } else {
    return find_fuzzy(devices, name);
  }
}

Device* PulseClient::GetDevice(const uint32_t index, DeviceType type) {
  switch (type) {
  case DeviceType::SINK:
    return GetSink(index);
  case DeviceType::SOURCE:
    return GetSource(index);
  case DeviceType::SINK_INPUT:
    return GetSinkInput(index);
  case DeviceType::SOURCE_OUTPUT:
    return GetSourceOutput(index);
  }

  throw unreachable();
}

Device* PulseClient::GetDevice(const std::string& name, DeviceType type) {
  switch (type) {
  case DeviceType::SINK:
    return GetSink(name);
  case DeviceType::SOURCE:
    return GetSource(name);
  case DeviceType::SINK_INPUT:
    return GetSinkInput(name);
  case DeviceType::SOURCE_OUTPUT:
    return GetSourceOutput(name);
  }

  throw unreachable();
}

const std::vector<Device>& PulseClient::GetDevices(DeviceType type) const {
  switch (type) {
  case DeviceType::SINK:
    return GetSinks();
  case DeviceType::SOURCE:
    return GetSources();
  case DeviceType::SINK_INPUT:
    return GetSinkInputs();
  case DeviceType::SOURCE_OUTPUT:
    return GetSourceOutputs();
  }

  throw unreachable();
}

Device* PulseClient::GetSink(const uint32_t index) {
  return get_device(sinks_, index);
}

Device* PulseClient::GetSink(const std::string& name) {
  return get_device(sinks_, name);
}

Device* PulseClient::GetSource(const uint32_t index) {
  return get_device(sources_, index);
}

Device* PulseClient::GetSource(const std::string& name) {
  return get_device(sources_, name);
}

Device* PulseClient::GetSinkInput(const uint32_t index) {
  return get_device(sink_inputs_, index);
}

Device* PulseClient::GetSinkInput(const std::string& name) {
  return get_device(sink_inputs_, name);
}

Device* PulseClient::GetSourceOutput(const uint32_t index) {
  return get_device(source_outputs_, index);
}

Device* PulseClient::GetSourceOutput(const std::string& name) {
  return get_device(source_outputs_, name);
}

void PulseClient::WaitOperationComplete(pa_operation* op) {
  int r;
  while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
    pa_mainloop_iterate(mainloop_, 1, &r);
  }

  pa_operation_unref(op);
}

template<class T>
T* PulseClient::find_fuzzy(std::vector<T>& haystack, const std::string& needle) {
  std::vector<T*> res;

  for (T& item : haystack) {
    if (item.name_.find(needle) != std::string::npos) res.push_back(&item);
  }

  switch (res.size()) {
  case 0:
    return nullptr;
  case 1:
    break;
  default:
    warnx("warning: ambiguous result for '%s', using '%s'",
        needle.c_str(), res[0]->name_.c_str());
  }
  return res[0];
}

void PulseClient::populate_cards() {
  std::vector<Card> cards;
  WaitOperationComplete(pa_context_get_card_info_list(
        context_, card_info_cb, static_cast<void*>(&cards)));

  cards_ = std::move(cards);
}

void PulseClient::populate_server_info() {
  WaitOperationComplete(pa_context_get_server_info(
        context_, server_info_cb, &defaults_));
}

void PulseClient::populate_sinks() {
  std::vector<Device> sinks;
  WaitOperationComplete(pa_context_get_sink_info_list(
      context_, device_info_cb, static_cast<void*>(&sinks)));
  sinks_ = std::move(sinks);

  std::vector<Device> sink_inputs;
  WaitOperationComplete(pa_context_get_sink_input_info_list(
      context_, device_info_cb, static_cast<void*>(&sink_inputs)));
  sink_inputs_ = std::move(sink_inputs);
}

void PulseClient::populate_sources() {
  std::vector<Device> sources;
  WaitOperationComplete(pa_context_get_source_info_list(
      context_, device_info_cb, static_cast<void*>(&sources)));
  sources_ = std::move(sources);

  std::vector<Device> source_outputs;
  WaitOperationComplete(pa_context_get_source_output_info_list(
      context_, device_info_cb, static_cast<void*>(&source_outputs)));
  source_outputs_ = std::move(source_outputs);
}

bool PulseClient::SetMute(Device& device, bool mute) {
  int success;

  if (device.ops_.Mute == nullptr) {
    warnx("device does not support muting.");
    return false;
  }

  WaitOperationComplete(device.ops_.Mute(
          context_, device.index_, mute, success_cb, &success));

  if (success) {
    device.mute_ = mute;
    notifier_->Notify(mute ? NotificationType::MUTE : NotificationType::UNMUTE,
                      device.volume_percent_, mute);
  }

  return success;
}

bool PulseClient::SetVolume(Device& device, long volume) {
  int success;

  if (device.ops_.SetVolume == nullptr) {
    warnx("device does not support setting volume.");
    return false;
  }

  volume = volume_range_.Clamp(volume);
  const pa_cvolume *cvol = value_to_cvol(volume, &device.volume_);
  WaitOperationComplete(device.ops_.SetVolume(
          context_, device.index_, cvol, success_cb, &success));

  if (success) {
    device.update_volume(*cvol);
    notifier_->Notify(NotificationType::VOLUME, device.volume_percent_, device.mute_);
  }

  return success;
}

bool PulseClient::IncreaseVolume(Device& device, long increment) {
  return SetVolume(device, device.volume_percent_ + increment);
}

bool PulseClient::DecreaseVolume(Device& device, long increment) {
  return SetVolume(device, device.volume_percent_ - increment);
}

bool PulseClient::SetBalance(Device& device, long balance) {
  if (device.ops_.SetVolume == nullptr) {
    warnx("device does not support setting balance.");
    return false;
  }

  balance = balance_range_.Clamp(balance);
  pa_cvolume *cvol = pa_cvolume_set_balance(&device.volume_,
                                            &device.channels_,
                                            balance / 100.0);

  int success;
  WaitOperationComplete(device.ops_.SetVolume(
          context_, device.index_, cvol, success_cb, &success));

  if (success) {
    device.update_volume(*cvol);
    notifier_->Notify(NotificationType::BALANCE, device.balance_, false);
  }

  return success;
}

bool PulseClient::IncreaseBalance(Device& device, long increment) {
  return SetBalance(device, device.balance_ + increment);
}

bool PulseClient::DecreaseBalance(Device& device, long increment) {
  return SetBalance(device, device.balance_ - increment);
}

int PulseClient::GetVolume(const Device& device) const {
  return device.Volume();
}

int PulseClient::GetBalance(const Device& device) const {
  return device.Balance();
}

bool PulseClient::SetProfile(Card& card, const std::string& profile) {
  int success;
  WaitOperationComplete(pa_context_set_card_profile_by_index(
          context_, card.index_, profile.c_str(), success_cb, &success));

  if (success) {
    // Update the profile
    for (const Profile& p : card.profiles_) {
      if (p.name == profile) {
        card.active_profile_ = p;
        break;
      }
    }
  }

  return success;
}

bool PulseClient::Move(Device& source, Device& dest) {
  if (source.ops_.Move == nullptr) {
    warnx("source device does not support moving.");
    return false;
  }

  int success;
  WaitOperationComplete(source.ops_.Move(
          context_, source.index_, dest.index_, success_cb, &success));

  return success;
}

bool PulseClient::Kill(Device& device) {
  if (device.ops_.Kill == nullptr) {
    warnx("source device does not support being killed.");
    return false;
  }

  int success;
  WaitOperationComplete(device.ops_.Kill(
          context_, device.index_, success_cb, &success));

  if (success) remove_device(device);

  return success;
}

bool PulseClient::SetDefault(Device& device) {
  int success;

  if (device.ops_.SetDefault == nullptr) {
    warnx("device does not support defaults");
    return false;
  }

  WaitOperationComplete(device.ops_.SetDefault(
          context_, device.name_.c_str(), success_cb, &success));

  if (success) {
    switch (device.type_) {
    case DeviceType::SINK:
      defaults_.sink = device.name_;
      break;
    case DeviceType::SOURCE:
      defaults_.source = device.name_;
      break;
    default:
      errx(1, "impossible to set a default for device type %d",
           static_cast<int>(device.type_));
    }
  }

  return success;
}

void PulseClient::remove_device(Device& device) {
  std::vector<Device>* devlist = nullptr;

  switch (device.type_) {
  case DeviceType::SINK:
    devlist = &sinks_;
    break;
  case DeviceType::SINK_INPUT:
    devlist = &sink_inputs_;
    break;
  case DeviceType::SOURCE:
    devlist = &sources_;
    break;
  case DeviceType::SOURCE_OUTPUT:
    devlist = &source_outputs_;
    break;
  }
  devlist->erase(
      std::remove_if(
        devlist->begin(), devlist->end(),
        [&device](const Device& d) { return d.index_ == device.index_; }),
      devlist->end());
}

void PulseClient::SetNotifier(std::unique_ptr<Notifier> notifier) {
  notifier_ = std::move(notifier);
}

//
// Cards
//
Card::Card(const pa_card_info* info) :
    index_(info->index),
    name_(info->name),
    owner_module_(info->owner_module),
    driver_(info->driver),
    active_profile_(*info->active_profile) {
  for (int i = 0; info->profiles[i].name != nullptr; i++) {
    profiles_.push_back(info->profiles[i]);
  }
}

//
// Devices
//
Device::Device(const pa_sink_info* info) :
    type_(DeviceType::SINK),
    index_(info->index),
    name_(info->name ? info->name : ""),
    desc_(info->description),
    mute_(info->mute),
    card_idx_(info->card) {
  update_volume(info->volume);
  channels_ = info->channel_map;
  balance_ = pa_cvolume_get_balance(&volume_, &channels_) * 100.0;

  ops_.SetVolume = pa_context_set_sink_volume_by_index;
  ops_.Mute = pa_context_set_sink_mute_by_index;
  ops_.Kill = nullptr;
  ops_.Move = nullptr;
  ops_.SetDefault = pa_context_set_default_sink;

  if (info->active_port) {
    switch (info->active_port->available) {
      case PA_PORT_AVAILABLE_YES:
        available_ = Device::Availability::YES;
        break;
      case PA_PORT_AVAILABLE_NO:
        available_ = Device::Availability::NO;
        break;
      case PA_PORT_AVAILABLE_UNKNOWN:
        available_ = Device::Availability::UNKNOWN;
        break;
    }
  }
}

Device::Device(const pa_source_info* info) :
    type_(DeviceType::SOURCE),
    index_(info->index),
    name_(info->name ? info->name : ""),
    desc_(info->description),
    mute_(info->mute),
    card_idx_(info->card) {
  update_volume(info->volume);
  channels_ = info->channel_map;
  balance_ = pa_cvolume_get_balance(&volume_, &channels_) * 100.0;

  ops_.SetVolume = pa_context_set_source_volume_by_index;
  ops_.Mute = pa_context_set_source_mute_by_index;
  ops_.Kill = nullptr;
  ops_.Move = nullptr;
  ops_.SetDefault = pa_context_set_default_source;
}

Device::Device(const pa_sink_input_info* info) :
    type_(DeviceType::SINK_INPUT),
    index_(info->index),
    name_(info->name ? info->name : ""),
    mute_(info->mute),
    card_idx_(-1) {
  update_volume(info->volume);
  channels_ = info->channel_map;
  balance_ = pa_cvolume_get_balance(&volume_, &channels_) * 100.0;

  const char *desc = pa_proplist_gets(info->proplist,
                                      PA_PROP_APPLICATION_NAME);
  if (desc) desc_ = desc;

  ops_.SetVolume = pa_context_set_sink_input_volume;
  ops_.Mute = pa_context_set_sink_input_mute;
  ops_.Kill = pa_context_kill_sink_input;
  ops_.Move = pa_context_move_sink_input_by_index;
  ops_.SetDefault = nullptr;
}

Device::Device(const pa_source_output_info* info) :
    type_(DeviceType::SOURCE_OUTPUT),
    index_(info->index),
    name_(info->name ? info->name : ""),
    mute_(info->mute),
    card_idx_(-1) {
  update_volume(info->volume);
  volume_percent_ = volume_as_percent(&volume_);
  balance_ = pa_cvolume_get_balance(&volume_, &channels_) * 100.0;

  const char *desc = pa_proplist_gets(info->proplist,
                                      PA_PROP_APPLICATION_NAME);
  if (desc) desc_ = desc;

  ops_.SetVolume = pa_context_set_source_output_volume;
  ops_.Mute = pa_context_set_source_output_mute;
  ops_.Kill = pa_context_kill_source_output;
  ops_.Move = pa_context_move_source_output_by_index;
  ops_.SetDefault = nullptr;
}

void Device::update_volume(const pa_cvolume& newvol) {
  volume_ = newvol;
  volume_percent_ = volume_as_percent(&volume_);
  balance_ = pa_cvolume_get_balance(&volume_, &channels_) * 100.0;
}

// vim: set et ts=2 sw=2:
