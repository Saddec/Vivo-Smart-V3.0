import 'dart:async';
import 'dart:convert';
import 'package:dio/dio.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../services/api_service.dart';
import '../services/notification_service.dart';

// Provider for ApiService
final apiServiceProvider = Provider<ApiService>((ref) {
  final apiService = ApiService();
  
  // Bind unauthorized event to auto logout
  apiService.onUnauthorized = () {
    ref.read(connectionStateProvider.notifier).handleSessionExpired();
  };
  
  return apiService;
});

// Model to represent connection and login state
class ConnectionStateModel {
  final String? ip;
  final bool isConnecting;
  final bool isConnected;
  final bool isLoggedIn;
  final String? error;

  ConnectionStateModel({
    this.ip,
    this.isConnecting = false,
    this.isConnected = false,
    this.isLoggedIn = false,
    this.error,
  });

  ConnectionStateModel copyWith({
    String? ip,
    bool? isConnecting,
    bool? isConnected,
    bool? isLoggedIn,
    String? error,
  }) {
    return ConnectionStateModel(
      ip: ip ?? this.ip,
      isConnecting: isConnecting ?? this.isConnecting,
      isConnected: isConnected ?? this.isConnected,
      isLoggedIn: isLoggedIn ?? this.isLoggedIn,
      error: error, // If error is not passed, it resets to null
    );
  }
}

// Notifier for ConnectionState
class ConnectionStateNotifier extends StateNotifier<ConnectionStateModel> {
  final Ref _ref;

  ConnectionStateNotifier(this._ref) : super(ConnectionStateModel()) {
    _loadSavedSession();
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  // Load saved session at app startup
  Future<void> _loadSavedSession() async {
    state = state.copyWith(isConnecting: true);
    await _api.init();
    
    if (_api.ipAddress != null && _api.sessionToken != null) {
      // Verify connection
      final isAlive = await _api.checkConnection(_api.ipAddress!);
      if (isAlive) {
        state = ConnectionStateModel(
          ip: _api.ipAddress,
          isConnected: true,
          isLoggedIn: true,
        );
        // Start polling
        _ref.read(statusPollingProvider.notifier).startPolling();
      } else {
        // Device offline, but keep credentials or prompt login
        state = ConnectionStateModel(
          ip: _api.ipAddress,
          isConnected: false,
          isLoggedIn: true, // we have a token, but offline
          error: 'الجهاز غير متصل بالشبكة الحالية',
        );
      }
    } else {
      state = ConnectionStateModel();
    }
  }

  // Check connection to an IP address without logging in
  Future<bool> connectToDevice(String ip) async {
    state = state.copyWith(isConnecting: true, error: null);
    final success = await _api.checkConnection(ip);
    
    if (success) {
      state = state.copyWith(
        ip: ip,
        isConnected: true,
        isConnecting: false,
      );
      return true;
    } else {
      state = state.copyWith(
        isConnecting: false,
        isConnected: false,
        error: 'تعذر الاتصال بالجهاز، تأكد من عنوان IP والشبكة',
      );
      return false;
    }
  }

  // Attempt login
  Future<bool> login(String password) async {
    final ip = state.ip;
    if (ip == null) {
      state = state.copyWith(error: 'الرجاء الاتصال بالجهاز أولاً');
      return false;
    }

    state = state.copyWith(isConnecting: true, error: null);
    final result = await _api.login(ip, password);

    if (result['ok'] == true) {
      state = state.copyWith(
        isConnected: true,
        isLoggedIn: true,
        isConnecting: false,
      );
      // Save device connection history
      await _ref.read(deviceNicknamesProvider.notifier).saveDeviceConnection(ip);
      final currentName = _ref.read(deviceNicknamesProvider)[ip] ?? ip;
      await _ref.read(savedDevicesProvider.notifier).saveOrUpdateDevice(ip, currentName, password);
      // Start polling status
      _ref.read(statusPollingProvider.notifier).startPolling();
      return true;
    } else {
      state = state.copyWith(
        isConnecting: false,
        error: result['error'] ?? 'فشل تسجيل الدخول',
      );
      return false;
    }
  }

  // Switch to another saved device
  Future<bool> switchToDevice(String newIp, String password) async {
    state = state.copyWith(isConnecting: true, error: null);
    _ref.read(statusPollingProvider.notifier).stopPolling();
    await _api.logout();

    final connected = await connectToDevice(newIp);
    if (!connected) {
      state = state.copyWith(isConnecting: false, error: 'تعذر الاتصال بـ $newIp');
      return false;
    }

    final loggedIn = await login(password);
    return loggedIn;
  }

  // Handle session expiration (called on 401)
  void handleSessionExpired() {
    _ref.read(statusPollingProvider.notifier).stopPolling();
    _api.setSessionToken(null);
    state = ConnectionStateModel(
      ip: state.ip,
      isConnected: state.isConnected,
      isLoggedIn: false,
      error: 'انتهت صلاحية الجلسة، يرجى تسجيل الدخول مجدداً',
    );
  }

  // Perform logout
  Future<void> logout() async {
    _ref.read(statusPollingProvider.notifier).stopPolling();
    await _api.logout();
    state = ConnectionStateModel();
  }
}

final connectionStateProvider =
    StateNotifierProvider<ConnectionStateNotifier, ConnectionStateModel>((ref) {
  return ConnectionStateNotifier(ref);
});

// Model for Device Status
class DeviceStatusModel {
  final bool isPlaying;
  final String currentFile;
  final int volume;
  final String statusText;
  final bool isWifiConnected;
  final String deviceIp;
  final bool isAdhan;
  final bool isEidMode;
  
  final String gregorianDate;
  final String hijriDate;
  final String currentClockTime;
  
  final Map<String, String> prayerTimes;
  final String prayerSource;
  final String? nextPrayerName;
  final String? nextPrayerTime;
  final Duration? timeToNextPrayer;
  
  final String? error;

  DeviceStatusModel({
    this.isPlaying = false,
    this.currentFile = '',
    this.volume = 15,
    this.statusText = 'مجهول',
    this.isWifiConnected = false,
    this.deviceIp = '',
    this.isAdhan = false,
    this.isEidMode = false,
    this.gregorianDate = '',
    this.hijriDate = '',
    this.currentClockTime = '--:--',
    this.prayerTimes = const {},
    this.prayerSource = 'غير معروف',
    this.nextPrayerName,
    this.nextPrayerTime,
    this.timeToNextPrayer,
    this.error,
  });
}

// Notifier for Polling Device Status
class StatusPollingNotifier extends StateNotifier<DeviceStatusModel> {
  final Ref _ref;
  Timer? _pollingTimer;
  Timer? _countdownTimer;
  DateTime? _lastAutoTimeSync;

  StatusPollingNotifier(this._ref) : super(DeviceStatusModel());

  ApiService get _api => _ref.read(apiServiceProvider);

  bool _isPaused = false;

  void pausePolling() {
    _isPaused = true;
  }

  void resumePolling() {
    _isPaused = false;
    pollStatus();
  }

  void startPolling() {
    _isPaused = false;
    _pollingTimer?.cancel();
    // Poll immediately
    pollStatus();
    
    // Poll status every 3 seconds
    _pollingTimer = Timer.periodic(const Duration(seconds: 3), (timer) {
      pollStatus();
    });

    // Run countdown timer every second
    _countdownTimer?.cancel();
    _countdownTimer = Timer.periodic(const Duration(seconds: 1), (timer) {
      _updateNextPrayerCountdown();
    });
  }

  void stopPolling() {
    _isPaused = false;
    _pollingTimer?.cancel();
    _countdownTimer?.cancel();
    state = DeviceStatusModel();
  }

  Future<void> pollStatus() async {
    if (_isPaused) return;
    final statusFuture = _api.getStatus(isUserActive: true);
    final clockFuture = _api.getClock();
    final prayersFuture = _api.getPrayerTimes();

    try {
      final results = await Future.wait([statusFuture, clockFuture, prayersFuture]);
      
      final status = results[0];
      final clock = results[1];
      final prayers = results[2];

      if (status['ok'] == false && status['error'] != null) {
        // Connection issue
        return;
      }

      // Auto-sync time if ESP clock is reset to 1970 or lost
      final greg = clock['greg']?.toString() ?? '';
      if (greg.startsWith('1970')) {
        final now = DateTime.now();
        if (_lastAutoTimeSync == null || now.difference(_lastAutoTimeSync!).inSeconds > 20) {
          _lastAutoTimeSync = now;
          _api.syncBrowserTime();
        }
      }

      // Parse prayer times
      final prayerTimesMap = <String, String>{};
      if (prayers.containsKey('fajr')) prayerTimesMap['الفجر'] = prayers['fajr'] ?? '';
      if (prayers.containsKey('dhuhr')) prayerTimesMap['الظهر'] = prayers['dhuhr'] ?? '';
      if (prayers.containsKey('asr')) prayerTimesMap['العصر'] = prayers['asr'] ?? '';
      if (prayers.containsKey('maghrib')) prayerTimesMap['المغرب'] = prayers['maghrib'] ?? '';
      if (prayers.containsKey('isha')) prayerTimesMap['العشاء'] = prayers['isha'] ?? '';
      if (prayers.containsKey('shorooq')) prayerTimesMap['الشروق'] = prayers['shorooq'] ?? '';

      // Determine next prayer
      String? nextName;
      String? nextTime;
      if (prayerTimesMap.isNotEmpty && clock.containsKey('time')) {
        final next = _calculateNextPrayer(prayerTimesMap, clock['time']);
        nextName = next['name'];
        nextTime = next['time'];
      }

      state = DeviceStatusModel(
        isPlaying: status['playing'] ?? false,
        currentFile: status['file'] ?? '',
        volume: status['volume'] ?? 15,
        statusText: status['status_text'] ?? 'متوقف',
        isWifiConnected: status['wifi'] ?? false,
        deviceIp: status['ip'] ?? '',
        isAdhan: status['adhan'] ?? false,
        isEidMode: status['eidMode'] ?? false,
        gregorianDate: clock['greg'] ?? '',
        hijriDate: clock['hijri'] ?? '',
        currentClockTime: clock['time'] ?? '--:--',
        prayerTimes: prayerTimesMap,
        prayerSource: prayers['source'] ?? 'غير معروف',
        nextPrayerName: nextName,
        nextPrayerTime: nextTime,
      );
      
      _updateNextPrayerCountdown();

      if (prayerTimesMap.isNotEmpty) {
        final adhanConfig = _ref.read(adhanConfigProvider).valueOrNull;
        NotificationService.instance.schedulePrayerNotifications(
          prayerTimes: prayerTimesMap,
          iqamaConfig: adhanConfig,
        );
      }
    } catch (e) {
      debugPrint('Error during polling: $e');
    }
  }

  void _updateNextPrayerCountdown() {
    if (state.nextPrayerTime == null || state.currentClockTime == '--:--') return;

    try {
      final now = DateTime.now();
      final timeParts = state.nextPrayerTime!.split(':');
      final pH = int.parse(timeParts[0]);
      final pM = int.parse(timeParts[1]);

      var prayerTime = DateTime(now.year, now.month, now.day, pH, pM);
      
      // If prayer is already past today, it must be tomorrow (for Isha to Fajr transition)
      if (prayerTime.isBefore(now)) {
        // But wait! Since our device updates clock from system or NTP, let's compare simple times
        final curParts = state.currentClockTime.split(':');
        final cH = int.parse(curParts[0]);
        final cM = int.parse(curParts[1]);
        if (cH > pH || (cH == pH && cM > pM)) {
          prayerTime = prayerTime.add(const Duration(days: 1));
        }
      }

      final diff = prayerTime.difference(now);
      
      state = DeviceStatusModel(
        isPlaying: state.isPlaying,
        currentFile: state.currentFile,
        volume: state.volume,
        statusText: state.statusText,
        isWifiConnected: state.isWifiConnected,
        deviceIp: state.deviceIp,
        isAdhan: state.isAdhan,
        isEidMode: state.isEidMode,
        gregorianDate: state.gregorianDate,
        hijriDate: state.hijriDate,
        currentClockTime: state.currentClockTime,
        prayerTimes: state.prayerTimes,
        prayerSource: state.prayerSource,
        nextPrayerName: state.nextPrayerName,
        nextPrayerTime: state.nextPrayerTime,
        timeToNextPrayer: diff.isNegative ? Duration.zero : diff,
      );
    } catch (e) {
      // Ignore parsing errors
    }
  }

  // Utility to calculate the next prayer time
  Map<String, String> _calculateNextPrayer(Map<String, String> pTimes, String currentClock) {
    try {
      final cParts = currentClock.split(':');
      final cMin = int.parse(cParts[0]) * 60 + int.parse(cParts[1]);

      final pList = <_PrayerItem>[];
      final keys = ['الفجر', 'الظهر', 'العصر', 'المغرب', 'العشاء'];
      for (final key in keys) {
        if (pTimes.containsKey(key) && pTimes[key]!.contains(':')) {
          final tParts = pTimes[key]!.split(':');
          final min = int.parse(tParts[0]) * 60 + int.parse(tParts[1]);
          pList.add(_PrayerItem(key, min, pTimes[key]!));
        }
      }

      if (pList.isEmpty) return {};

      // Sort prayers chronologically
      pList.sort((a, b) => a.minutes.compareTo(b.minutes));

      // Find first prayer that is later than current time
      for (final p in pList) {
        if (p.minutes > cMin) {
          return {'name': p.name, 'time': p.timeStr};
        }
      }

      // If no prayer is later today, the next prayer is tomorrow's Fajr (first sorted item)
      return {'name': pList.first.name, 'time': pList.first.timeStr};
    } catch (e) {
      return {};
    }
  }

  @override
  void dispose() {
    stopPolling();
    super.dispose();
  }
}

class _PrayerItem {
  final String name;
  final int minutes;
  final String timeStr;
  _PrayerItem(this.name, this.minutes, this.timeStr);
}

final statusPollingProvider =
    StateNotifierProvider<StatusPollingNotifier, DeviceStatusModel>((ref) {
  return StatusPollingNotifier(ref);
});

// --- Files List Provider ---
class FilesStateNotifier extends StateNotifier<AsyncValue<Map<String, dynamic>>> {
  final Ref _ref;
  FilesStateNotifier(this._ref) : super(const AsyncValue.loading()) {
    // Note: Do not load immediately if connection state shows offline,
    // but connection notifier starts polling which starts this on dashboard
    refresh();
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  Future<void> refresh() async {
    try {
      final res = await _api.getFilesList();
      if (mounted) {
        state = AsyncValue.data(res);
      }
    } catch (e, stack) {
      if (mounted) {
        state = AsyncValue.error(e, stack);
      }
    }
  }

  Future<bool> deleteFile(String path) async {
    final success = await _api.deleteFile(path);
    if (success) {
      await refresh();
    }
    return success;
  }

  Future<bool> createDirectory(String name) async {
    final success = await _api.createDirectory(name);
    if (success) {
      await refresh();
    }
    return success;
  }

  Future<bool> renameFile(String oldPath, String newPath) async {
    final success = await _api.renameFile(oldPath, newPath);
    if (success) {
      await refresh();
    }
    return success;
  }

  Future<bool> uploadFile({
    List<int>? bytes,
    String? filePath,
    required String filename,
    required String folder,
    void Function(int count, int total, double speedBps, String statusText)? onSendProgress,
    bool Function()? isCancelled,
  }) async {
    _ref.read(statusPollingProvider.notifier).pausePolling();
    try {
      final success = await _api.uploadFile(
        bytes: bytes,
        filePath: filePath,
        filename: filename,
        folder: folder,
        onSendProgress: onSendProgress,
        isCancelled: isCancelled,
      );
      if (success) {
        await refresh();
      }
      return success;
    } finally {
      _ref.read(statusPollingProvider.notifier).resumePolling();
    }
  }

  Future<bool> playFile(String path) async {
    final success = await _api.playDirectFile(path);
    if (success) {
      _ref.read(statusPollingProvider.notifier).pollStatus();
    }
    return success;
  }
}

final filesProvider = StateNotifierProvider<FilesStateNotifier, AsyncValue<Map<String, dynamic>>>((ref) {
  return FilesStateNotifier(ref);
});

// --- Alerts / Schedules Provider ---
class AlertsStateNotifier extends StateNotifier<AsyncValue<List<dynamic>>> {
  final Ref _ref;
  AlertsStateNotifier(this._ref) : super(const AsyncValue.loading()) {
    refresh();
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  Future<void> refresh() async {
    try {
      final res = await _api.getAlertsList();
      if (mounted) {
        state = AsyncValue.data(res);
        NotificationService.instance.scheduleCustomAlerts(res);
      }
    } catch (e, stack) {
      if (mounted) {
        state = AsyncValue.error(e, stack);
      }
    }
  }

  Future<bool> deleteAlert(int index) async {
    final success = await _api.deleteAlert(index);
    if (success) {
      await refresh();
    }
    return success;
  }

  Future<bool> toggleAlert(int index, bool enabled) async {
    final success = await _api.toggleAlert(index, enabled);
    if (success) {
      await refresh();
    }
    return success;
  }

  Future<bool> saveAlert(Map<String, dynamic> alert, int index) async {
    final success = await _api.addOrEditAlert(alert, index);
    if (success) {
      await refresh();
    }
    return success;
  }
}

final alertsProvider = StateNotifierProvider<AlertsStateNotifier, AsyncValue<List<dynamic>>>((ref) {
  return AlertsStateNotifier(ref);
});

// --- GPIO Mappings & Schedules Provider ---
class GpioStateModel {
  final Map<String, dynamic> mappings;
  final List<dynamic> schedules;
  GpioStateModel({this.mappings = const {}, this.schedules = const []});
}

class GpioStateNotifier extends StateNotifier<AsyncValue<GpioStateModel>> {
  final Ref _ref;
  GpioStateNotifier(this._ref) : super(const AsyncValue.loading()) {
    refresh();
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  Future<void> refresh() async {
    try {
      final mappings = await _api.getGpioMappings();
      final schedules = await _api.getGpioSchedules();
      if (mounted) {
        state = AsyncValue.data(GpioStateModel(mappings: mappings, schedules: schedules));
      }
    } catch (e, stack) {
      if (mounted) {
        state = AsyncValue.error(e, stack);
      }
    }
  }

  Future<bool> addInputMapping(Map<String, dynamic> mapping) async {
    final success = await _api.addInputMapping(mapping);
    if (success) {
      await refresh();
    }
    return success;
  }

  Future<bool> deleteInputMapping(int pin) async {
    final success = await _api.deleteInputMapping(pin);
    if (success) {
      await refresh();
    }
    return success;
  }

  Future<bool> addOutputMapping(int pin, String alertFile, int durationSec) async {
    final success = await _api.addOutputMapping(pin, alertFile, durationSec);
    if (success) {
      await refresh();
    }
    return success;
  }

  Future<bool> addGpioSchedule(Map<String, dynamic> entry, int index) async {
    final success = await _api.addGpioSchedule(entry, index);
    if (success) {
      await refresh();
    }
    return success;
  }

  Future<bool> deleteGpioSchedule(int index) async {
    final success = await _api.deleteGpioSchedule(index);
    if (success) {
      await refresh();
    }
    return success;
  }
}

final gpioProvider = StateNotifierProvider<GpioStateNotifier, AsyncValue<GpioStateModel>>((ref) {
  return GpioStateNotifier(ref);
});

// --- Wi-Fi State ---
class WifiStateModel {
  final List<dynamic> networks;
  final Map<String, dynamic> status;
  final bool isScanning;
  final bool isConnecting;
  final String? error;

  WifiStateModel({
    this.networks = const [],
    this.status = const {},
    this.isScanning = false,
    this.isConnecting = false,
    this.error,
  });

  WifiStateModel copyWith({
    List<dynamic>? networks,
    Map<String, dynamic>? status,
    bool? isScanning,
    bool? isConnecting,
    String? error,
  }) {
    return WifiStateModel(
      networks: networks ?? this.networks,
      status: status ?? this.status,
      isScanning: isScanning ?? this.isScanning,
      isConnecting: isConnecting ?? this.isConnecting,
      error: error,
    );
  }
}

class WifiStateNotifier extends StateNotifier<WifiStateModel> {
  final Ref _ref;

  WifiStateNotifier(this._ref) : super(WifiStateModel()) {
    refreshStatus();
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  Future<void> refreshStatus() async {
    final status = await _api.getWifiStatus();
    state = state.copyWith(status: status);
  }

  Future<void> scan() async {
    state = state.copyWith(isScanning: true, error: null);
    try {
      final networks = await _api.scanWifi();
      state = state.copyWith(networks: networks, isScanning: false);
    } catch (e) {
      state = state.copyWith(isScanning: false, error: 'فشل البحث عن شبكات: $e');
    }
  }

  Future<bool> saveAndConnect(String ssid, String password) async {
    state = state.copyWith(isConnecting: true, error: null);
    try {
      final saveSuccess = await _api.saveWifiConfig(ssid, password);
      if (!saveSuccess) {
        state = state.copyWith(isConnecting: false, error: 'فشل حفظ إعدادات الشبكة');
        return false;
      }
      final connectSuccess = await _api.connectWifi();
      if (!connectSuccess) {
        state = state.copyWith(isConnecting: false, error: 'فشل إرسال أمر الاتصال');
        return false;
      }
      await Future.delayed(const Duration(seconds: 3));
      await refreshStatus();
      state = state.copyWith(isConnecting: false);
      return true;
    } catch (e) {
      state = state.copyWith(isConnecting: false, error: 'حدث خطأ: $e');
      return false;
    }
  }
}

final wifiProvider = StateNotifierProvider<WifiStateNotifier, WifiStateModel>((ref) {
  return WifiStateNotifier(ref);
});

// --- System Logs State ---
class LogsStateNotifier extends StateNotifier<AsyncValue<List<dynamic>>> {
  final Ref _ref;
  LogsStateNotifier(this._ref) : super(const AsyncValue.loading()) {
    refresh();
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  Future<void> refresh({int limit = 200, String level = ''}) async {
    try {
      final logs = await _api.getSystemLogs(limit: limit, level: level);
      if (mounted) {
        state = AsyncValue.data(logs);
      }
    } catch (e, stack) {
      if (mounted) {
        state = AsyncValue.error(e, stack);
      }
    }
  }

  Future<bool> clear(String password) async {
    final success = await _api.clearSystemLogs(password);
    if (success) {
      await refresh();
    }
    return success;
  }
}

final logsProvider = StateNotifierProvider<LogsStateNotifier, AsyncValue<List<dynamic>>>((ref) {
  return LogsStateNotifier(ref);
});

// --- Adhan & Iqama Config State ---
class AdhanConfigNotifier extends StateNotifier<AsyncValue<Map<String, dynamic>>> {
  final Ref _ref;
  AdhanConfigNotifier(this._ref) : super(const AsyncValue.loading()) {
    refresh();
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  Future<void> refresh() async {
    try {
      final config = await _api.getAdhanConfig();
      if (mounted) {
        state = AsyncValue.data(config);
        final currentPrayerTimes = _ref.read(statusPollingProvider).prayerTimes;
        if (currentPrayerTimes.isNotEmpty) {
          NotificationService.instance.schedulePrayerNotifications(
            prayerTimes: currentPrayerTimes,
            iqamaConfig: config,
          );
        }
      }
    } catch (e, stack) {
      if (mounted) {
        state = AsyncValue.error(e, stack);
      }
    }
  }

  Future<bool> save(Map<String, String> config) async {
    final success = await _api.saveAdhanConfig(config);
    if (success) {
      await refresh();
    }
    return success;
  }
}

final adhanConfigProvider = StateNotifierProvider<AdhanConfigNotifier, AsyncValue<Map<String, dynamic>>>((ref) {
  return AdhanConfigNotifier(ref);
});

// --- Quick Player Provider ---
class QuickPlayerState {
  final int duration;
  final int position;
  final int playbackState; // 0: stopped, 1: playing, 2: paused
  final bool isAdhan;
  final List<String> playlist;
  final int volume;
  final bool respectAdhan;
  final bool isLoading;

  QuickPlayerState({
    this.duration = 0,
    this.position = 0,
    this.playbackState = 0,
    this.isAdhan = false,
    this.playlist = const [],
    this.volume = 15,
    this.respectAdhan = true,
    this.isLoading = false,
  });

  QuickPlayerState copyWith({
    int? duration,
    int? position,
    int? playbackState,
    bool? isAdhan,
    List<String>? playlist,
    int? volume,
    bool? respectAdhan,
    bool? isLoading,
  }) {
    return QuickPlayerState(
      duration: duration ?? this.duration,
      position: position ?? this.position,
      playbackState: playbackState ?? this.playbackState,
      isAdhan: isAdhan ?? this.isAdhan,
      playlist: playlist ?? this.playlist,
      volume: volume ?? this.volume,
      respectAdhan: respectAdhan ?? this.respectAdhan,
      isLoading: isLoading ?? this.isLoading,
    );
  }
}

class QuickPlayerNotifier extends StateNotifier<QuickPlayerState> {
  final Ref _ref;
  Timer? _trackInfoTimer;

  QuickPlayerNotifier(this._ref) : super(QuickPlayerState()) {
    // Listen to device status updates to start/stop polling
    _ref.listen<DeviceStatusModel>(statusPollingProvider, (previous, next) {
      if (next.isPlaying) {
        startPolling();
      } else {
        stopPolling();
      }
    });
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  void startPolling() {
    if (_trackInfoTimer != null) return;
    _trackInfoTimer = Timer.periodic(const Duration(seconds: 2), (timer) {
      updateTrackInfo();
    });
    updateTrackInfo();
  }

  void stopPolling() {
    _trackInfoTimer?.cancel();
    _trackInfoTimer = null;
    state = state.copyWith(duration: 0, position: 0, playbackState: 0);
  }

  Future<void> updateTrackInfo() async {
    try {
      final info = await _api.getTrackInfo();
      if (mounted) {
        state = state.copyWith(
          duration: info['duration'] ?? 0,
          position: info['position'] ?? 0,
          playbackState: info['state'] ?? 0,
          isAdhan: info['adhan'] ?? false,
        );
      }
    } catch (e) {
      debugPrint('Error updating track info: $e');
    }
  }

  void addToPlaylist(String file) {
    state = state.copyWith(playlist: [...state.playlist, file]);
  }

  void removeFromPlaylist(int index) {
    final list = List<String>.from(state.playlist);
    if (index >= 0 && index < list.length) {
      list.removeAt(index);
      state = state.copyWith(playlist: list);
    }
  }

  void clearPlaylist() {
    state = state.copyWith(playlist: const []);
  }

  void setVolume(int vol) {
    state = state.copyWith(volume: vol);
  }

  void setRespectAdhan(bool val) {
    state = state.copyWith(respectAdhan: val);
  }

  Future<bool> play(String file) async {
    state = state.copyWith(isLoading: true);
    final success = await _api.playDirectFile(file);
    state = state.copyWith(isLoading: false);
    if (success) {
      _ref.read(statusPollingProvider.notifier).pollStatus();
      startPolling();
    }
    return success;
  }

  Future<bool> playQueue() async {
    if (state.playlist.isEmpty) return false;
    state = state.copyWith(isLoading: true);
    final filesStr = state.playlist.join(',');
    final success = await _api.playPlaylist(filesStr, state.volume, state.respectAdhan);
    state = state.copyWith(isLoading: false);
    if (success) {
      _ref.read(statusPollingProvider.notifier).pollStatus();
      startPolling();
    }
    return success;
  }

  Future<bool> togglePlayPause() async {
    if (state.isAdhan) return false;
    state = state.copyWith(isLoading: true);
    bool success = false;
    if (state.playbackState == 1) { // Playing -> Pause
      success = await _api.pauseAudio();
    } else if (state.playbackState == 2) { // Paused -> Resume
      success = await _api.resumeAudio();
    }
    state = state.copyWith(isLoading: false);
    if (success) {
      await updateTrackInfo();
      _ref.read(statusPollingProvider.notifier).pollStatus();
    }
    return success;
  }

  Future<bool> stop() async {
    state = state.copyWith(isLoading: true);
    final success = await _api.stopAudio();
    state = state.copyWith(isLoading: false);
    if (success) {
      stopPolling();
      _ref.read(statusPollingProvider.notifier).pollStatus();
    }
    return success;
  }

  Future<void> seek(int seconds) async {
    await _api.seekAudio(seconds);
    await updateTrackInfo();
  }

  Future<void> seekForward() async {
    final newPos = state.position + 10;
    await seek(newPos);
  }

  Future<void> seekBackward() async {
    final newPos = (state.position - 10).clamp(0, state.duration);
    await seek(newPos);
  }

  @override
  void dispose() {
    _trackInfoTimer?.cancel();
    super.dispose();
  }
}

final quickPlayerProvider = StateNotifierProvider<QuickPlayerNotifier, QuickPlayerState>((ref) {
  return QuickPlayerNotifier(ref);
});

// --- Eid Mode Provider ---
class EidState {
  final bool enabled;
  final bool manualEnabled;
  final bool autoEnabled;
  final bool autoDayActive;
  final String autoReason;
  final String takbeerFile;
  final int takbeerVolume;
  final List<dynamic> prayers;

  EidState({
    this.enabled = false,
    this.manualEnabled = false,
    this.autoEnabled = true,
    this.autoDayActive = false,
    this.autoReason = '',
    this.takbeerFile = '',
    this.takbeerVolume = 15,
    this.prayers = const [],
  });

  EidState copyWith({
    bool? enabled,
    bool? manualEnabled,
    bool? autoEnabled,
    bool? autoDayActive,
    String? autoReason,
    String? takbeerFile,
    int? takbeerVolume,
    List<dynamic>? prayers,
  }) {
    return EidState(
      enabled: enabled ?? this.enabled,
      manualEnabled: manualEnabled ?? this.manualEnabled,
      autoEnabled: autoEnabled ?? this.autoEnabled,
      autoDayActive: autoDayActive ?? this.autoDayActive,
      autoReason: autoReason ?? this.autoReason,
      takbeerFile: takbeerFile ?? this.takbeerFile,
      takbeerVolume: takbeerVolume ?? this.takbeerVolume,
      prayers: prayers ?? this.prayers,
    );
  }
}

class EidStateNotifier extends StateNotifier<AsyncValue<EidState>> {
  final Ref _ref;

  EidStateNotifier(this._ref) : super(const AsyncValue.loading()) {
    refresh();
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  Future<void> refresh() async {
    try {
      final status = await _api.getEidStatus();
      final config = await _api.getEidTakbeerConfig();

      final prayersList = config['prayers'] ?? [
        {'enabled': true, 'before': 15, 'after': 15},
        {'enabled': true, 'before': 15, 'after': 15},
        {'enabled': true, 'before': 15, 'after': 15},
        {'enabled': true, 'before': 15, 'after': 15},
        {'enabled': true, 'before': 15, 'after': 15},
      ];

      final effectiveEnabled = status['enabled'] == true || status['enabled'] == 1 || status['enabled'] == '1';
      final manualEn = status['manualEnabled'] != null
          ? (status['manualEnabled'] == true || status['manualEnabled'] == 1 || status['manualEnabled'] == '1')
          : effectiveEnabled;
      final autoEn = status['autoEnabled'] != null
          ? (status['autoEnabled'] == true || status['autoEnabled'] == 1 || status['autoEnabled'] == '1')
          : true;
      final autoDay = status['autoDayActive'] == true || status['autoDayActive'] == 1 || status['autoDayActive'] == '1';
      final reason = (status['autoReason'] ?? '').toString();

      if (mounted) {
        state = AsyncValue.data(EidState(
          enabled: effectiveEnabled,
          manualEnabled: manualEn,
          autoEnabled: autoEn,
          autoDayActive: autoDay,
          autoReason: reason,
          takbeerFile: status['takbeerFile'] ?? '',
          takbeerVolume: status['takbeerVolume'] ?? 15,
          prayers: prayersList,
        ));
      }
    } catch (e, stack) {
      if (mounted) {
        state = AsyncValue.error(e, stack);
      }
    }
  }

  Future<bool> toggleMode(bool manualVal, {bool? autoVal}) async {
    bool currentAuto = true;
    state.whenData((d) => currentAuto = d.autoEnabled);
    final targetAuto = autoVal ?? currentAuto;
    final success = await _api.toggleEidMode(manualVal, autoEnabled: targetAuto);
    if (success) {
      await refresh();
      _ref.read(statusPollingProvider.notifier).pollStatus();
    }
    return success;
  }

  Future<bool> saveConfig({
    required String file,
    required int volume,
    required List<dynamic> prayers,
  }) async {
    final fileSuccess = await _api.saveEidFile(file, volume);
    if (!fileSuccess) {
      await refresh();
      return false;
    }
    final configSuccess = await _api.saveEidTakbeerConfig(prayers);
    await refresh();
    return configSuccess;
  }

  Future<bool> triggerTakbeer(String file, int volume) async {
    final success = await _api.playDirectFile(file);
    if (success) {
      _ref.read(statusPollingProvider.notifier).pollStatus();
    }
    return success;
  }
}

final eidStateProvider = StateNotifierProvider<EidStateNotifier, AsyncValue<EidState>>((ref) {
  return EidStateNotifier(ref);
});

// --- Maghrib Alerts Provider ---
class MaghribState {
  final int offset;
  final List<dynamic> alerts;

  MaghribState({
    this.offset = 1,
    this.alerts = const [],
  });

  MaghribState copyWith({
    int? offset,
    List<dynamic>? alerts,
  }) {
    return MaghribState(
      offset: offset ?? this.offset,
      alerts: alerts ?? this.alerts,
    );
  }
}

class MaghribAlertsNotifier extends StateNotifier<AsyncValue<MaghribState>> {
  final Ref _ref;

  MaghribAlertsNotifier(this._ref) : super(const AsyncValue.loading()) {
    refresh();
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  Future<void> refresh() async {
    try {
      final alerts = await _api.getMaghribAlerts();
      final currentOffset = await _api.getMaghribOffset();
      
      // Ensure we have exactly 7 days in the list (Sunday to Saturday, 0 to 6)
      final parsedAlerts = List<Map<String, dynamic>>.generate(7, (index) {
        final existing = alerts.firstWhere(
          (a) => a['day'] == index,
          orElse: () => null,
        );
        if (existing != null) {
          return Map<String, dynamic>.from(existing);
        } else {
          return {
            'day': index,
            'file': '',
            'enabled': false,
            'volume': 15,
          };
        }
      });

      if (mounted) {
        state = AsyncValue.data(MaghribState(
          offset: currentOffset,
          alerts: parsedAlerts,
        ));
      }
    } catch (e, stack) {
      if (mounted) {
        state = AsyncValue.error(e, stack);
      }
    }
  }

  Future<bool> saveOffset(int val) async {
    final success = await _api.saveMaghribOffset(val);
    if (success) {
      state.whenData((data) {
        state = AsyncValue.data(data.copyWith(offset: val));
      });
    }
    return success;
  }

  Future<bool> saveAlerts(List<dynamic> alertsList) async {
    state = const AsyncValue.loading();
    final success = await _api.saveMaghribAlerts(alertsList);
    await refresh();
    return success;
  }
}

final maghribAlertsProvider = StateNotifierProvider<MaghribAlertsNotifier, AsyncValue<MaghribState>>((ref) {
  return MaghribAlertsNotifier(ref);
});

// --- Device Nicknames & Connections History Provider ---
class DeviceNicknamesNotifier extends StateNotifier<Map<String, String>> {
  DeviceNicknamesNotifier() : super(const {}) {
    _loadNicknames();
  }

  Future<void> _loadNicknames() async {
    final prefs = await SharedPreferences.getInstance();
    final keys = prefs.getKeys();
    final nicknames = <String, String>{};
    for (final key in keys) {
      if (key.startsWith('device_nickname_')) {
        final ip = key.replaceFirst('device_nickname_', '');
        final nickname = prefs.getString(key);
        if (nickname != null) {
          nicknames[ip] = nickname;
        }
      }
    }
    state = nicknames;
  }

  Future<void> setNickname(String ip, String nickname) async {
    final prefs = await SharedPreferences.getInstance();
    final key = 'device_nickname_$ip';
    if (nickname.trim().isEmpty) {
      await prefs.remove(key);
      final newState = Map<String, String>.from(state);
      newState.remove(ip);
      state = newState;
    } else {
      await prefs.setString(key, nickname.trim());
      final newState = Map<String, String>.from(state);
      newState[ip] = nickname.trim();
      state = newState;
    }
  }

  String getNickname(String? ip) {
    if (ip == null) return '';
    return state[ip] ?? '';
  }

  // Manage saved device connections history
  Future<List<String>> getSavedDevices() async {
    final prefs = await SharedPreferences.getInstance();
    return prefs.getStringList('saved_device_ips') ?? [];
  }

  Future<void> saveDeviceConnection(String ip) async {
    final prefs = await SharedPreferences.getInstance();
    final savedIps = prefs.getStringList('saved_device_ips') ?? [];
    if (!savedIps.contains(ip)) {
      savedIps.add(ip);
      await prefs.setStringList('saved_device_ips', savedIps);
    }
  }

  Future<void> removeDeviceConnection(String ip) async {
    final prefs = await SharedPreferences.getInstance();
    final savedIps = prefs.getStringList('saved_device_ips') ?? [];
    if (savedIps.contains(ip)) {
      savedIps.remove(ip);
      await prefs.setStringList('saved_device_ips', savedIps);
    }
    // Also remove nickname
    await setNickname(ip, '');
  }
}

final deviceNicknamesProvider =
    StateNotifierProvider<DeviceNicknamesNotifier, Map<String, String>>((ref) {
  return DeviceNicknamesNotifier();
});

// --- Saved ESP Devices Manager ---
class SavedEspDevice {
  final String ip;
  final String name;
  final String password;

  SavedEspDevice({
    required this.ip,
    required this.name,
    this.password = '',
  });

  Map<String, dynamic> toJson() => {
    'ip': ip,
    'name': name,
    'password': password,
  };

  factory SavedEspDevice.fromJson(Map<String, dynamic> json) => SavedEspDevice(
    ip: json['ip'] ?? '',
    name: json['name'] ?? '',
    password: json['password'] ?? '',
  );
}

class SavedDevicesNotifier extends StateNotifier<List<SavedEspDevice>> {
  SavedDevicesNotifier() : super([]) {
    load();
  }

  Future<void> load() async {
    final prefs = await SharedPreferences.getInstance();
    final rawList = prefs.getStringList('saved_esp_devices_v2') ?? [];
    if (rawList.isNotEmpty) {
      state = rawList.map((str) {
        try {
          return SavedEspDevice.fromJson(jsonDecode(str));
        } catch (_) {
          return null;
        }
      }).whereType<SavedEspDevice>().toList();
    } else {
      // Migrate from old saved_device_ips if exists
      final oldIps = prefs.getStringList('saved_device_ips') ?? [];
      final oldNicknames = prefs.getString('device_nicknames');
      Map<String, dynamic> nicknamesMap = {};
      if (oldNicknames != null) {
        try { nicknamesMap = jsonDecode(oldNicknames); } catch (_) {}
      }
      final migrated = oldIps.map((ip) => SavedEspDevice(
        ip: ip,
        name: nicknamesMap[ip] ?? ip,
      )).toList();
      state = migrated;
      _saveToPrefs();
    }
  }

  Future<void> _saveToPrefs() async {
    final prefs = await SharedPreferences.getInstance();
    final rawList = state.map((d) => jsonEncode(d.toJson())).toList();
    await prefs.setStringList('saved_esp_devices_v2', rawList);
  }

  Future<void> saveOrUpdateDevice(String ip, String name, [String? password]) async {
    final existingIdx = state.indexWhere((d) => d.ip == ip);
    final pwd = (password != null && password.isNotEmpty)
        ? password
        : (existingIdx != -1 ? state[existingIdx].password : '');
    final newDev = SavedEspDevice(ip: ip, name: name.trim().isEmpty ? ip : name.trim(), password: pwd);
    
    if (existingIdx != -1) {
      final updated = List<SavedEspDevice>.from(state);
      updated[existingIdx] = newDev;
      state = updated;
    } else {
      state = [...state, newDev];
    }
    await _saveToPrefs();
  }

  Future<void> removeDevice(String ip) async {
    state = state.where((d) => d.ip != ip).toList();
    await _saveToPrefs();
  }
}

final savedDevicesProvider =
    StateNotifierProvider<SavedDevicesNotifier, List<SavedEspDevice>>((ref) {
  return SavedDevicesNotifier();
});

class TimeFormatNotifier extends StateNotifier<bool> {
  TimeFormatNotifier() : super(false) {
    _load();
  }

  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    state = prefs.getBool('is_12_hour') ?? false;
  }

  Future<void> toggle(bool is12Hour) async {
    state = is12Hour;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('is_12_hour', is12Hour);
  }
}

final timeFormatProvider = StateNotifierProvider<TimeFormatNotifier, bool>((ref) {
  return TimeFormatNotifier();
});

// --- Presence & Smart Welcome State ---
class PresenceState {
  final Map<String, dynamic> config;
  final Map<String, dynamic> status;
  final bool isLoading;
  final bool isSaving;
  final bool isTesting;
  final bool isBleScanning;
  final bool isWifiScanning;
  final List<dynamic> bleDevices;
  final List<dynamic> wifiDevices;
  final String? error;

  PresenceState({
    this.config = const {},
    this.status = const {},
    this.isLoading = false,
    this.isSaving = false,
    this.isTesting = false,
    this.isBleScanning = false,
    this.isWifiScanning = false,
    this.bleDevices = const [],
    this.wifiDevices = const [],
    this.error,
  });

  PresenceState copyWith({
    Map<String, dynamic>? config,
    Map<String, dynamic>? status,
    bool? isLoading,
    bool? isSaving,
    bool? isTesting,
    bool? isBleScanning,
    bool? isWifiScanning,
    List<dynamic>? bleDevices,
    List<dynamic>? wifiDevices,
    String? error,
  }) {
    return PresenceState(
      config: config ?? this.config,
      status: status ?? this.status,
      isLoading: isLoading ?? this.isLoading,
      isSaving: isSaving ?? this.isSaving,
      isTesting: isTesting ?? this.isTesting,
      isBleScanning: isBleScanning ?? this.isBleScanning,
      isWifiScanning: isWifiScanning ?? this.isWifiScanning,
      bleDevices: bleDevices ?? this.bleDevices,
      wifiDevices: wifiDevices ?? this.wifiDevices,
      error: error,
    );
  }
}

class PresenceNotifier extends StateNotifier<PresenceState> {
  final Ref _ref;
  PresenceNotifier(this._ref) : super(PresenceState()) {
    refresh();
  }

  ApiService get _api => _ref.read(apiServiceProvider);

  Future<void> refresh() async {
    state = state.copyWith(isLoading: true, error: null);
    try {
      final config = await _api.getPresenceConfig();
      final status = await _api.getPresenceStatus();
      state = state.copyWith(
        config: config ?? state.config,
        status: status ?? state.status,
        isLoading: false,
      );
    } catch (e) {
      state = state.copyWith(isLoading: false, error: e.toString());
    }
  }

  Future<void> refreshStatusOnly() async {
    try {
      final status = await _api.getPresenceStatus();
      if (status != null && mounted) {
        state = state.copyWith(status: status);
      }
    } catch (_) {}
  }

  Future<bool> save(Map<String, dynamic> newConfig) async {
    state = state.copyWith(isSaving: true, error: null);
    try {
      final ok = await _api.savePresenceConfig(newConfig);
      if (ok) {
        state = state.copyWith(config: newConfig, isSaving: false);
        await refresh();
      } else {
        state = state.copyWith(isSaving: false, error: 'فشل حفظ الإعدادات');
      }
      return ok;
    } catch (e) {
      state = state.copyWith(isSaving: false, error: e.toString());
      return false;
    }
  }

