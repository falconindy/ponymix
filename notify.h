#pragma once

#include <stdio.h>

enum NotificationType {
  NOTIFY_VOLUME,
  NOTIFY_BALANCE,
  NOTIFY_UNMUTE,
  NOTIFY_MUTE,
};

class Notifier {
 public:
  virtual ~Notifier() {}

  virtual void Notify(enum NotificationType type, long value, bool mute) = 0;

 protected:
  bool initialized_;
};

class CommandLineNotifier : public Notifier {
 public:
  virtual ~CommandLineNotifier() {}

  virtual void Notify(enum NotificationType type, long value, bool) {
    switch (type) {
    case NOTIFY_VOLUME:
    case NOTIFY_BALANCE:
    case NOTIFY_UNMUTE:
    case NOTIFY_MUTE:
      printf("%ld\n", value);
      break;
    }
  }
};

// vim: set et ts=2 sw=2:
