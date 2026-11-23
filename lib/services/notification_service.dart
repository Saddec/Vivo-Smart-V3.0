import 'dart:convert';
import 'dart:io';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:timezone/data/latest_all.dart' as tz;
import 'package:timezone/timezone.dart' as tz;

class NotificationService {
  NotificationService._();
  static final NotificationService instance = NotificationService._();

  final FlutterLocalNotificationsPlugin _notificationsPlugin =
      FlutterLocalNotificationsPlugin();

  bool _isInitialized = false;

  // Channel IDs
  static const String _adhanChannelId = 'adhan_channel';
  static const String _iqamaChannelId = 'iqama_channel';
  static const String _alertsChannelId = 'alerts_channel';

  // Cache Keys
  static const String _cachedPrayersKey = 'cached_prayer_times_for_notif';
  static const String _cachedIqamaKey = 'cached_iqama_config_for_notif';
  static const String _cachedAlertsKey = 'cached_alerts_for_notif';

  Future<void> initialize() async {
    if (_isInitialized) return;

    // Only configure notifications on Android / iOS
    if (!kIsWeb && (Platform.isAndroid || Platform.isIOS)) {
      try {
        // Initialize Timezone database
        tz.initializeTimeZones();
        _configureLocalTimeZone();

        const AndroidInitializationSettings androidSettings =
            AndroidInitializationSettings('@mipmap/launcher_icon');

        const DarwinInitializationSettings iosSettings =
            DarwinInitializationSettings(
          requestAlertPermission: true,
          requestBadgePermission: true,
          requestSoundPermission: true,
        );

        const InitializationSettings initSettings = InitializationSettings(
          android: androidSettings,
          iOS: iosSettings,
        );

        await _notificationsPlugin.initialize(
          settings: initSettings,
          onDidReceiveNotificationResponse: (NotificationResponse response) {
            debugPrint('Notification clicked: ${response.payload}');
          },
        );

        // Create Android Notification Channels
        if (Platform.isAndroid) {
          final androidPlugin = _notificationsPlugin
              .resolvePlatformSpecificImplementation<
                  AndroidFlutterLocalNotificationsPlugin>();

          if (androidPlugin != null) {
            await androidPlugin.createNotificationChannel(
              const AndroidNotificationChannel(
                _adhanChannelId,
                'مواقيت الأذان والصلوات',
                description: 'تنبيهات مميزة عند دخول وقت الصلاة ورفع الأذان',
                importance: Importance.max,
                playSound: true,
                enableVibration: true,
                showBadge: true,
              ),
            );

            await androidPlugin.createNotificationChannel(
              const AndroidNotificationChannel(
                _iqamaChannelId,
                'مواقيت إقامة الصلاة',
                description: 'تنبيهات اقتراب موعد إقامة الصلاة بعد الأذان',
                importance: Importance.high,
                playSound: true,
                enableVibration: true,
                showBadge: true,
              ),
            );

            await androidPlugin.createNotificationChannel(
              const AndroidNotificationChannel(
                _alertsChannelId,
                'تنبيهات النظام والمواعيد',
                description: 'تنبيهات المواعيد والملفات المجدولة',
                importance: Importance.high,
                playSound: true,
                enableVibration: true,
                showBadge: true,
              ),
            );
          }
        }

        _isInitialized = true;
        debugPrint('NotificationService initialized successfully.');

        // Re-schedule cached notifications if available
        await restoreAndScheduleCached();
      } catch (e) {
        debugPrint('Error initializing NotificationService: $e');
      }
    }
  }

  void _configureLocalTimeZone() {
    try {
      final now = DateTime.now();
      final offsetMs = now.timeZoneOffset.inMilliseconds;
      for (final loc in tz.timeZoneDatabase.locations.values) {
        if (loc.currentTimeZone.offset == offsetMs) {
          tz.setLocalLocation(loc);
          return;
        }
      }
    } catch (e) {
      debugPrint('Error matching local timezone: $e');
    }
  }

  Future<void> requestPermissions() async {
    if (kIsWeb) return;
    if (Platform.isAndroid) {
      final androidPlugin = _notificationsPlugin
          .resolvePlatformSpecificImplementation<
              AndroidFlutterLocalNotificationsPlugin>();
      if (androidPlugin != null) {
        final granted = await androidPlugin.requestNotificationsPermission();
        final exactGranted = await androidPlugin.requestExactAlarmsPermission();
        debugPrint('Notification permission: $granted, Exact alarm: $exactGranted');
      }
    } else if (Platform.isIOS) {
      final iosPlugin = _notificationsPlugin
          .resolvePlatformSpecificImplementation<
              IOSFlutterLocalNotificationsPlugin>();
      if (iosPlugin != null) {
        await iosPlugin.requestPermissions(
          alert: true,
          badge: true,
          sound: true,
        );
      }
    }
  }

  // --- Schedule Prayer & Iqama Notifications ---
  Future<void> schedulePrayerNotifications({
    required Map<String, String> prayerTimes,
    Map<String, dynamic>? iqamaConfig,
  }) async {
    if (kIsWeb || (!Platform.isAndroid && !Platform.isIOS)) return;
    if (!_isInitialized) await initialize();
    if (prayerTimes.isEmpty) return;

    // Cache to SharedPreferences for offline resilience
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_cachedPrayersKey, jsonEncode(prayerTimes));
      if (iqamaConfig != null) {
        await prefs.setString(_cachedIqamaKey, jsonEncode(iqamaConfig));
      }
    } catch (_) {}

    final prayerOrder = [
      {'key': 'الفجر', 'name': 'صلاة الفجر', 'index': 0},
      {'key': 'الظهر', 'name': 'صلاة الظهر', 'index': 1},
      {'key': 'العصر', 'name': 'صلاة العصر', 'index': 2},
      {'key': 'المغرب', 'name': 'صلاة المغرب', 'index': 3},
      {'key': 'العشاء', 'name': 'صلاة العشاء', 'index': 4},
    ];

    final now = DateTime.now();

    for (final prayer in prayerOrder) {
      final key = prayer['key'] as String;
      final name = prayer['name'] as String;
      final pIndex = prayer['index'] as int;

      final timeStr = prayerTimes[key];
      if (timeStr == null || !timeStr.contains(':')) continue;

      final parts = timeStr.split(':');
      final hour = int.tryParse(parts[0]) ?? 0;
      final minute = int.tryParse(parts[1]) ?? 0;

      // Check iqama delay & enable status
      final iqamaEn = iqamaConfig != null &&
          (iqamaConfig['iqama_en_$pIndex'] == 1 ||
              iqamaConfig['iqama_en_$pIndex'] == '1' ||
              iqamaConfig['iqama_en_$pIndex'] == true);
      final iqamaDelay = iqamaConfig != null
          ? (int.tryParse(iqamaConfig['iqama_del_$pIndex']?.toString() ?? '10') ?? 10)
          : 10;

      // Schedule for Today and Tomorrow
      for (int dayOffset = 0; dayOffset <= 1; dayOffset++) {
        final targetDate = now.add(Duration(days: dayOffset));
        final prayerDateTime =
            DateTime(targetDate.year, targetDate.month, targetDate.day, hour, minute);

        final adhanNotifId = 100 + (dayOffset * 10) + pIndex;
        final iqamaNotifId = 200 + (dayOffset * 10) + pIndex;

        // Cancel previous if scheduled
        await _notificationsPlugin.cancel(id: adhanNotifId);
        await _notificationsPlugin.cancel(id: iqamaNotifId);

        // Schedule Adhan
        if (prayerDateTime.isAfter(now)) {
          final tzScheduled = tz.TZDateTime.from(prayerDateTime, tz.local);
          await _scheduleNotification(
            id: adhanNotifId,
            channelId: _adhanChannelId,
            channelName: 'مواقيت الأذان والصلوات',
            title: '🕌 حان الآن موعد أذان $name',
            body: 'الله أكبر، الله أكبر — حان الآن موعد رفع الأذان حسب التوقيت المحلي ($timeStr).',
            scheduledDate: tzScheduled,
          );
        }

        // Schedule Iqama if enabled
        if (iqamaEn) {
          final iqamaDateTime = prayerDateTime.add(Duration(minutes: iqamaDelay));
          if (iqamaDateTime.isAfter(now)) {
            final tzIqama = tz.TZDateTime.from(iqamaDateTime, tz.local);
            final iqamaTimeStr =
                '${iqamaDateTime.hour.toString().padLeft(2, '0')}:${iqamaDateTime.minute.toString().padLeft(2, '0')}';
            await _scheduleNotification(
              id: iqamaNotifId,
              channelId: _iqamaChannelId,
              channelName: 'مواقيت إقامة الصلاة',
              title: '⏱️ حان الآن موعد إقامة $name',
              body: 'قد قامت الصلاة — موعد الإقامة الآن ($iqamaTimeStr) بعد انقضاء $iqamaDelay دقائق من الأذان.',
              scheduledDate: tzIqama,
            );
          }
        }
      }
    }
  }

  // --- Schedule Custom Alerts ---
  Future<void> scheduleCustomAlerts(List<dynamic> alerts) async {
    if (kIsWeb || (!Platform.isAndroid && !Platform.isIOS)) return;
    if (!_isInitialized) await initialize();

    // Cache alerts for offline resilience
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_cachedAlertsKey, jsonEncode(alerts));
    } catch (_) {}

    final now = DateTime.now();

    // Clear old scheduled alert notifications in range 1000..1999
    for (int i = 0; i < 50; i++) {
      for (int d = 0; d < 7; d++) {
        await _notificationsPlugin.cancel(id: 1000 + (i * 10) + d);
      }
    }

    for (int i = 0; i < alerts.length; i++) {
      final alert = alerts[i];
      if (alert is! Map) continue;

      final enabled = alert['enabled'] == true ||
          alert['enabled'] == 1 ||
          alert['enabled'] == '1';
      if (!enabled) continue;

      final name = alert['name']?.toString() ?? 'تنبيه مجدول';
      final timeStr = alert['time']?.toString() ?? '';
      if (!timeStr.contains(':')) continue;

      final parts = timeStr.split(':');
      final hour = int.tryParse(parts[0]) ?? 0;
      final minute = int.tryParse(parts[1]) ?? 0;

      // Days of week: list of ints (0: Sunday .. 6: Saturday or 1: Monday .. 7: Sunday)
      // Usually ESP format uses days array e.g. [0,1,2,3,4,5,6] or days bitmask
      List<int> activeDays = [];
      if (alert['days'] is List) {
        activeDays = (alert['days'] as List).map((e) => int.tryParse(e.toString()) ?? 0).toList();
      } else {
        // If no days specified or daily, repeat all days
        activeDays = [0, 1, 2, 3, 4, 5, 6];
      }

      // Schedule for upcoming 7 days
      for (int dayOffset = 0; dayOffset < 7; dayOffset++) {
        final targetDate = now.add(Duration(days: dayOffset));
        // Match day of week (DateTime: 1 = Monday, 7 = Sunday; convert to 0 = Sunday)
        final weekdayZeroBased = targetDate.weekday % 7; // Sunday = 0, Monday = 1...
        if (!activeDays.contains(weekdayZeroBased) && !activeDays.contains(targetDate.weekday)) {
          continue;
        }

        final alertDateTime =
            DateTime(targetDate.year, targetDate.month, targetDate.day, hour, minute);

        if (alertDateTime.isAfter(now)) {
          final tzAlert = tz.TZDateTime.from(alertDateTime, tz.local);
          final notifId = 1000 + (i * 10) + (dayOffset % 10);

          await _scheduleNotification(
            id: notifId,
            channelId: _alertsChannelId,
            channelName: 'تنبيهات النظام والمواعيد',
            title: '⏰ تنبيه مجدول: $name',
            body: 'حان الآن موعد التنبيه المجدول في تمام الساعة $timeStr.',
            scheduledDate: tzAlert,
          );
        }
      }
    }
  }

  Future<void> _scheduleNotification({
    required int id,
    required String channelId,
    required String channelName,
    required String title,
    required String body,
    required tz.TZDateTime scheduledDate,
  }) async {
    try {
      final androidDetails = AndroidNotificationDetails(
        channelId,
        channelName,
        channelDescription: 'تنبيهات تلقائية مجدولة من تطبيق VivoSmart',
        importance: Importance.max,
        priority: Priority.high,
        ticker: title,
        styleInformation: BigTextStyleInformation(
          body,
          contentTitle: title,
          summaryText: 'VivoSmart',
        ),
        icon: '@mipmap/launcher_icon',
        color: const Color(0xFF00F2FE),
        category: AndroidNotificationCategory.alarm,
        visibility: NotificationVisibility.public,
        playSound: true,
        enableVibration: true,
        vibrationPattern: Int64List.fromList([0, 500, 250, 500]),
      );

      final notifDetails = NotificationDetails(
        android: androidDetails,
        iOS: const DarwinNotificationDetails(
          presentAlert: true,
          presentBadge: true,
          presentSound: true,
        ),
      );

      await _notificationsPlugin.zonedSchedule(
        id: id,
        title: title,
        body: body,
        scheduledDate: scheduledDate,
        notificationDetails: notifDetails,
        androidScheduleMode: AndroidScheduleMode.exactAllowWhileIdle,
      );
    } catch (e) {
      debugPrint('Error scheduling notification $id: $e');
    }
  }

  // --- Restore and schedule from offline cache ---
  Future<void> restoreAndScheduleCached() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final cachedPrayersStr = prefs.getString(_cachedPrayersKey);
      if (cachedPrayersStr != null) {
        final Map<String, dynamic> rawMap = jsonDecode(cachedPrayersStr);
        final prayerTimes = rawMap.map((k, v) => MapEntry(k, v.toString()));

        Map<String, dynamic>? iqamaConfig;
        final cachedIqamaStr = prefs.getString(_cachedIqamaKey);
        if (cachedIqamaStr != null) {
          iqamaConfig = jsonDecode(cachedIqamaStr);
        }

        await schedulePrayerNotifications(
          prayerTimes: prayerTimes,
          iqamaConfig: iqamaConfig,
        );
      }

      final cachedAlertsStr = prefs.getString(_cachedAlertsKey);
      if (cachedAlertsStr != null) {
        final List<dynamic> alerts = jsonDecode(cachedAlertsStr);
        await scheduleCustomAlerts(alerts);
      }
    } catch (e) {
      debugPrint('Error restoring cached notifications: $e');
    }
  }

  Future<void> cancelAll() async {
    await _notificationsPlugin.cancelAll();
  }
}