  Future<bool> saveDevice({
    int id = -1,
    required String name,
    required String mac,
    String welcomeFile = '/Azan/welcome.mp3',
    bool enabled = true,
  }) async {
    state = state.copyWith(isSaving: true);
    try {
      final ok = await _api.savePresenceDevice(
        id: id,
        name: name,
        mac: mac,
        welcomeFile: welcomeFile,
        enabled: enabled,
      );
      state = state.copyWith(isSaving: false);
      if (ok) await refresh();
      return ok;
    } catch (e) {
      state = state.copyWith(isSaving: false, error: e.toString());
      return false;
    }
  }

  Future<bool> deleteDevice(int id) async {
    state = state.copyWith(isSaving: true);
    try {
      final ok = await _api.deletePresenceDevice(id);
      state = state.copyWith(isSaving: false);
      if (ok) await refresh();
      return ok;
    } catch (e) {
      state = state.copyWith(isSaving: false, error: e.toString());
      return false;
    }
  }

  Future<bool> testWelcome({String file = '', int volume = 0}) async {
    state = state.copyWith(isTesting: true);
    try {
      final ok = await _api.testPresenceWelcome(file: file, volume: volume);
      state = state.copyWith(isTesting: false);
      return ok;
    } catch (e) {
      state = state.copyWith(isTesting: false);
      return false;
    }
  }

  Future<void> scanBle() async {
    state = state.copyWith(isBleScanning: true, bleDevices: []);
    try {
      final devs = await _api.scanPresenceBle(start: true);
      state = state.copyWith(isBleScanning: false, bleDevices: devs);
    } catch (e) {
      state = state.copyWith(isBleScanning: false);
    }
  }

  Future<void> scanWifi() async {
    state = state.copyWith(isWifiScanning: true, wifiDevices: []);
    try {
      final devs = await _api.scanPresenceWifi();
      state = state.copyWith(isWifiScanning: false, wifiDevices: devs);
    } catch (e) {
      state = state.copyWith(isWifiScanning: false);
    }
  }
}

final presenceProvider = StateNotifierProvider<PresenceNotifier, PresenceState>((ref) {
  return PresenceNotifier(ref);
});

// --- Shared Radio Stations State (178+ stations) ---
class RadioStationsState {
  final List<Map<String, dynamic>> stations;
  final bool isLoading;
  final String? error;

  const RadioStationsState({
    this.stations = const [],
    this.isLoading = false,
    this.error,
  });

  RadioStationsState copyWith({
    List<Map<String, dynamic>>? stations,
    bool? isLoading,
    String? error,
  }) {
    return RadioStationsState(
      stations: stations ?? this.stations,
      isLoading: isLoading ?? this.isLoading,
      error: error,
    );
  }
}

class RadioStationsNotifier extends StateNotifier<RadioStationsState> {
  static const List<Map<String, dynamic>> offlineRadios = [
    { "name": "إذاعة القرآن الكريم من القاهرة", "url": "http://stream.radiojar.com/8s5u5tpdtwzuv" },
    { "name": "إذاعة القرآن الكريم - السعودية", "url": "https://stream.radiojar.com/0tpy1h0kxtzuv" },
    { "name": "الإذاعة العامة - تلاوات متنوعة (Mp3Quran)", "url": "https://backup.qurango.net/radio/mix" },
    { "name": "إذاعة تكبيرات العيد", "url": "https://backup.qurango.net/radio/eid" },
    { "name": "إذاعة الرقية الشرعية", "url": "https://backup.qurango.net/radio/roqiah" },
    { "name": "إذاعة تفسير القرآن الكريم", "url": "https://backup.qurango.net/radio/tafseer" },
    { "name": "آيات السكينة (تلاوات هادئة)", "url": "https://backup.qurango.net/radio/sakeenah" },
    { "name": "أذكار الصباح (بث مباشر)", "url": "https://backup.qurango.net/radio/athkar_sabah" },
    { "name": "أذكار المساء (بث مباشر)", "url": "https://backup.qurango.net/radio/athkar_masa" },
    { "name": "إذاعة مشاري العفاسي", "url": "https://backup.qurango.net/radio/mishary_alafasi" },
    { "name": "إذاعة عبد الرحمن السديس", "url": "https://backup.qurango.net/radio/abdulrahman_alsudaes" },
    { "name": "إذاعة ماهر المعيقلي", "url": "https://backup.qurango.net/radio/maher" },
    { "name": "إذاعة عبد الباسط عبد الصمد (المصحف المجود)", "url": "https://backup.qurango.net/radio/abdulbasit_abdulsamad_mojawwad" }
  ];

  final Dio _dio = Dio(BaseOptions(
    connectTimeout: const Duration(seconds: 10),
    receiveTimeout: const Duration(seconds: 12),
  ));

  RadioStationsNotifier() : super(const RadioStationsState(stations: offlineRadios)) {
    fetchRadios();
  }

  Future<void> fetchRadios({bool force = false}) async {
    if (!force && state.stations.length > offlineRadios.length) return;
    state = state.copyWith(isLoading: true, error: null);
    try {
      final response = await _dio.get('https://mp3quran.net/api/v3/radios?language=ar');
      if (response.statusCode == 200) {
        final data = response.data;
        if (data != null && data['radios'] is List) {
          final fetched = (data['radios'] as List).cast<Map<String, dynamic>>();
          final cairoRadio = {
            "id": 99999,
            "name": "إذاعة القرآن الكريم من القاهرة",
            "url": "http://stream.radiojar.com/8s5u5tpdtwzuv"
          };
          final merged = <Map<String, dynamic>>[cairoRadio];
          for (final r in fetched) {
            final url = r['url']?.toString() ?? '';
            if (url.isNotEmpty && !merged.any((m) => m['url'] == url)) {
              merged.add(Map<String, dynamic>.from(r));
            }
          }
          state = state.copyWith(stations: merged, isLoading: false);
          return;
        }
      }
      throw Exception('Format error');
    } catch (e) {
      debugPrint('Failed to load online radios: $e');
      state = state.copyWith(
        stations: state.stations.isEmpty ? offlineRadios : state.stations,
        isLoading: false,
        error: 'تعذر تحميل الإذاعات من الإنترنت، تم تفعيل القائمة المحلية',
      );
    }
  }
}

final radioStationsProvider = StateNotifierProvider<RadioStationsNotifier, RadioStationsState>((ref) {
  return RadioStationsNotifier();
});

