#pragma once

#include <stdio.h>

#ifdef HAVE_NOTIFY
#include <libnotify/notify.h>
#endif

enum class NotificationType {
  VOLUME,
  BALANCE,
  UNMUTE,
  MUTE,
};

class Notifier {
 public:
  virtual ~Notifier() {}

  virtual void Notify(NotificationType type, long value, bool mute) const = 0;

 protected:
  bool initialized_;
};

class NullNotifier : public Notifier {
 public:
  virtual ~NullNotifier() {}
  virtual void Notify(enum NotificationType, long, bool) const {}
};

class CommandLineNotifier : public Notifier {
 public:
  virtual ~CommandLineNotifier() {}

  virtual void Notify(enum NotificationType type, long value, bool) const {
    switch (type) {
    case NotificationType::VOLUME:
    case NotificationType::BALANCE:
    case NotificationType::UNMUTE:
    case NotificationType::MUTE:
      printf("%ld\n", value);
      break;
    }
  }
};

#ifdef HAVE_NOTIFY
class LibnotifyNotifier : public Notifier {
 public:
  LibnotifyNotifier() {
    notify_init("ponymix");
  }

  virtual ~LibnotifyNotifier() {
    notify_uninit();
  }

  virtual void Notify(enum NotificationType type, long value, bool mute) const {
    switch (type) {
    case NotificationType::BALANCE:
      break;
    case NotificationType::VOLUME:
    case NotificationType::UNMUTE:
    case NotificationType::MUTE:
      volchange(value, mute);
      break;
    }
  }

 private:
  void volchange(long vol, bool mute) const {
    const char* icon = "notification-audio-volume-muted";

    if (!mute) {
      if (vol > 67) {
        icon = "notification-audio-volume-high";
      } else if (vol > 33) {
        icon = "notification-audio-volume-medium";
      } else if (vol > 0) {
        icon = "notification-audio-volume-low";
      }
    }

    NotifyNotification* notification = notify_notification_new("ponymix", "", icon);
    notify_notification_set_timeout(notification, 1000);
    notify_notification_set_urgency(notification, NOTIFY_URGENCY_NORMAL);
    notify_notification_set_hint_int32(notification, "value", vol);
    notify_notification_set_hint_string(notification, "synchronous", "volume");
    notify_notification_show(notification, nullptr);
    g_object_unref(G_OBJECT(notification));
  }
};
#endif

// vim: set et ts=2 sw=2:
