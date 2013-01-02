#pragma once

// C
#include <string.h>

// C++
#include <memory>
#include <string>
#include <vector>

// external
#include <pulse/pulseaudio.h>

using std::string;
using std::vector;
using std::unique_ptr;

enum DeviceType {
  DEVTYPE_SINK,
  DEVTYPE_SOURCE,
  DEVTYPE_SINK_INPUT,
  DEVTYPE_SOURCE_OUTPUT,
};

struct Profile {
  Profile(const pa_card_profile_info& info) :
    name(info.name),
    desc(info.description) {}

  string name;
  string desc;
};

struct Operations {
  pa_operation* (*Mute)(pa_context*, uint32_t, int, pa_context_success_cb_t, void*);
  pa_operation* (*SetVolume)(pa_context*, uint32_t, const pa_cvolume*, pa_context_success_cb_t, void*);
  pa_operation* (*SetDefault)(pa_context*, const char*, pa_context_success_cb_t, void*);
  pa_operation* (*Kill)(pa_context*, uint32_t, pa_context_success_cb_t, void*);
  pa_operation* (*Move)(pa_context *, uint32_t, uint32_t, pa_context_success_cb_t, void *);
};

class Device {
 public:
  Device(const pa_source_info* info);
  Device(const pa_sink_info* info);
  Device(const pa_sink_input_info* info);
  Device(const pa_source_output_info* info);

  uint32_t Index() const { return index_; }
  const string& Name() const { return name_; }
  const string& Desc() const { return desc_; }
  int Volume() const { return volume_percent_; }
  int Balance() const { return balance_; }
  bool Muted() const { return mute_; }
  enum DeviceType Type() const { return type_; }

 private:
  friend class PulseClient;

  void update_volume(const pa_cvolume& newvol);

  enum DeviceType type_;
  uint32_t index_;
  string name_;
  string desc_;
  pa_cvolume volume_;
  int volume_percent_;
  pa_channel_map channels_;
  int mute_;
  int balance_;
  Operations ops_;
};

class Card {
 public:
  Card(const pa_card_info* info);

  const string& Name() const { return name_; }
  uint32_t Index() const { return index_; }
  const string& Driver() const { return driver_; }

  const vector<Profile>& Profiles() const { return profiles_; }
  const Profile& ActiveProfile() const { return active_profile_; }

 private:
  friend class PulseClient;

  uint32_t index_;
  string name_;
  uint32_t owner_module_;
  string driver_;
  vector<Profile> profiles_;
  Profile active_profile_;
};

struct ServerInfo {
  string sink;
  string source;
};

template<typename T>
struct Range {
  Range(T min, T max) : min(min), max(max) {}

  T clamp(T value) {
    return value < min ? min : (value > max ? max : value);
  }

  T min;
  T max;
};

class PulseClient {
 public:
  PulseClient(string client_name);
  ~PulseClient();

  // Populates all known devices and cards. Any currently known
  // devices and cards are cleared before the new data is stored.
  void Populate();

  // Get a sink by index or name, or all sinks.
  Device* GetSink(const uint32_t& index);
  Device* GetSink(const string& name);
  const vector<Device>& GetSinks() const { return sinks_; }

  // Get a source by index or name, or all sources.
  Device* GetSource(const uint32_t& index);
  Device* GetSource(const string& name);
  const vector<Device>& GetSources() const { return sources_; }

  // Get a sink input by index or name, or all sink inputs.
  Device* GetSinkInput(const uint32_t& name);
  Device* GetSinkInput(const string& name);
  const vector<Device>& GetSinkInputs() const { return sink_inputs_; }

  // Get a source output by index or name, or all source outputs.
  Device* GetSourceOutput(const uint32_t& name);
  Device* GetSourceOutput(const string& name);
  const vector<Device>& GetSourceOutputs() const { return source_outputs_; }

  // Get a card by index or name, or all cards.
  Card* GetCard(const uint32_t& index);
  Card* GetCard(const string& name);
  const vector<Card>& GetCards() const { return cards_; }

  // Get or set the volume of a device.
  int GetVolume(const Device& device) const;
  bool SetVolume(Device& device, long value);

  // Convenience wrappers for adjusting volume
  bool IncreaseVolume(Device& device, long increment);
  bool DecreaseVolume(Device& device, long decrement);

  // Get or set the volume of a device. Not all devices support this.
  int GetBalance(const Device& device) const;
  bool SetBalance(Device& device, long value);

  // Convenience wrappers for adjusting balance
  bool IncreaseBalance(Device& device, long increment);
  bool DecreaseBalance(Device& device, long decrement);

  // Get and set mute for a device.
  bool IsMuted(const Device& device) const { return device.mute_; };
  bool SetMute(Device& device, bool mute);

  // Set the profile for a card by name.
  bool SetProfile(Card& card, const string& profile);

  // Move a given source output or sink input to the destination.
  bool Move(Device& source, Device& dest);

  // Kill a source output or sink input.
  bool Kill(Device& device);

  // Get or set the default sink and source.
  const ServerInfo& GetDefaults() const { return defaults_; }
  bool SetDefault(Device& device);

  // Set minimum and maximum allowed volume
  void SetVolumeRange(int min, int max) {
    volume_range_ = { min, max };
  }

  // Set minimum and maximum allowed balance
  void SetBalanceRange(int min, int max) {
    balance_range_ = { min, max };
  }

 private:
  void mainloop_iterate(pa_operation* op);

  void populate_server_info();
  void populate_cards();
  void populate_sinks();
  void populate_sources();

  Device* get_device(vector<Device>& devices, const uint32_t& index);
  Device* get_device(vector<Device>& devices, const string& name);

  void remove_device(Device& device);

  string client_name_;
  pa_context* context_;
  pa_mainloop* mainloop_;
  vector<Device> sinks_;
  vector<Device> sources_;
  vector<Device> sink_inputs_;
  vector<Device> source_outputs_;
  vector<Card> cards_;
  ServerInfo defaults_;
  Range<int> volume_range_;
  Range<int> balance_range_;
};

// vim: set et ts=2 sw=2:
